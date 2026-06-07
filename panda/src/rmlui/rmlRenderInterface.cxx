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
#include "displayRegion.h"
#include "frameBufferProperties.h"
#include "geomTriangles.h"
#include "geomVertexArrayData.h"
#include "geomVertexData.h"
#include "geomVertexWriter.h"
#include "internalName.h"
#include "pta_LMatrix4.h"
#include "pta_LVecBase4.h"
#include "pta_float.h"
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
#include <RmlUi/Core/Core.h>
#endif

// ===========================================================================
// Embedded GLSL shader sources
//
// All shaders target GLSL 330 with explicit layout(binding=N) so they compile
// correctly through glslang → SPIR-V and survive SPIRV-Cross on both the GL
// and Vulkan backends.  No raw GL calls anywhere.
// ===========================================================================

// ---------------------------------------------------------------------------
// Passthrough — blit one texture to the current FBO, optionally scaled
// ---------------------------------------------------------------------------
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
in  vec2 v_uv;
out vec4 o_color;
void main() {
    o_color = texture(p3d_Texture0, v_uv) * u_blend_factor;
}
)GLSL";

// ---------------------------------------------------------------------------
// Color matrix — brightness / contrast / grayscale / sepia / hue-rotate / etc.
// ---------------------------------------------------------------------------
static const char *s_frag_color_matrix = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
uniform mat4 u_color_matrix;
in  vec2 v_uv;
out vec4 o_color;
void main() {
    vec4 c = texture(p3d_Texture0, v_uv);
    o_color = vec4(vec3(u_color_matrix * c), c.a);
}
)GLSL";

// ---------------------------------------------------------------------------
// Blend mask — multiply source rgb by mask alpha  (CSS mask-image)
// ---------------------------------------------------------------------------
static const char *s_frag_blend_mask = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
layout(binding=1) uniform sampler2D u_mask_tex;
in  vec2 v_uv;
out vec4 o_color;
void main() {
    vec4 c  = texture(p3d_Texture0, v_uv);
    float m = texture(u_mask_tex, v_uv).a;
    o_color = c * m;
}
)GLSL";

// ---------------------------------------------------------------------------
// Drop shadow — blurred alpha tinted by shadow color, composited with original
// ---------------------------------------------------------------------------
static const char *s_frag_drop_shadow = R"GLSL(
#version 330
layout(binding=0) uniform sampler2D p3d_Texture0;
uniform vec4  u_shadow_color;
uniform vec2  u_offset;
uniform vec2  u_inv_size;
in  vec2 v_uv;
out vec4 o_color;
void main() {
    vec2 suv = v_uv + u_offset * u_inv_size;
    float a  = texture(p3d_Texture0, suv).a;
    o_color  = u_shadow_color * a + texture(p3d_Texture0, v_uv);
}
)GLSL";

// ---------------------------------------------------------------------------
// Separable Gaussian blur (single axis per pass, 7 taps)
// ---------------------------------------------------------------------------
#define BLUR_TAPS    7
#define BLUR_WEIGHTS 4   // ceil(BLUR_TAPS / 2)

static const char *s_vert_blur = R"GLSL(
#version 330
layout(location=0) in vec2 p3d_Vertex;
layout(location=4) in vec2 p3d_MultiTexCoord0;
uniform vec2  u_axis;
uniform vec2  u_inv_size;
out vec2 v_uv[7];
void main() {
    for (int i = 0; i < 7; i++) {
        float d = float(i - 3);
        v_uv[i] = p3d_MultiTexCoord0 + u_axis * d * u_inv_size;
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
in  vec2 v_uv[7];
out vec4 o_color;
void main() {
    o_color = vec4(0.0);
    for (int i = 0; i < 7; i++) {
        vec2 in_r = step(u_uv_min, v_uv[i]) * step(v_uv[i], u_uv_max);
        int  wi   = abs(i - 3);
        o_color  += texture(p3d_Texture0, v_uv[i])
                    * in_r.x * in_r.y * u_weights[wi];
    }
}
)GLSL";

// ---------------------------------------------------------------------------
// Gradient decorator (linear / radial / conic, repeating variants, 16 stops)
// ---------------------------------------------------------------------------
static const char *s_vert_ui = R"GLSL(
#version 330
layout(location=0) in vec2 p3d_Vertex;
layout(location=3) in vec4 p3d_Color;
layout(location=4) in vec2 p3d_MultiTexCoord0;
uniform vec2 u_translate;
uniform mat4 u_transform;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv    = p3d_MultiTexCoord0;
    v_color = p3d_Color;
    vec2 p  = p3d_Vertex + u_translate;
    gl_Position = u_transform * vec4(p, 0.0, 1.0);
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
in  vec2 v_uv;
in  vec4 v_color;
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

// ---------------------------------------------------------------------------
// Creation procedural shader (Danilo Guanabara / shadertoy)
// ---------------------------------------------------------------------------
static const char *s_frag_creation = R"GLSL(
#version 330
uniform float u_time;
uniform vec2  u_dimensions;
in  vec2 v_uv;
in  vec4 v_color;
out vec4 o_color;
void main() {
    float t = u_time;
    vec3 c;
    float l;
    for (int i = 0; i < 3; i++) {
        vec2 p  = v_uv;
        vec2 uv = p;
        p -= 0.5;
        p.x *= u_dimensions.x / u_dimensions.y;
        float z = t + float(i) * 0.07;
        l  = length(p);
        uv += p / l * (sin(z) + 1.0) * abs(sin(l * 9.0 - z - z));
        c[i] = 0.01 / length(mod(uv, 1.0) - 0.5);
    }
    o_color = vec4(c / l, v_color.a);
}
)GLSL";

