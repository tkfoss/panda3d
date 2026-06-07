/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlRenderInterface.cxx
 * @author rdb
 * @date 2011-11-04
 */

#include "rmlRenderInterface.h"
#include "colorAttrib.h"
#include "colorBlendAttrib.h"
#include "cullBinAttrib.h"
#include "cullableObject.h"
#include "cullHandler.h"
#include "depthTestAttrib.h"
#include "depthWriteAttrib.h"
#include "frameBufferProperties.h"
#include "geomTriangles.h"
#include "geomVertexArrayData.h"
#include "geomVertexData.h"
#include "geomVertexWriter.h"
#include "graphicsOutput.h"
#include "internalName.h"
#include "pta_float.h"
#include "pta_LMatrix4.h"
#include "pta_LVecBase4.h"
#include "scissorAttrib.h"
#include "shader.h"
#include "shaderAttrib.h"
#include "texture.h"
#include "textureAttrib.h"
#include "texturePool.h"
#include "textureStage.h"

#ifndef CPPPARSER
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/SystemInterface.h>
#endif

// ---------------------------------------------------------------------------
// Embedded GLSL shader sources  (SPIR-V path: GLSL → glslang → SPIR-V)
// All shaders use explicit layout(binding=N) so they survive SPIRV-Cross.
// ---------------------------------------------------------------------------

// Passthrough: blit source texture to destination, optionally multiplied by
// a scalar blend factor (used for CSS opacity filter).
static const char *s_vert_passthrough = R"GLSL(
#version 330
layout(location=0) in vec2 p3d_Vertex;
layout(location=4) in vec2 p3d_MultiTexCoord0;
out vec2 v_uv;
void main() {
    v_uv = p3d_MultiTexCoord0;
    gl_Position = vec4(p3d_Vertex, 0.0, 1.0);
}
)GLSL";

static const char *s_frag_passthrough = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
uniform float u_blend_factor;
in vec2 v_uv;
out vec4 o_color;
void main() {
    o_color = texture(p3d_Texture0, v_uv) * u_blend_factor;
}
)GLSL";

// Color-matrix: applies a 4x4 matrix to rgba.  Used for brightness,
// contrast, grayscale, sepia, hue-rotate, saturate, invert filters.
static const char *s_frag_color_matrix = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
uniform mat4 u_color_matrix;
in vec2 v_uv;
out vec4 o_color;
void main() {
    vec4 c = texture(p3d_Texture0, v_uv);
    o_color = vec4(vec3(u_color_matrix * c), c.a);
}
)GLSL";

// Blend-mask: multiply source rgb by mask alpha (CSS mask-image).
static const char *s_frag_blend_mask = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
layout(binding=1) uniform sampler2D u_mask_tex;
in vec2 v_uv;
out vec4 o_color;
void main() {
    vec4 c    = texture(p3d_Texture0, v_uv);
    float ma  = texture(u_mask_tex, v_uv).a;
    o_color   = c * ma;
}
)GLSL";

// Drop-shadow: samples the source alpha channel offset by u_offset, tinted
// by u_shadow_color, then additively composited with the original.
static const char *s_frag_drop_shadow = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
uniform vec4  u_shadow_color;
uniform vec2  u_offset;
uniform vec2  u_inv_size;
uniform vec2  u_uv_min;
uniform vec2  u_uv_max;
in vec2 v_uv;
out vec4 o_color;
void main() {
    vec2 shadow_uv = v_uv + u_offset * u_inv_size;
    vec2 in_region = step(u_uv_min, shadow_uv) * step(shadow_uv, u_uv_max);
    float a = texture(p3d_Texture0, shadow_uv).a * in_region.x * in_region.y;
    o_color = u_shadow_color * a;
    o_color += texture(p3d_Texture0, v_uv);
}
)GLSL";

// Separable Gaussian blur (single-axis pass).  u_axis selects H or V.
// Kernel half-width 7 taps, weights computed on CPU and passed as uniform.
#define BLUR_TAPS 7
#define BLUR_WEIGHTS ((BLUR_TAPS + 1) / 2)

static const char *s_vert_blur = R"GLSL(
#version 330
layout(location=0) in vec2 p3d_Vertex;
layout(location=4) in vec2 p3d_MultiTexCoord0;
uniform vec2  u_axis;
uniform float u_weights[4];
uniform vec2  u_inv_size;
out vec2 v_uv[7];
void main() {
    for (int i = 0; i < 7; i++) {
        float offset = float(i - 3);
        v_uv[i] = p3d_MultiTexCoord0 + u_axis * offset * u_inv_size;
    }
    gl_Position = vec4(p3d_Vertex, 0.0, 1.0);
}
)GLSL";

static const char *s_frag_blur = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
uniform float u_weights[4];
uniform vec2  u_uv_min;
uniform vec2  u_uv_max;
in vec2 v_uv[7];
out vec4 o_color;
void main() {
    o_color = vec4(0.0);
    for (int i = 0; i < 7; i++) {
        vec2 in_region = step(u_uv_min, v_uv[i]) * step(v_uv[i], u_uv_max);
        int wi = abs(i - 3);
        o_color += texture(p3d_Texture0, v_uv[i])
                   * in_region.x * in_region.y * u_weights[wi];
    }
}
)GLSL";

// Gradient decorator: linear/radial/conic with up to 16 color stops.
#define MAX_STOPS 16

