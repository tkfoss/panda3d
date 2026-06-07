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
#include "cullableObject.h"
#include "cullHandler.h"
#include "geomVertexData.h"
#include "geomVertexArrayData.h"
#include "geomVertexWriter.h"
#include "geomTriangles.h"
#include "internalName.h"
#include "colorAttrib.h"
#include "colorBlendAttrib.h"
#include "cullBinAttrib.h"
#include "depthTestAttrib.h"
#include "depthWriteAttrib.h"
#include "scissorAttrib.h"
#include "texture.h"
#include "textureAttrib.h"
#include "texturePool.h"
#include "textureStage.h"

/**
 * Called from RmlRegion::do_cull.  Invokes context->Update()/Render() and
 * submits the resulting geometry to the cull handler.
 */
void RmlRenderInterface::
render(Rml::Context *context, CullTraverser *trav) {
  nassertv(context != nullptr);
  MutexHolder holder(_lock);

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

  context->Render();

  _trav = nullptr;
  _net_transform = nullptr;
  _net_state = nullptr;
}

/**
 * Builds a Panda Geom from RmlUi vertex/index spans.
 */
PT(Geom) RmlRenderInterface::
make_geom(Rml::Span<const Rml::Vertex> vertices,
          Rml::Span<const int> indices,
          GeomEnums::UsageHint uh) {

  PT(GeomVertexData) vdata = new GeomVertexData(
    "", GeomVertexFormat::get_v3c4t2(), uh);
  vdata->unclean_set_num_rows((int) vertices.size());

  {
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
  }

  PT(GeomTriangles) tris = new GeomTriangles(uh);
  {
    PT(GeomVertexArrayData) idata = tris->modify_vertices();
    idata->unclean_set_num_rows((int) indices.size());
    GeomVertexWriter iw(idata, 0);
    for (int idx : indices) {
      iw.add_data1i(idx);
    }
  }

  PT(Geom) geom = new Geom(vdata);
  geom->add_primitive(tris);
  return geom;
}

/**
 * Submit a geom+state to the cull handler.  Only valid during render().
 */
void RmlRenderInterface::
render_geom(const Geom *geom, const RenderState *state, Rml::Vector2f translation) {
  LVector3 offset =
    LVector3::right() * translation.x +
    LVector3::up()    * translation.y;

  CPT(RenderState) full_state = _net_state->compose(state);
  if (_enable_scissor) {
    full_state = full_state->add_attrib(ScissorAttrib::make(_scissor));
  }

  CPT(TransformState) xform =
    _trav->get_scene()->get_cs_world_transform()->compose(
      _net_transform->compose(TransformState::make_pos(offset)));

  CullableObject obj(geom, full_state, xform);
  _trav->get_cull_handler()->record_object(std::move(obj), _trav);
}

// ---------------------------------------------------------------------------
// Required RenderInterface implementation
// ---------------------------------------------------------------------------

Rml::CompiledGeometryHandle RmlRenderInterface::
CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                Rml::Span<const int> indices) {
  CompiledGeometry *cg = new CompiledGeometry;
  cg->_geom = make_geom(vertices, indices, GeomEnums::UH_static);
  return (Rml::CompiledGeometryHandle) cg;
}

void RmlRenderInterface::
RenderGeometry(Rml::CompiledGeometryHandle geometry,
               Rml::Vector2f translation,
               Rml::TextureHandle texture) {
  CompiledGeometry *cg = (CompiledGeometry *) geometry;
  if (cg == nullptr) return;

  CPT(RenderState) state;
  Texture *tex = (Texture *) texture;
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
  delete (CompiledGeometry *) geometry;
}

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
  return (Rml::TextureHandle) tex.p();
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
  return (Rml::TextureHandle) tex.p();
}

void RmlRenderInterface::
ReleaseTexture(Rml::TextureHandle texture) {
  Texture *tex = (Texture *) texture;
  if (tex != nullptr) {
    unref_delete(tex);
  }
}

void RmlRenderInterface::
EnableScissorRegion(bool enable) {
  _enable_scissor = enable;
}

void RmlRenderInterface::
SetScissorRegion(Rml::Rectanglei region) {
  if (_dimensions.x > 0 && _dimensions.y > 0) {
    _scissor[0] = region.Left()   / (PN_stdfloat) _dimensions.x;
    _scissor[1] = region.Right()  / (PN_stdfloat) _dimensions.x;
    _scissor[2] = 1.0f - (region.Bottom() / (PN_stdfloat) _dimensions.y);
    _scissor[3] = 1.0f - (region.Top()    / (PN_stdfloat) _dimensions.y);
  }
}