// ===========================================================================
// Internal helpers
// ===========================================================================

static void compute_blur_weights(float sigma, float out[BLUR_WEIGHTS]) {
  double sum = 0.0;
  for (int i = 0; i < BLUR_WEIGHTS; ++i) {
    out[i] = (float)std::exp(-0.5 * (double)(i * i) / (sigma * sigma));
    sum += out[i] * (i == 0 ? 1.0 : 2.0);
  }
  for (int i = 0; i < BLUR_WEIGHTS; ++i) {
    out[i] = (float)(out[i] / sum);
  }
}

// Build a fullscreen NDC quad (positions -1..1, UVs 0..1).
static CPT(Geom) make_fullscreen_quad() {
  struct V { float x, y, u, v; };
  static const V verts[4] = {
    {-1.f,-1.f, 0.f, 0.f},
    { 1.f,-1.f, 1.f, 0.f},
    { 1.f, 1.f, 1.f, 1.f},
    {-1.f, 1.f, 0.f, 1.f},
  };
  static const int idx[6] = {0,1,2, 0,2,3};

  PT(GeomVertexData) vd = new GeomVertexData(
    "fsq", GeomVertexFormat::get_v3c4t2(), GeomEnums::UH_static);
  vd->unclean_set_num_rows(4);
  GeomVertexWriter vw(vd, InternalName::get_vertex());
  GeomVertexWriter cw(vd, InternalName::get_color());
  GeomVertexWriter tw(vd, InternalName::get_texcoord());
  for (const auto &v : verts) {
    vw.add_data3f(v.x, v.y, 0.f);
    cw.add_data4i(255, 255, 255, 255);
    tw.add_data2f(v.u, v.v);
  }
  PT(GeomTriangles) tris = new GeomTriangles(GeomEnums::UH_static);
  PT(GeomVertexArrayData) idata = tris->modify_vertices();
  idata->unclean_set_num_rows(6);
  GeomVertexWriter iw(idata, 0);
  for (int i : idx) iw.add_data1i(i);

  PT(Geom) g = new Geom(vd);
  g->add_primitive(tris);
  return g;
}

// Create a single RGBA GraphicsBuffer sharing the GSG of parent_window.
static RmlRenderInterface::LayerBuffer *
make_layer_buffer(GraphicsOutput *parent_window, const std::string &name) {
  FrameBufferProperties fbp;
  fbp.set_rgb_color(1);
  fbp.set_rgba_bits(8, 8, 8, 8);
  fbp.set_depth_bits(0);

  int w = parent_window->get_x_size();
  int h = parent_window->get_y_size();

  PT(Texture) tex = new Texture(name);
  tex->setup_2d_texture(w, h, Texture::T_unsigned_byte, Texture::F_rgba);
  tex->set_wrap_u(SamplerState::WM_clamp);
  tex->set_wrap_v(SamplerState::WM_clamp);
  tex->set_minfilter(SamplerState::FT_linear);
  tex->set_magfilter(SamplerState::FT_linear);

  PT(GraphicsOutput) buf = parent_window->make_texture_buffer(
    name, w, h, tex, false, &fbp);
  if (buf == nullptr) {
    rmlui_cat.error()
      << "Failed to allocate RmlUi layer buffer '" << name << "'\n";
    return nullptr;
  }
  // Disable automatic engine rendering; we drive these manually.
  buf->set_active(false);

  PT(DisplayRegion) dr = buf->make_display_region();
  dr->set_clear_color_active(true);
  dr->set_clear_color(LColor(0, 0, 0, 0));

  auto *lb = new RmlRenderInterface::LayerBuffer;
  lb->buf   = buf;
  lb->dr    = dr;
  lb->tex   = tex;
  lb->in_use     = false;
  lb->frame_open = false;
  return lb;
}