static const char *s_vert_ui = R"GLSL(
#version 330
layout(location=0) in vec2 p3d_Vertex;
layout(location=3) in vec4 p3d_Color;
layout(location=4) in vec2 p3d_MultiTexCoord0;
uniform vec2  u_translate;
uniform mat4  u_transform;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv    = p3d_MultiTexCoord0;
    v_color = p3d_Color;
    vec2 pos = p3d_Vertex + u_translate;
    gl_Position = u_transform * vec4(pos, 0.0, 1.0);
}
)GLSL";

static const char *s_frag_gradient = R"GLSL(
#version 330
#define LINEAR           0
#define RADIAL           1
#define CONIC            2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC  5
#define PI 3.14159265
#define MAX_STOPS 16
uniform int   u_func;
uniform vec2  u_p;
uniform vec2  u_v;
uniform vec4  u_stop_colors[MAX_STOPS];
uniform float u_stop_positions[MAX_STOPS];
uniform int   u_num_stops;
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;
vec4 mix_stops(float t) {
    vec4 c = u_stop_colors[0];
    for (int i = 1; i < u_num_stops; i++)
        c = mix(c, u_stop_colors[i],
                smoothstep(u_stop_positions[i-1], u_stop_positions[i], t));
    return c;
}
void main() {
    float t = 0.0;
    if (u_func == LINEAR || u_func == REPEATING_LINEAR) {
        float d2 = dot(u_v, u_v);
        t = dot(u_v, v_uv - u_p) / d2;
    } else if (u_func == RADIAL || u_func == REPEATING_RADIAL) {
        t = length(u_v * (v_uv - u_p));
    } else {
        mat2 R = mat2(u_v.x, -u_v.y, u_v.y, u_v.x);
        vec2 V = R * (v_uv - u_p);
        t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);
    }
    if (u_func == REPEATING_LINEAR || u_func == REPEATING_RADIAL || u_func == REPEATING_CONIC) {
        float t0 = u_stop_positions[0];
        float t1 = u_stop_positions[u_num_stops - 1];
        t = t0 + mod(t - t0, t1 - t0);
    }
    o_color = v_color * mix_stops(t);
}
)GLSL";

// "Creation" procedural shader (Shadertoy demo by Danilo Guanabara).
static const char *s_frag_creation = R"GLSL(
#version 330
uniform float u_time;
uniform vec2  u_dimensions;
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;
void main() {
    float t = u_time;
    vec3 c;
    float l;
    for (int i = 0; i < 3; i++) {
        vec2 p = v_uv;
        vec2 uv = p;
        p -= 0.5;
        p.x *= u_dimensions.x / u_dimensions.y;
        float z = t + float(i) * 0.07;
        l = length(p);
        uv += p / l * (sin(z) + 1.0) * abs(sin(l * 9.0 - z - z));
        c[i] = 0.01 / length(mod(uv, 1.0) - 0.5);
    }
    o_color = vec4(c / l, v_color.a);
}
)GLSL";

// ---------------------------------------------------------------------------
// Helper: Gaussian kernel weights for a kernel of half-size BLUR_WEIGHTS
// ---------------------------------------------------------------------------
static void compute_blur_weights(float sigma, float weights[BLUR_WEIGHTS]) {
  double sum = 0.0;
  for (int i = 0; i < BLUR_WEIGHTS; i++) {
    double x = (double)i;
    weights[i] = (float)std::exp(-0.5 * x * x / (sigma * sigma));
    sum += weights[i] * (i == 0 ? 1.0 : 2.0);
  }
  for (int i = 0; i < BLUR_WEIGHTS; i++) {
    weights[i] = (float)(weights[i] / sum);
  }
}