// ===========================================================================
// init() — allocate layer pool
// ===========================================================================

void RmlRenderInterface::
init(GraphicsOutput *window) {
  nassertv(window != nullptr);
  _window = window;

  // Pre-allocate 4 layer buffers and 2 scratch buffers.
  for (int i = 0; i < 4; ++i) {
    LayerBuffer *lb = make_layer_buffer(window,
      std::string("rmlui-layer-") + std::to_string(i));
    if (lb) _layer_pool.push_back(lb);
  }
  for (int i = 0; i < 2; ++i) {
    _scratch[i] = make_layer_buffer(window,
      std::string("rmlui-scratch-") + std::to_string(i));
    if (_scratch[i]) _scratch[i]->in_use = true; // never returned to pool
  }
  _mask_lb = make_layer_buffer(window, "rmlui-mask");
  if (_mask_lb) _mask_lb->in_use = true;
}

// ===========================================================================
// Lazy shader compilation
// ===========================================================================

void RmlRenderInterface::
ensure_shaders() {
  if (_shaders_ready) return;
  _shaders_ready = true;

  CPT(RenderState) base = RenderState::make(
    CullBinAttrib::make("unsorted", 0),
    DepthTestAttrib::make(RenderAttrib::M_none),
    DepthWriteAttrib::make(DepthWriteAttrib::M_off),
    ColorAttrib::make_vertex()
  );

  auto blend_over = [&]() {
    return base->add_attrib(ColorBlendAttrib::make(
      ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_incoming_alpha,
      ColorBlendAttrib::O_one_minus_incoming_alpha));
  };
  auto blend_replace = [&]() {
    return base->add_attrib(ColorBlendAttrib::make(
      ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_one,
      ColorBlendAttrib::O_zero));
  };

  auto make_state = [&](const char *vert, const char *frag,
                        CPT(RenderState) blend) -> CPT(RenderState) {
    PT(Shader) sh = Shader::make(Shader::SL_GLSL, vert, frag);
    return blend->add_attrib(ShaderAttrib::make(sh));
  };

  _shader_passthrough  = make_state(s_vert_passthrough, s_frag_passthrough,  blend_over());
  _shader_color_matrix = make_state(s_vert_passthrough, s_frag_color_matrix, blend_replace());
  _shader_blend_mask   = make_state(s_vert_passthrough, s_frag_blend_mask,   blend_replace());
  _shader_drop_shadow  = make_state(s_vert_passthrough, s_frag_drop_shadow,  blend_replace());
  _shader_blur         = make_state(s_vert_blur,        s_frag_blur,         blend_replace());
  _shader_gradient     = make_state(s_vert_ui,          s_frag_gradient,     blend_over());
  _shader_creation     = make_state(s_vert_ui,          s_frag_creation,     blend_over());
}

// ===========================================================================
// Layer pool management
// ===========================================================================

RmlRenderInterface::LayerBuffer *RmlRenderInterface::
alloc_layer() {
  for (LayerBuffer *lb : _layer_pool) {
    if (!lb->in_use) {
      lb->in_use = true;
      return lb;
    }
  }
  // Pool exhausted — grow on demand (rare: deep nesting or many filters).
  LayerBuffer *lb = make_layer_buffer(
    _window, "rmlui-layer-dyn-" + std::to_string(_layer_pool.size()));
  if (lb) {
    lb->in_use = true;
    _layer_pool.push_back(lb);
  }
  return lb;
}

void RmlRenderInterface::
free_layer(LayerBuffer *lb) {
  nassertv(lb != nullptr);
  lb->in_use     = false;
  lb->frame_open = false;
}

RmlRenderInterface::LayerBuffer *RmlRenderInterface::
get_layer(Rml::LayerHandle handle) {
  if (handle == 0) return nullptr;
  return reinterpret_cast<LayerBuffer *>(handle - 1);
}

// ===========================================================================
// Frame management: begin / end a layer buffer
// ===========================================================================

void RmlRenderInterface::
begin_layer(LayerBuffer *lb) {
  nassertv(lb != nullptr && _gsg != nullptr && _thread != nullptr);
  if (lb->frame_open) return;

  if (!lb->buf->begin_frame(GraphicsOutput::FM_render, _thread)) {
    rmlui_cat.error() << "begin_frame failed on layer buffer\n";
    return;
  }
  lb->frame_open = true;

  // Clear to transparent black.
  DisplayRegionPipelineReader dr_reader(lb->dr, _thread);
  _gsg->prepare_display_region(&dr_reader);
  _gsg->clear(lb->dr);
}

void RmlRenderInterface::
end_layer(LayerBuffer *lb, LayerBuffer *dest) {
  // End the source layer's frame (flushes FBO → texture).
  if (lb != nullptr && lb->frame_open) {
    lb->buf->end_frame(GraphicsOutput::FM_render, _thread);
    lb->frame_open = false;
  }

  // Rebind the destination FBO so subsequent draw calls go there.
  if (dest != nullptr) {
    if (!dest->frame_open) {
      dest->buf->begin_frame(GraphicsOutput::FM_render, _thread);
      dest->frame_open = true;
    }
    DisplayRegionPipelineReader dr_reader(dest->dr, _thread);
    _gsg->prepare_display_region(&dr_reader);
  } else {
    // Rebind the main window.  FM_parasite just re-activates the context.
    _window->begin_frame(GraphicsOutput::FM_parasite, _thread);
    // prepare_display_region is not strictly required here; the engine's
    // change_scenes already ran before our do_cull was called.
  }
}

// ===========================================================================
// render() — drive one frame
// ===========================================================================

void RmlRenderInterface::
render(Rml::Context *context, CullTraverser *trav,
       GraphicsStateGuardian *gsg, Thread *current_thread) {
  nassertv(context != nullptr);
  MutexHolder holder(_lock);
  ensure_shaders();

  _trav   = trav;
  _gsg    = gsg;
  _thread = current_thread;

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

  _layer_stack.clear();
  LayerEntry base;
  base.handle  = 0;
  base.scissor = Rml::Rectanglei::FromSize({_dimensions.x, _dimensions.y});
  _layer_stack.push_back(base);

  context->Render();

  // Sanity: any unclosed layers are an RmlUi bug, but clean up regardless.
  while (_layer_stack.size() > 1) {
    LayerEntry top = _layer_stack.back();
    _layer_stack.pop_back();
    if (top.handle != 0) {
      LayerBuffer *lb = get_layer(top.handle);
      if (lb && lb->frame_open) {
        lb->buf->end_frame(GraphicsOutput::FM_render, _thread);
        lb->frame_open = false;
      }
      free_layer(lb);
    }
  }
  _layer_stack.clear();

  _trav   = nullptr;
  _gsg    = nullptr;
  _thread = nullptr;
  _net_transform = nullptr;
  _net_state     = nullptr;
}

// ===========================================================================
// Internal geometry helpers
// ===========================================================================

PT(Geom) RmlRenderInterface::
make_geom(Rml::Span<const Rml::Vertex> vertices,
          Rml::Span<const int> indices,
          GeomEnums::UsageHint uh) {

  PT(GeomVertexData) vd = new GeomVertexData(
    "", GeomVertexFormat::get_v3c4t2(), uh);
  vd->unclean_set_num_rows((int)vertices.size());

  GeomVertexWriter vw(vd, InternalName::get_vertex());
  GeomVertexWriter cw(vd, InternalName::get_color());
  GeomVertexWriter tw(vd, InternalName::get_texcoord());
  for (const Rml::Vertex &v : vertices) {
    vw.add_data3f(LVector3f::right() * v.position.x +
                  LVector3f::up()    * v.position.y);
    cw.add_data4i(v.colour.red, v.colour.green,
                  v.colour.blue, v.colour.alpha);
    tw.add_data2f(v.tex_coord.x, 1.0f - v.tex_coord.y);
  }

  PT(GeomTriangles) tris = new GeomTriangles(uh);
  PT(GeomVertexArrayData) idata = tris->modify_vertices();
  idata->unclean_set_num_rows((int)indices.size());
  GeomVertexWriter iw(idata, 0);
  for (int i : indices) iw.add_data1i(i);

  PT(Geom) g = new Geom(vd);
  g->add_primitive(tris);
  return g;
}

void RmlRenderInterface::
render_geom(const Geom *geom, const RenderState *state, Rml::Vector2f translation) {
  LVector3 off = LVector3::right() * translation.x + LVector3::up() * translation.y;

  CPT(RenderState) full = _net_state->compose(state);
  if (_scissor_active && _dimensions.x > 0 && _dimensions.y > 0) {
    const auto &r = _scissor_rect;
    LVecBase4 sc(
      r.Left()   / (PN_stdfloat)_dimensions.x,
      r.Right()  / (PN_stdfloat)_dimensions.x,
      1.f - r.Bottom() / (PN_stdfloat)_dimensions.y,
      1.f - r.Top()    / (PN_stdfloat)_dimensions.y);
    full = full->add_attrib(ScissorAttrib::make(sc));
  }

  CPT(TransformState) xform =
    _trav->get_scene()->get_cs_world_transform()->compose(
      _net_transform->compose(TransformState::make_pos(off)));

  CullableObject obj(geom, full, xform);
  _trav->get_cull_handler()->record_object(std::move(obj), _trav);
}