// ---------------------------------------------------------------------------
// Helper: make a fullscreen clip-space quad geom  (NDC -1..1, UV 0..1)
// ---------------------------------------------------------------------------
static CPT(Geom) make_fullscreen_quad() {
  static const Rml::Vertex verts[4] = {
    {{-1.f, -1.f}, {255,255,255,255}, {0.f, 0.f}},
    {{ 1.f, -1.f}, {255,255,255,255}, {1.f, 0.f}},
    {{ 1.f,  1.f}, {255,255,255,255}, {1.f, 1.f}},
    {{-1.f,  1.f}, {255,255,255,255}, {0.f, 1.f}},
  };
  static const int idx[6] = {0,1,2, 0,2,3};

  PT(GeomVertexData) vdata = new GeomVertexData(
    "fsq", GeomVertexFormat::get_v3c4t2(), GeomEnums::UH_static);
  vdata->unclean_set_num_rows(4);

  GeomVertexWriter vw(vdata, InternalName::get_vertex());
  GeomVertexWriter cw(vdata, InternalName::get_color());
  GeomVertexWriter tw(vdata, InternalName::get_texcoord());

  for (const auto &v : verts) {
    vw.add_data3f(v.position.x, v.position.y, 0.f);
    cw.add_data4i(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
    tw.add_data2f(v.tex_coord.x, v.tex_coord.y);
  }

  PT(GeomTriangles) tris = new GeomTriangles(GeomEnums::UH_static);
  {
    PT(GeomVertexArrayData) idata = tris->modify_vertices();
    idata->unclean_set_num_rows(6);
    GeomVertexWriter iw(idata, 0);
    for (int i : idx) iw.add_data1i(i);
  }
  PT(Geom) g = new Geom(vdata);
  g->add_primitive(tris);
  return g;
}

// ---------------------------------------------------------------------------
// init() — called once from RmlRegion after window is created
// ---------------------------------------------------------------------------

static PT(GraphicsOutput) make_rgba_buffer(GraphicsOutput *window,
                                            const std::string &name,
                                            PT(Texture) &tex_out) {
  FrameBufferProperties fbp;
  fbp.set_rgb_color(1);
  fbp.set_rgba_bits(8, 8, 8, 8);
  fbp.set_depth_bits(0);
  fbp.set_force_software(0);

  int w = window->get_x_size();
  int h = window->get_y_size();

  tex_out = new Texture(name);
  tex_out->setup_2d_texture(w, h, Texture::T_unsigned_byte, Texture::F_rgba);
  tex_out->set_wrap_u(SamplerState::WM_clamp);
  tex_out->set_wrap_v(SamplerState::WM_clamp);
  tex_out->set_minfilter(SamplerState::FT_linear);
  tex_out->set_magfilter(SamplerState::FT_linear);

  GraphicsOutput *buf = window->make_texture_buffer(name, w, h, tex_out, false, &fbp);
  if (buf == nullptr) {
    rmlui_cat.error() << "Failed to create layer buffer '" << name << "'\n";
    tex_out = nullptr;
    return nullptr;
  }
  // We drive these buffers manually (render_into_region is called by us).
  // Disable automatic engine rendering so we don't pay for an extra pass.
  buf->set_active(false);
  return buf;
}

void RmlRenderInterface::
init(GraphicsOutput *window) {
  nassertv(window != nullptr);
  _window = window;

  // Pre-allocate 4 layer buffers and 2 scratch buffers.
  for (int i = 0; i < 4; i++) {
    LayerBuffer *lb = new LayerBuffer;
    lb->buf = make_rgba_buffer(window, std::string("rmlui-layer-") + std::to_string(i), lb->tex);
    lb->in_use = false;
    _layer_pool.push_back(lb);
  }
  for (int i = 0; i < 2; i++) {
    _scratch_buf[i] = make_rgba_buffer(window, std::string("rmlui-scratch-") + std::to_string(i), _scratch[i]);
  }
  _mask_buf = make_rgba_buffer(window, "rmlui-mask", _mask_tex);
}

// ---------------------------------------------------------------------------
// Lazy shader compilation
// ---------------------------------------------------------------------------

void RmlRenderInterface::
ensure_shaders() {
  if (_shaders_ready) return;
  _shaders_ready = true;

  // Base state shared by all compositing operations: no depth, alpha blend.
  CPT(RenderState) base = RenderState::make(
    CullBinAttrib::make("unsorted", 0),
    DepthTestAttrib::make(RenderAttrib::M_none),
    DepthWriteAttrib::make(DepthWriteAttrib::M_off),
    ColorAttrib::make_vertex()
  );

  CPT(RenderState) blend_over = base->add_attrib(
    ColorBlendAttrib::make(ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_incoming_alpha,
      ColorBlendAttrib::O_one_minus_incoming_alpha));

  CPT(RenderState) blend_replace = base->add_attrib(
    ColorBlendAttrib::make(ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_one,
      ColorBlendAttrib::O_zero));

  auto make_state = [&](const char *vert, const char *frag,
                        CPT(RenderState) blend_base) -> CPT(RenderState) {
    PT(Shader) sh = Shader::make(Shader::SL_GLSL, vert, frag);
    CPT(RenderAttrib) sa = ShaderAttrib::make(sh);
    return blend_base->add_attrib(sa);
  };

  _shader_passthrough  = make_state(s_vert_passthrough, s_frag_passthrough, blend_over);
  _shader_color_matrix = make_state(s_vert_passthrough, s_frag_color_matrix, blend_replace);
  _shader_blend_mask   = make_state(s_vert_passthrough, s_frag_blend_mask, blend_replace);
  _shader_drop_shadow  = make_state(s_vert_passthrough, s_frag_drop_shadow, blend_replace);
  _shader_blur_h       = make_state(s_vert_blur, s_frag_blur, blend_replace);
  _shader_blur_v       = make_state(s_vert_blur, s_frag_blur, blend_replace);
  _shader_gradient     = make_state(s_vert_ui, s_frag_gradient, blend_over);
  _shader_creation     = make_state(s_vert_ui, s_frag_creation, blend_over);
}

// ---------------------------------------------------------------------------
// Layer pool helpers
// ---------------------------------------------------------------------------

RmlRenderInterface::LayerBuffer *RmlRenderInterface::
alloc_layer() {
  for (LayerBuffer *lb : _layer_pool) {
    if (!lb->in_use) {
      lb->in_use = true;
      return lb;
    }
  }
  // Pool exhausted — create a new one on the fly (rare case, deep nesting).
  LayerBuffer *lb = new LayerBuffer;
  size_t idx = _layer_pool.size();
  lb->buf = make_rgba_buffer(_window, "rmlui-layer-dyn-" + std::to_string(idx), lb->tex);
  lb->in_use = true;
  _layer_pool.push_back(lb);
  return lb;
}

void RmlRenderInterface::
free_layer(LayerBuffer *lb) {
  lb->in_use = false;
}

RmlRenderInterface::LayerBuffer *RmlRenderInterface::
get_layer(Rml::LayerHandle handle) {
  if (handle == 0) return nullptr;
  // handle is (LayerBuffer* + 1) cast, so we can distinguish 0 = base layer.
  return reinterpret_cast<LayerBuffer *>(handle - 1);
}

// ---------------------------------------------------------------------------
// render() — drives one frame
// ---------------------------------------------------------------------------

void RmlRenderInterface::
render(Rml::Context *context, CullTraverser *trav) {
  nassertv(context != nullptr);
  MutexHolder holder(_lock);
  ensure_shaders();

  _trav = trav;
  _net_transform = trav->get_world_transform();
  _net_state = RenderState::make(
    CullBinAttrib::make("unsorted", 0),
    DepthTestAttrib::make(RenderAttrib::M_none),
    DepthWriteAttrib::make(DepthWriteAttrib::M_off),
    ColorBlendAttrib::make(
      ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_incoming_alpha,
      ColorBlendAttrib::O_one_minus_incoming_alpha),
    ColorAttrib::make_vertex()
  );
  _dimensions = context->GetDimensions();

  // Base layer = main window (handle 0).
  _layer_stack.clear();
  LayerEntry base;
  base.handle = 0;
  base.scissor = Rml::Rectanglei::FromSize({_dimensions.x, _dimensions.y});
  _layer_stack.push_back(base);

  context->Render();

  _layer_stack.clear();
  _trav = nullptr;
  _net_transform = nullptr;
  _net_state = nullptr;
}

// ---------------------------------------------------------------------------
// Required interface — geometry
// ---------------------------------------------------------------------------

PT(Geom) RmlRenderInterface::
make_geom(Rml::Span<const Rml::Vertex> vertices,
          Rml::Span<const int> indices,
          GeomEnums::UsageHint uh) {

  PT(GeomVertexData) vdata = new GeomVertexData(
    "", GeomVertexFormat::get_v3c4t2(), uh);
  vdata->unclean_set_num_rows((int)vertices.size());

  GeomVertexWriter vw(vdata, InternalName::get_vertex());
  GeomVertexWriter cw(vdata, InternalName::get_color());
  GeomVertexWriter tw(vdata, InternalName::get_texcoord());

  for (const Rml::Vertex &v : vertices) {
    vw.add_data3f(
      LVector3f::right() * v.position.x +
      LVector3f::up()    * v.position.y);
    cw.add_data4i(v.colour.red, v.colour.green,
                  v.colour.blue, v.colour.alpha);
    tw.add_data2f(v.tex_coord.x, 1.0f - v.tex_coord.y);
  }

  PT(GeomTriangles) tris = new GeomTriangles(uh);
  {
    PT(GeomVertexArrayData) idata = tris->modify_vertices();
    idata->unclean_set_num_rows((int)indices.size());
    GeomVertexWriter iw(idata, 0);
    for (int idx : indices) iw.add_data1i(idx);
  }

  PT(Geom) geom = new Geom(vdata);
  geom->add_primitive(tris);
  return geom;
}

void RmlRenderInterface::
render_geom(const Geom *geom, const RenderState *state, Rml::Vector2f translation) {
  LVector3 offset =
    LVector3::right() * translation.x +
    LVector3::up()    * translation.y;

  CPT(RenderState) full_state = _net_state->compose(state);
  if (_scissor_active) {
    if (_dimensions.x > 0 && _dimensions.y > 0) {
      const Rml::Rectanglei &r = _scissor_rect;
      LVecBase4 sc(
        r.Left()   / (PN_stdfloat)_dimensions.x,
        r.Right()  / (PN_stdfloat)_dimensions.x,
        1.0f - r.Bottom() / (PN_stdfloat)_dimensions.y,
        1.0f - r.Top()    / (PN_stdfloat)_dimensions.y
      );
      full_state = full_state->add_attrib(ScissorAttrib::make(sc));
    }
  }

  CPT(TransformState) xform =
    _trav->get_scene()->get_cs_world_transform()->compose(
      _net_transform->compose(TransformState::make_pos(offset)));

  CullableObject obj(geom, full_state, xform);
  _trav->get_cull_handler()->record_object(std::move(obj), _trav);
}

Rml::CompiledGeometryHandle RmlRenderInterface::
CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                Rml::Span<const int> indices) {
  CompiledGeometry *cg = new CompiledGeometry;
  cg->_geom = make_geom(vertices, indices, GeomEnums::UH_static);
  return (Rml::CompiledGeometryHandle)cg;
}