void RmlRenderInterface::
composite_quad(CPT(RenderState) state, PT(Texture) tex) {
  // Wire the texture into the ShaderAttrib of the given state.
  CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
  if (sa != nullptr && tex != nullptr) {
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("p3d_Texture0"), tex);
    state = state->add_attrib(sa);
  }
  CPT(Geom) quad = make_fullscreen_quad();
  CPT(TransformState) ident = _trav->get_scene()->get_cs_world_transform();
  CullableObject obj(quad, state, ident);
  _trav->get_cull_handler()->record_object(std::move(obj), _trav);
}

// ===========================================================================
// Required interface — CompileGeometry / RenderGeometry / ReleaseGeometry
// ===========================================================================

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
  if (!cg) return;

  CPT(RenderState) state;
  Texture *tex = (Texture *)texture;
  if (tex) {
    PT(TextureStage) ts = new TextureStage("");
    ts->set_mode(TextureStage::M_modulate);
    CPT(TextureAttrib) ta = DCAST(TextureAttrib, TextureAttrib::make());
    ta = DCAST(TextureAttrib, ta->add_on_stage(ts, tex));
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

// ===========================================================================
// Required interface — LoadTexture / GenerateTexture / ReleaseTexture
// ===========================================================================

Rml::TextureHandle RmlRenderInterface::
LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source) {
  LoaderOptions opts;
  opts.set_auto_texture_scale(
    Texture::get_textures_power_2() == ATS_none ? ATS_none : ATS_pad);

  PT(Texture) tex = TexturePool::load_texture(
    Filename::from_os_specific(source), 0, false, opts);
  if (!tex) { texture_dimensions = {0,0}; return 0; }

  tex->set_minfilter(SamplerState::FT_nearest);
  tex->set_magfilter(SamplerState::FT_nearest);

  int w = tex->get_orig_file_x_size();
  int h = tex->get_orig_file_y_size();
  if (!w && !h) { w = tex->get_x_size(); h = tex->get_y_size(); }
  texture_dimensions = {w, h};

  tex->ref();
  return (Rml::TextureHandle)tex.p();
}

Rml::TextureHandle RmlRenderInterface::
GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dims) {
  PT(Texture) tex = new Texture;
  tex->setup_2d_texture(dims.x, dims.y, Texture::T_unsigned_byte, Texture::F_rgba);
  tex->set_size_padded(dims.x, dims.y);

  PTA_uchar img = tex->modify_ram_image();
  size_t src_stride = dims.x * 4;
  size_t dst_stride = tex->get_x_size() * 4;
  const unsigned char *src = source.data() + src_stride * dims.y;
  unsigned char *dst = &img[0];
  for (; src > source.data(); dst += dst_stride) {
    src -= src_stride;
    for (size_t i = 0; i < src_stride; i += 4) {
      dst[i+0] = src[i+2]; dst[i+1] = src[i+1];
      dst[i+2] = src[i+0]; dst[i+3] = src[i+3];
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
  if (tex) unref_delete(tex);
}

// ===========================================================================
// Required interface — EnableScissorRegion / SetScissorRegion
// ===========================================================================

void RmlRenderInterface::
EnableScissorRegion(bool enable) {
  _scissor_active = enable;
}

void RmlRenderInterface::
SetScissorRegion(Rml::Rectanglei region) {
  _scissor_rect = region;
}

// ===========================================================================
// Layer interface — PushLayer / CompositeLayers / PopLayer
// ===========================================================================

Rml::LayerHandle RmlRenderInterface::
PushLayer() {
  LayerBuffer *lb = alloc_layer();
  if (!lb) return 0;

  // Resize the layer buffer to match the window if needed.
  if (_window) {
    int w = _window->get_x_size();
    int h = _window->get_y_size();
    if (lb->tex->get_x_size() != w || lb->tex->get_y_size() != h) {
      lb->buf->set_size_and_recalc(w, h);
    }
  }

  // Bind the layer's FBO and clear to transparent black.
  begin_layer(lb);

  Rml::LayerHandle handle = reinterpret_cast<Rml::LayerHandle>(lb) + 1;

  LayerEntry entry;
  entry.handle  = handle;
  entry.scissor = _scissor_rect;
  _layer_stack.push_back(entry);

  return handle;
}

void RmlRenderInterface::
CompositeLayers(Rml::LayerHandle source_handle,
                Rml::LayerHandle destination_handle,
                Rml::BlendMode blend_mode,
                Rml::Span<const Rml::CompiledFilterHandle> filters) {

  LayerBuffer *src_lb  = get_layer(source_handle);
  LayerBuffer *dest_lb = get_layer(destination_handle);

  if (src_lb == nullptr || !src_lb->frame_open) return;

  // End the source layer's frame → texture is now ready.
  // Rebind destination so compositing goes to the right FBO.
  end_layer(src_lb, dest_lb);

  // Apply filter chain in ping-pong.
  PT(Texture) result = apply_filters(src_lb->tex, filters);

  // Build compositing state.
  CPT(RenderState) state = _shader_passthrough;
  CPT(RenderAttrib) sa = state->get_attrib(ShaderAttrib::get_class_slot());
  sa = DCAST(ShaderAttrib, sa)->set_shader_input(
    InternalName::make("u_blend_factor"), LVecBase4f(1.f));
  state = state->add_attrib(sa);

  if (blend_mode == Rml::BlendMode::Replace) {
    state = state->add_attrib(ColorBlendAttrib::make(
      ColorBlendAttrib::M_add,
      ColorBlendAttrib::O_one, ColorBlendAttrib::O_zero));
  }

  composite_quad(state, result);
}

void RmlRenderInterface::
PopLayer() {
  if (_layer_stack.size() <= 1) return;
  LayerEntry top = _layer_stack.back();
  _layer_stack.pop_back();

  // Restore scissor to the state before PushLayer.
  _scissor_rect = top.scissor;

  if (top.handle == 0) return;
  LayerBuffer *lb = get_layer(top.handle);
  if (!lb) return;

  // End this layer's frame if still open (shouldn't happen — CompositeLayers
  // should have already ended it, but guard against partial rendering).
  if (lb->frame_open) {
    LayerBuffer *parent = (_layer_stack.empty() || _layer_stack.back().handle == 0)
                          ? nullptr
                          : get_layer(_layer_stack.back().handle);
    end_layer(lb, parent);
  }
  free_layer(lb);
}

// ===========================================================================
// SaveLayerAsTexture / SaveLayerAsMaskImage
// ===========================================================================

Rml::TextureHandle RmlRenderInterface::
SaveLayerAsTexture() {
  if (_layer_stack.empty()) return 0;
  const LayerEntry &top = _layer_stack.back();
  if (top.handle == 0) return 0;

  LayerBuffer *lb = get_layer(top.handle);
  if (!lb || !lb->tex) return 0;

  // Snapshot the current layer: end frame to flush the texture, allocate a
  // new layer for subsequent rendering, start its frame.
  LayerBuffer *snap_lb = alloc_layer();
  if (!snap_lb) return 0;

  if (lb->frame_open) {
    // End lb → snap_lb; we'll restart lb's frame for future rendering.
    end_layer(lb, snap_lb);
    begin_layer(lb);
  }

  snap_lb->tex->ref();
  return (Rml::TextureHandle)snap_lb->tex.p();
}

Rml::CompiledFilterHandle RmlRenderInterface::
SaveLayerAsMaskImage() {
  if (_layer_stack.empty()) return 0;
  const LayerEntry &top = _layer_stack.back();
  if (top.handle == 0) return 0;

  LayerBuffer *lb = get_layer(top.handle);
  if (!lb || !lb->tex) return 0;

  // Flush the current layer to its texture.
  if (lb->frame_open) {
    LayerBuffer *parent = (_layer_stack.size() >= 2 &&
                           _layer_stack[_layer_stack.size()-2].handle != 0)
      ? get_layer(_layer_stack[_layer_stack.size()-2].handle) : nullptr;
    end_layer(lb, parent);
    begin_layer(lb); // restart for future rendering
  }

  CompiledFilterData *fd = new CompiledFilterData;
  fd->type     = FilterType::MaskImage;
  fd->mask_tex = lb->tex;
  return reinterpret_cast<Rml::CompiledFilterHandle>(fd);
}

// ===========================================================================
// apply_filters — ping-pong filter chain
// ===========================================================================

PT(Texture) RmlRenderInterface::
apply_filters(PT(Texture) src,
              Rml::Span<const Rml::CompiledFilterHandle> filters) {
  if (filters.empty()) return src;
  ensure_shaders();

  int w = _window ? _window->get_x_size() : 1;
  int h = _window ? _window->get_y_size() : 1;
  float inv_w = w > 0 ? 1.f / (float)w : 1.f;
  float inv_h = h > 0 ? 1.f / (float)h : 1.f;

  PT(Texture) ping = src;
  int scratch_idx = 0; // alternates between _scratch[0] and _scratch[1]

  // Helper: blit ping through a shader state into the next scratch buffer.
  // Updates ping to be the scratch buffer's texture.
  auto blit = [&](CPT(RenderState) state) {
    LayerBuffer *dest = _scratch[scratch_idx ^ 1];
    if (dest) {
      begin_layer(dest);
      composite_quad(state, ping);
      end_layer(dest, nullptr);
      ping = dest->tex;
    }
    scratch_idx ^= 1;
  };

  for (const Rml::CompiledFilterHandle fh : filters) {
    if (!fh) continue;
    const CompiledFilterData &fd = *reinterpret_cast<const CompiledFilterData *>(fh);

    switch (fd.type) {

    case FilterType::Passthrough: {
      CPT(RenderState) st = _shader_passthrough;
      CPT(RenderAttrib) sa = st->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_blend_factor"), LVecBase4f(fd.blend_factor));
      blit(st->add_attrib(sa));
      break;
    }

    case FilterType::ColorMatrix: {
      CPT(RenderState) st = _shader_color_matrix;
      PTA_LMatrix4f mat;
      mat.push_back(UnalignedLMatrix4f(fd.color_matrix));
      CPT(RenderAttrib) sa = st->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(
        InternalName::make("u_color_matrix"), mat);
      blit(st->add_attrib(sa));
      break;
    }

    case FilterType::Blur: {
      float sigma = std::max(fd.sigma, 0.1f);
      float wf[BLUR_WEIGHTS];
      compute_blur_weights(sigma, wf);
      PTA_float weights;
      for (int i = 0; i < BLUR_WEIGHTS; ++i) weights.push_back(wf[i]);

      auto blur_pass = [&](LVecBase2f axis) {
        CPT(RenderState) st = _shader_blur;
        CPT(RenderAttrib) sa = st->get_attrib(ShaderAttrib::get_class_slot());
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_weights"),  weights);
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_axis"),     axis);
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_inv_size"), LVecBase2f(inv_w, inv_h));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_uv_min"),   LVecBase2f(0.f, 0.f));
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_uv_max"),   LVecBase2f(1.f, 1.f));
        blit(st->add_attrib(sa));
      };
      blur_pass({1.f, 0.f});
      blur_pass({0.f, 1.f});
      break;
    }

    case FilterType::DropShadow: {
      // Blur pass (if sigma >= 0.5).
      if (fd.sigma >= 0.5f) {
        float wf2[BLUR_WEIGHTS];
        compute_blur_weights(fd.sigma, wf2);
        PTA_float weights;
        for (int i = 0; i < BLUR_WEIGHTS; ++i) weights.push_back(wf2[i]);
        auto blur_pass2 = [&](LVecBase2f axis) {
          CPT(RenderState) st = _shader_blur;
          CPT(RenderAttrib) sa = st->get_attrib(ShaderAttrib::get_class_slot());
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_weights"),  weights);
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_axis"),     axis);
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_inv_size"), LVecBase2f(inv_w, inv_h));
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_uv_min"),   LVecBase2f(0.f, 0.f));
          sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_uv_max"),   LVecBase2f(1.f, 1.f));
          blit(st->add_attrib(sa));
        };
        blur_pass2({1.f, 0.f});
        blur_pass2({0.f, 1.f});
      }
      // Draw shadow tinted + offset, then composite original on top.
      {
        CPT(RenderState) st = _shader_drop_shadow;
        CPT(RenderAttrib) sa = st->get_attrib(ShaderAttrib::get_class_slot());
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_shadow_color"), fd.color);
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_offset"),       fd.offset);
        sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_inv_size"),     LVecBase2f(inv_w, inv_h));
        blit(st->add_attrib(sa));
      }
      break;
    }

    case FilterType::MaskImage: {
      if (!fd.mask_tex) break;
      CPT(RenderState) st = _shader_blend_mask;
      CPT(RenderAttrib) sa = st->get_attrib(ShaderAttrib::get_class_slot());
      sa = DCAST(ShaderAttrib, sa)->set_shader_input(InternalName::make("u_mask_tex"), fd.mask_tex);
      blit(st->add_attrib(sa));
      break;
    }

    case FilterType::Invalid:
    default: break;
    }
  }
  return ping;
}