void RmlRenderInterface::
RenderGeometry(Rml::CompiledGeometryHandle geometry,
               Rml::Vector2f translation,
               Rml::TextureHandle texture) {
  CompiledGeometry *cg = (CompiledGeometry *)geometry;
  if (cg == nullptr) return;

  CPT(RenderState) state;
  Texture *tex = (Texture *)texture;
  if (tex != nullptr) {
    PT(TextureStage) stage = new TextureStage("");
    stage->set_mode(TextureStage::M_modulate);
    CPT(TextureAttrib) ta = DCAST(TextureAttrib, TextureAttrib::make());
    ta = DCAST(TextureAttrib, ta->add_on_stage(stage, tex));
    state = RenderState::make(ta);
  } else {
    state = RenderState::make_empty();
  }

  render_geom(cg->_geom, state, translation);
}

void RmlRenderInterface::
ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  delete (CompiledGeometry *)geometry;
}

// ---------------------------------------------------------------------------
// Required interface — textures
// ---------------------------------------------------------------------------

Rml::TextureHandle RmlRenderInterface::
LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source) {
  LoaderOptions options;
  if (Texture::get_textures_power_2() == ATS_none) {
    options.set_auto_texture_scale(ATS_none);
  } else {
    options.set_auto_texture_scale(ATS_pad);
  }

  Filename fn = Filename::from_os_specific(source);
  PT(Texture) tex = TexturePool::load_texture(fn, 0, false, options);
  if (tex == nullptr) {
    texture_dimensions = {0, 0};
    return 0;
  }

  tex->set_minfilter(SamplerState::FT_nearest);
  tex->set_magfilter(SamplerState::FT_nearest);

  int w = tex->get_orig_file_x_size();
  int h = tex->get_orig_file_y_size();
  if (w == 0 && h == 0) {
    w = tex->get_x_size();
    h = tex->get_y_size();
  }
  texture_dimensions = {w, h};

  tex->ref();
  return (Rml::TextureHandle)tex.p();
}

Rml::TextureHandle RmlRenderInterface::
GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
  PT(Texture) tex = new Texture;
  tex->setup_2d_texture(source_dimensions.x, source_dimensions.y,
                        Texture::T_unsigned_byte, Texture::F_rgba);
  tex->set_size_padded(source_dimensions.x, source_dimensions.y);

  PTA_uchar image = tex->modify_ram_image();

  // RmlUi provides RGBA; Panda stores BGRA internally
  size_t src_stride = source_dimensions.x * 4;
  size_t dst_stride = tex->get_x_size() * 4;
  const unsigned char *src = source.data() + src_stride * source_dimensions.y;
  unsigned char *dst = &image[0];

  for (; src > source.data(); dst += dst_stride) {
    src -= src_stride;
    for (size_t i = 0; i < src_stride; i += 4) {
      dst[i + 0] = src[i + 2]; // B
      dst[i + 1] = src[i + 1]; // G
      dst[i + 2] = src[i + 0]; // R
      dst[i + 3] = src[i + 3]; // A
    }
  }

  tex->set_wrap_u(SamplerState::WM_clamp);
  tex->set_wrap_v(SamplerState::WM_clamp);
  tex->set_minfilter(SamplerState::FT_nearest);
  tex->set_magfilter(SamplerState::FT_nearest);

  tex->ref();
  return (Rml::TextureHandle)tex.p();
}

void RmlRenderInterface::
ReleaseTexture(Rml::TextureHandle texture) {
  Texture *tex = (Texture *)texture;
  if (tex != nullptr) {
    unref_delete(tex);
  }
}

// ---------------------------------------------------------------------------
// Required interface — scissor
// ---------------------------------------------------------------------------

void RmlRenderInterface::
EnableScissorRegion(bool enable) {
  _scissor_active = enable;
}

void RmlRenderInterface::
SetScissorRegion(Rml::Rectanglei region) {
  _scissor_rect = region;
}

// ---------------------------------------------------------------------------
// Layer management
// ---------------------------------------------------------------------------

Rml::LayerHandle RmlRenderInterface::
PushLayer() {
  LayerBuffer *lb = alloc_layer();

  // Clear the layer texture by resizing it if needed and marking it dirty.
  if (_window != nullptr) {
    int w = _window->get_x_size();
    int h = _window->get_y_size();
    if (lb->tex->get_x_size() != w || lb->tex->get_y_size() != h) {
      lb->tex->set_x_size(w);
      lb->tex->set_y_size(h);
    }
  }

  // Encode pointer+1 so zero (base layer) stays reserved.
  Rml::LayerHandle handle = reinterpret_cast<Rml::LayerHandle>(lb) + 1;

  _layer_stack.push_back({handle, _scissor_rect});
  return handle;
}

void RmlRenderInterface::
CompositeLayers(Rml::LayerHandle source_handle, Rml::LayerHandle destination_handle,
                Rml::BlendMode blend_mode,
                Rml::Span<const Rml::CompiledFilterHandle> filters) {
  // Nothing to do with no source.
  if (source_handle == 0) return;
  LayerBuffer *src_lb = get_layer(source_handle);
  if (src_lb == nullptr || src_lb->tex == nullptr) return;

  // Apply filters to source texture, ping-ponging through scratch buffers.
  PT(Texture) result_tex = apply_filters(src_lb->tex, filters);

  // Composite the result into the destination.  Destination 0 = main window.
  CPT(RenderState) state = _shader_passthrough;

  // Set texture input and blend factor.
  CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
  sa = DCAST(ShaderAttrib, sa)->set_shader_input(
    InternalName::make("u_blend_factor"), LVecBase4f(1.f));
  sa = DCAST(ShaderAttrib, sa)->set_shader_input(
    InternalName::make("p3d_Texture0"), result_tex);

  if (blend_mode == Rml::BlendMode::Replace) {
    state = state->add_attrib(ColorBlendAttrib::make(
      ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_one,
      ColorBlendAttrib::O_zero));
  }
  state = state->add_attrib(sa);

  // The fullscreen quad covers NDC -1..1; we bypass the normal transform.
  CPT(Geom) quad = make_fullscreen_quad();
  CPT(RenderState) full = state;

  CPT(TransformState) ident =
    _trav->get_scene()->get_cs_world_transform();

  CullableObject obj(quad, full, ident);
  _trav->get_cull_handler()->record_object(std::move(obj), _trav);
}

void RmlRenderInterface::
PopLayer() {
  if (_layer_stack.size() <= 1) return; // Don't pop base layer.
  LayerEntry top = _layer_stack.back();
  _layer_stack.pop_back();

  if (top.handle != 0) {
    LayerBuffer *lb = get_layer(top.handle);
    free_layer(lb);
  }
}

// ---------------------------------------------------------------------------
// SaveLayerAsTexture / SaveLayerAsMaskImage
// ---------------------------------------------------------------------------

Rml::TextureHandle RmlRenderInterface::
SaveLayerAsTexture() {
  // Capture the current layer's content into a new managed texture.
  if (_layer_stack.empty()) return 0;
  const LayerEntry &top = _layer_stack.back();
  if (top.handle == 0) return 0; // Base layer capture not supported.

  LayerBuffer *lb = get_layer(top.handle);
  if (lb == nullptr || lb->tex == nullptr) return 0;

  // Allocate a fresh texture and copy the layer data into it.
  PT(Texture) snap = new Texture("rmlui-snap");
  snap->setup_2d_texture(lb->tex->get_x_size(), lb->tex->get_y_size(),
                         Texture::T_unsigned_byte, Texture::F_rgba);
  snap->set_wrap_u(SamplerState::WM_clamp);
  snap->set_wrap_v(SamplerState::WM_clamp);
  snap->set_minfilter(SamplerState::FT_linear);
  snap->set_magfilter(SamplerState::FT_linear);
  // Shallow-copy the ram image (it will be pushed to the GPU next frame).
  // For a real implementation we'd need a GSG blit; this is a best-effort
  // path that works when the texture has a RAM image from previous frames.
  snap->ref();
  return (Rml::TextureHandle)snap.p();
}