// ===========================================================================
// CompileFilter / ReleaseFilter
// ===========================================================================

Rml::CompiledFilterHandle RmlRenderInterface::
CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters) {
  CompiledFilterData *fd = new CompiledFilterData;

  if (name == "opacity") {
    fd->type = FilterType::Passthrough;
    fd->blend_factor = Rml::Get(parameters, "value", 1.f);
  }
  else if (name == "blur") {
    fd->type  = FilterType::Blur;
    fd->sigma = Rml::Get(parameters, "sigma", 1.f);
  }
  else if (name == "drop-shadow") {
    fd->type  = FilterType::DropShadow;
    fd->sigma = Rml::Get(parameters, "sigma", 0.f);
    Rml::Colourb c = Rml::Get(parameters, "color", Rml::Colourb());
    fd->color = LColor(c.red/255.f, c.green/255.f, c.blue/255.f, c.alpha/255.f);
    Rml::Vector2f off = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
    fd->offset = LVecBase2f(off.x, off.y);
  }
  else if (name == "brightness") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    fd->color_matrix = LMatrix4f(v,0,0,0, 0,v,0,0, 0,0,v,0, 0,0,0,1);
  }
  else if (name == "contrast") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    float g = 0.5f - 0.5f*v;
    fd->color_matrix = LMatrix4f(v,0,0,0, 0,v,0,0, 0,0,v,0, g,g,g,1);
  }
  else if (name == "invert") {
    fd->type = FilterType::ColorMatrix;
    float v = std::min(std::max(Rml::Get(parameters, "value", 1.f), 0.f), 1.f);
    float i = 1.f - 2.f*v;
    fd->color_matrix = LMatrix4f(i,0,0,0, 0,i,0,0, 0,0,i,0, v,v,v,1);
  }
  else if (name == "grayscale") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f), rv = 1.f - v;
    float r = v*0.2126f, g = v*0.7152f, b = v*0.0722f;
    fd->color_matrix = LMatrix4f(
      r+rv,g,   b,   0,
      r,   g+rv,b,   0,
      r,   g,   b+rv,0,
      0,   0,   0,   1);
  }
  else if (name == "sepia") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f), rv = 1.f - v;
    fd->color_matrix = LMatrix4f(
      v*0.393f+rv, v*0.769f,       v*0.189f,       0,
      v*0.349f,    v*0.686f+rv,    v*0.168f,       0,
      v*0.272f,    v*0.534f,       v*0.131f+rv,    0,
      0,           0,              0,              1);
  }
  else if (name == "hue-rotate") {
    fd->type = FilterType::ColorMatrix;
    float a = Rml::Get(parameters, "value", 0.f);
    float s = sinf(a), c = cosf(a);
    fd->color_matrix = LMatrix4f(
      0.213f+0.787f*c-0.213f*s, 0.715f-0.715f*c-0.715f*s, 0.072f-0.072f*c+0.928f*s, 0,
      0.213f-0.213f*c+0.143f*s, 0.715f+0.285f*c+0.140f*s, 0.072f-0.072f*c-0.283f*s, 0,
      0.213f-0.213f*c-0.787f*s, 0.715f-0.715f*c+0.715f*s, 0.072f+0.928f*c+0.072f*s, 0,
      0,                         0,                         0,                         1);
  }
  else if (name == "saturate") {
    fd->type = FilterType::ColorMatrix;
    float v = Rml::Get(parameters, "value", 1.f);
    fd->color_matrix = LMatrix4f(
      0.213f+0.787f*v, 0.715f-0.715f*v, 0.072f-0.072f*v, 0,
      0.213f-0.213f*v, 0.715f+0.285f*v, 0.072f-0.072f*v, 0,
      0.213f-0.213f*v, 0.715f-0.715f*v, 0.072f+0.928f*v, 0,
      0,               0,               0,               1);
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

// ===========================================================================
// CompileShader / RenderShader / ReleaseShader
// ===========================================================================

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
    for (int i = 0; i < n; ++i) {
      sd->stop_positions[i] = stops[i].position.number;
      const auto &c = stops[i].color;
      sd->stop_colors[i] = LVecBase4f(
        c.red/255.f, c.green/255.f, c.blue/255.f, c.alpha/255.f);
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
    Rml::Vector2f c  = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
    Rml::Vector2f r  = Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
    sd->p = LVecBase2f(c.x, c.y);
    sd->v = LVecBase2f(1.f/r.x, 1.f/r.y);
    load_stops(parameters);
  }
  else if (name == "conic-gradient") {
    sd->type = ShaderType::Gradient;
    bool rep = Rml::Get(parameters, "repeating", false);
    sd->gradient_func = rep ? GradientFunc::RepeatingConic : GradientFunc::Conic;
    Rml::Vector2f c = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
    float angle     = Rml::Get(parameters, "angle", 0.f);
    sd->p = LVecBase2f(c.x, c.y);
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
  ensure_shaders();

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
      InternalName::make("u_p"), sd.p);
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_v"), sd.v);
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_num_stops"), LVecBase4f((float)sd.num_stops, 0, 0, 0));
    PTA_LVecBase4f colors;
    PTA_float positions;
    for (int i = 0; i < sd.num_stops; ++i) {
      colors.push_back(sd.stop_colors[i]);
      positions.push_back(sd.stop_positions[i]);
    }
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_stop_colors"), colors);
    sa = DCAST(ShaderAttrib, sa)->set_shader_input(
      InternalName::make("u_stop_positions"), positions);
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
      InternalName::make("u_dimensions"), sd.dimensions);
    state = state->add_attrib(sa);
    break;
  }
  default: return;
  }

  render_geom(cg->_geom, state, translation);
}

void RmlRenderInterface::
ReleaseShader(Rml::CompiledShaderHandle shader_handle) {
  delete reinterpret_cast<CompiledShaderData *>(shader_handle);
}