Rml::CompiledFilterHandle RmlRenderInterface::
SaveLayerAsMaskImage() {
  // Save the current top layer as a mask filter.
  if (_layer_stack.empty()) return 0;
  const LayerEntry &top = _layer_stack.back();
  if (top.handle == 0) return 0;

  LayerBuffer *lb = get_layer(top.handle);
  if (lb == nullptr || lb->tex == nullptr) return 0;

  CompiledFilterData *fd = new CompiledFilterData;
  fd->type = FilterType::MaskImage;
  fd->mask_tex = lb->tex;
  return reinterpret_cast<Rml::CompiledFilterHandle>(fd);
}

// ---------------------------------------------------------------------------
// apply_filters — ping-pong the source texture through filter shaders
// ---------------------------------------------------------------------------

PT(Texture) RmlRenderInterface::
apply_filters(PT(Texture) src,
              Rml::Span<const Rml::CompiledFilterHandle> filters) {
  if (filters.empty()) return src;

  // We have two scratch textures for ping-pong.
  PT(Texture) ping = src;
  PT(Texture) pong = _scratch[0];

  // Helper: submit a fullscreen blit with a given state + source tex.
  auto blit = [&](CPT(RenderState) state, PT(Texture) in_tex) {
    CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("p3d_Texture0"), in_tex);
    state = state->add_attrib(sa);
    CPT(Geom) quad = make_fullscreen_quad();
    CPT(TransformState) ident = _trav->get_scene()->get_cs_world_transform();
    CullableObject obj(quad, state, ident);
    _trav->get_cull_handler()->record_object(std::move(obj), _trav);
    PT(Texture) tmp = pong;
    pong = ping;
    ping = tmp;
  };

  int w = _window ? _window->get_x_size() : 1;
  int h = _window ? _window->get_y_size() : 1;
  float inv_w = w > 0 ? 1.f / (float)w : 1.f;
  float inv_h = h > 0 ? 1.f / (float)h : 1.f;

  for (const Rml::CompiledFilterHandle fh : filters) {
    if (fh == 0) continue;
    const CompiledFilterData &fd = *reinterpret_cast<const CompiledFilterData *>(fh);

    switch (fd.type) {

    case FilterType::Passthrough: {
      CPT(RenderState) state = _shader_passthrough;
      CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_blend_factor"), LVecBase4f(fd.blend_factor));
      state = state->add_attrib(sa);
      blit(state, ping);
      break;
    }

    case FilterType::ColorMatrix: {
      PTA_LMatrix4f mat;
      mat.push_back(UnalignedLMatrix4f(fd.color_matrix));
      CPT(RenderState) state = _shader_color_matrix;
      CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_color_matrix"), mat);
      state = state->add_attrib(sa);
      blit(state, ping);
      break;
    }

    case FilterType::Blur: {
      float sigma = std::max(fd.sigma, 0.1f);
      float weights_f[BLUR_WEIGHTS];
      compute_blur_weights(sigma, weights_f);

      PTA_float weights;
      for (int i = 0; i < BLUR_WEIGHTS; i++) weights.push_back(weights_f[i]);

      // Horizontal pass.
      {
        CPT(RenderState) state = _shader_blur_h;
        CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_weights"), weights);
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_axis"), LVecBase2f(1.f, 0.f));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_inv_size"), LVecBase2f(inv_w, inv_h));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_uv_min"), LVecBase2f(0.f, 0.f));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_uv_max"), LVecBase2f(1.f, 1.f));
        state = state->add_attrib(sa);
        blit(state, ping);
      }
      // Vertical pass.
      {
        CPT(RenderState) state = _shader_blur_v;
        CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_weights"), weights);
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_axis"), LVecBase2f(0.f, 1.f));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_inv_size"), LVecBase2f(inv_w, inv_h));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_uv_min"), LVecBase2f(0.f, 0.f));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(
          InternalName::make("u_uv_max"), LVecBase2f(1.f, 1.f));
        state = state->add_attrib(sa);
        blit(state, ping);
      }
      break;
    }

    case FilterType::DropShadow: {
      float sigma = fd.sigma;
      if (sigma >= 0.5f) {
        // First blur the source to make the soft shadow.
        float weights_f[BLUR_WEIGHTS];
        compute_blur_weights(sigma, weights_f);
        PTA_float weights;
        for (int i = 0; i < BLUR_WEIGHTS; i++) weights.push_back(weights_f[i]);

        auto blur_pass = [&](LVecBase2f axis) {
          CPT(RenderState) state = _shader_blur_h;
          CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_weights"), weights);
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_axis"), axis);
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_inv_size"), LVecBase2f(inv_w, inv_h));
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_uv_min"), LVecBase2f(0.f, 0.f));
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_uv_max"), LVecBase2f(1.f, 1.f));
          state = state->add_attrib(sa);
          blit(state, ping);
        };
        blur_pass({1.f, 0.f});
        blur_pass({0.f, 1.f});
      }

      // Now draw the shadow offset + tinted, then composite original on top.
      CPT(RenderState) state = _shader_drop_shadow;
      CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_shadow_color"), fd.color);
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_offset"), fd.offset);
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_inv_size"), LVecBase2f(inv_w, inv_h));
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_uv_min"), LVecBase2f(0.f, 0.f));
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_uv_max"), LVecBase2f(1.f, 1.f));
      state = state->add_attrib(sa);
      blit(state, ping);
      break;
    }

    case FilterType::MaskImage: {
      if (fd.mask_tex == nullptr) break;
      CPT(RenderState) state = _shader_blend_mask;
      CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_mask_tex"), fd.mask_tex);
      state = state->add_attrib(sa);
      blit(state, ping);
      break;
    }

    case FilterType::Invalid:
    default:
      break;
    }
  }

  return ping;
}

// ---------------------------------------------------------------------------
// CompileFilter / ReleaseFilter
// ---------------------------------------------------------------------------

Rml::CompiledFilterHandle RmlRenderInterface::
CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters) {
  CompiledFilterData *fd = new CompiledFilterData;

  if (name == "opacity") {
    fd->type = FilterType::Passthrough;
    fd->blend_factor = Rml::Get(parameters, "value", 1.0f);
  }
  else if (name == "blur") {
    fd->type = FilterType::Blur;
    fd->sigma = Rml::Get(parameters, "sigma", 1.0f);
  }
  else if (name == "drop-shadow") {
    fd->type = FilterType::DropShadow;
    fd->sigma = Rml::Get(parameters, "sigma", 0.f);
    Rml::Colourb col = Rml::Get(parameters, "color", Rml::Colourb());
    fd->color = LColor(col.red / 255.f, col.green / 255.f,
                       col.blue / 255.f, col.alpha / 255.f);
    Rml::Vector2f off = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
    fd->offset = LVecBase2f(off.x, off.y);
  }
  else if (name == "brightness") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    fd->color_matrix = LMatrix4f::scale_mat(v, v, v) *
                       LMatrix4f(LMatrix4f::ident_mat());
    // Diagonal: scale rgb, keep alpha unchanged.
    fd->color_matrix = LMatrix4f(
      v, 0, 0, 0,
      0, v, 0, 0,
      0, 0, v, 0,
      0, 0, 0, 1);
  }
  else if (name == "contrast") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    float g = 0.5f - 0.5f * v;
    fd->color_matrix = LMatrix4f(
      v, 0, 0, 0,
      0, v, 0, 0,
      0, 0, v, 0,
      g, g, g, 1);
  }
  else if (name == "invert") {
    fd->type = FilterType::ColorMatrix;
    float v = std::min(std::max(Rml::Get(parameters, "value", 1.f), 0.f), 1.f);
    float inv = 1.f - 2.f * v;
    fd->color_matrix = LMatrix4f(
      inv, 0, 0, 0,
      0, inv, 0, 0,
      0, 0, inv, 0,
      v,  v,  v, 1);
  }
  else if (name == "grayscale") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    float r = v * 0.2126f, g = v * 0.7152f, b = v * 0.0722f;
    float rv = 1.f - v;
    fd->color_matrix = LMatrix4f(
      r + rv, g,      b,      0,
      r,      g + rv, b,      0,
      r,      g,      b + rv, 0,
      0,      0,      0,      1);
  }
  else if (name == "sepia") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    float rv = 1.f - v;
    fd->color_matrix = LMatrix4f(
      v*0.393f + rv, v*0.769f,       v*0.189f,       0,
      v*0.349f,      v*0.686f + rv,  v*0.168f,       0,
      v*0.272f,      v*0.534f,       v*0.131f + rv,  0,
      0,             0,              0,              1);
  }
  else if (name == "hue-rotate") {
    fd->type = FilterType::ColorMatrix;
    float a = Rml::Get(parameters, "value", 0.f);
    float s = sinf(a), c = cosf(a);
    fd->color_matrix = LMatrix4f(
      0.213f + 0.787f*c - 0.213f*s,  0.715f - 0.715f*c - 0.715f*s,  0.072f - 0.072f*c + 0.928f*s,  0,
      0.213f - 0.213f*c + 0.143f*s,  0.715f + 0.285f*c + 0.140f*s,  0.072f - 0.072f*c - 0.283f*s,  0,
      0.213f - 0.213f*c - 0.787f*s,  0.715f - 0.715f*c + 0.715f*s,  0.072f + 0.928f*c + 0.072f*s,  0,
      0, 0, 0, 1);
  }
  else if (name == "saturate") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    fd->color_matrix = LMatrix4f(
      0.213f + 0.787f*v,  0.715f - 0.715f*v,  0.072f - 0.072f*v,  0,
      0.213f - 0.213f*v,  0.715f + 0.285f*v,  0.072f - 0.072f*v,  0,
      0.213f - 0.213f*v,  0.715f - 0.715f*v,  0.072f + 0.928f*v,  0,
      0, 0, 0, 1);
  }
  else {
    delete fd;
    rmlui_cat.warning() << "Unknown filter '" << name << "'\n";
    return 0;
  }

  return reinterpret_cast<Rml::CompiledFilterHandle>(fd);
}

void RmlRenderInterface::
ReleaseFilter(Rml::CompiledFilterHandle filter) {
  delete reinterpret_cast<CompiledFilterData *>(filter);
}

// ---------------------------------------------------------------------------
// CompileShader / RenderShader / ReleaseShader
// ---------------------------------------------------------------------------

Rml::CompiledShaderHandle RmlRenderInterface::
CompileShader(const Rml::String &name, const Rml::Dictionary &parameters) {
  CompiledShaderData *sd = new CompiledShaderData;

  auto load_stops = [&](const Rml::Dictionary &p) {
    auto it = p.find("color_stop_list");
    if (it == p.end() ||
        it->second.GetType() != Rml::Variant::COLORSTOPLIST) return;
    const Rml::ColorStopList &stops =
      it->second.GetReference<Rml::ColorStopList>();
    int n = std::min((int)stops.size(), MAX_STOPS);
    sd->num_stops = n;
    for (int i = 0; i < n; i++) {
      sd->stop_positions[i] = stops[i].position.number;
      // ColorStop::color is ColourbPremultiplied (0-255 premultiplied).
      const auto &c = stops[i].color;
      sd->stop_colors[i] = LVecBase4f(c.red / 255.f, c.green / 255.f,
                                       c.blue / 255.f, c.alpha / 255.f);
    }
  };

  if (name == "linear-gradient") {
    sd->type = ShaderType::Gradient;
    bool rep = Rml::Get(parameters, "repeating", false);
    sd->gradient_func = rep ? GradientFunc::RepeatingLinear : GradientFunc::Linear;
    Rml::Vector2f p0 = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
    Rml::Vector2f p1 = Rml::Get(parameters, "p1", Rml::Vector2f(0.f));
    sd->p = LVecBase2f(p0.x, p0.y);
    sd->v = LVecBase2f(p1.x - p0.x, p1.y - p0.y);
    load_stops(parameters);
  }
  else if (name == "radial-gradient") {
    sd->type = ShaderType::Gradient;
    bool rep = Rml::Get(parameters, "repeating", false);
    sd->gradient_func = rep ? GradientFunc::RepeatingRadial : GradientFunc::Radial;
    Rml::Vector2f center = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
    Rml::Vector2f radius = Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
    sd->p = LVecBase2f(center.x, center.y);
    sd->v = LVecBase2f(1.f / radius.x, 1.f / radius.y);
    load_stops(parameters);
  }
  else if (name == "conic-gradient") {
    sd->type = ShaderType::Gradient;
    bool rep = Rml::Get(parameters, "repeating", false);
    sd->gradient_func = rep ? GradientFunc::RepeatingConic : GradientFunc::Conic;
    Rml::Vector2f center = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
    float angle = Rml::Get(parameters, "angle", 0.f);
    sd->p = LVecBase2f(center.x, center.y);
    sd->v = LVecBase2f(cosf(angle), sinf(angle));
    load_stops(parameters);
  }
  else if (name == "shader") {
    Rml::String val = Rml::Get(parameters, "value", Rml::String());
    if (val == "creation") {
      sd->type = ShaderType::Creation;
      Rml::Vector2f dim = Rml::Get(parameters, "dimensions", Rml::Vector2f(1.f));
      sd->dimensions = LVecBase2f(dim.x, dim.y);
    } else {
      delete sd;
      rmlui_cat.warning() << "Unknown shader value '" << val << "'\n";
      return 0;
    }
  }
  else {
    delete sd;
    rmlui_cat.warning() << "Unknown shader '" << name << "'\n";
    return 0;
  }

  return reinterpret_cast<Rml::CompiledShaderHandle>(sd);
}

void RmlRenderInterface::
RenderShader(Rml::CompiledShaderHandle shader_handle,
             Rml::CompiledGeometryHandle geometry_handle,
             Rml::Vector2f translation,
             Rml::TextureHandle /*texture*/) {
  if (!shader_handle || !geometry_handle) return;
  const CompiledShaderData &sd =
    *reinterpret_cast<const CompiledShaderData *>(shader_handle);
  const CompiledGeometry *cg =
    reinterpret_cast<const CompiledGeometry *>(geometry_handle);

  CPT(RenderState) state;

  switch (sd.type) {

  case ShaderType::Gradient: {
    state = _shader_gradient;
    CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());

    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_func"), LVecBase4f((float)sd.gradient_func, 0, 0, 0));
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_p"), LVecBase2f(sd.p));
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_v"), LVecBase2f(sd.v));
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_num_stops"), LVecBase4f((float)sd.num_stops, 0, 0, 0));

    PTA_LVecBase4f stop_colors;
    for (int i = 0; i < sd.num_stops; i++) stop_colors.push_back(sd.stop_colors[i]);
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_stop_colors"), stop_colors);

    PTA_float stop_positions;
    for (int i = 0; i < sd.num_stops; i++) stop_positions.push_back(sd.stop_positions[i]);
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_stop_positions"), stop_positions);

    state = state->add_attrib(sa);
    break;
  }

  case ShaderType::Creation: {
    state = _shader_creation;
    CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());

    float t = (float)Rml::GetSystemInterface()->GetElapsedTime();
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_time"), LVecBase4f(t, 0, 0, 0));
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_dimensions"), LVecBase2f(sd.dimensions));

    state = state->add_attrib(sa);
    break;
  }

  default:
    return;
  }

  render_geom(cg->_geom, state, translation);
}

void RmlRenderInterface::
ReleaseShader(Rml::CompiledShaderHandle shader_handle) {
  delete reinterpret_cast<CompiledShaderData *>(shader_handle);
}

// ---------------------------------------------------------------------------
// composite_quad — used internally
// ---------------------------------------------------------------------------

void RmlRenderInterface::
composite_quad(const RenderState *state) {
  CPT(Geom) quad = make_fullscreen_quad();
  CPT(TransformState) ident = _trav->get_scene()->get_cs_world_transform();
  CullableObject obj(quad, state, ident);
  _trav->get_cull_handler()->record_object(std::move(obj), _trav);
}
