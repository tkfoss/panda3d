/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlRenderInterface.h
 * @author rdb
 * @date 2011-11-04
 */

#ifndef RML_RENDER_INTERFACE_H
#define RML_RENDER_INTERFACE_H

#include "config_rmlui.h"
#include "cullTraverser.h"
#include "geom.h"
#include "renderState.h"
#include "transformState.h"

#ifndef CPPPARSER
#include <RmlUi/Core/RenderInterface.h>
#endif

/**
 * Implements Rml::RenderInterface, dispatching draw calls into Panda3D's cull
 * traverser.  Only the required subset of the interface is implemented; the
 * optional advanced-rendering methods (layers, clip masks, filters) are left
 * as no-ops for now and can be added incrementally.
 */
class RmlRenderInterface
#ifndef CPPPARSER
  : public Rml::RenderInterface
#endif
{
public:
  void render(Rml::Context *context, CullTraverser *trav);

  // Required interface
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

  Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
                                 const Rml::String &source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                     Rml::Vector2i source_dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture) override;

  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;

protected:
  struct CompiledGeometry {
    CPT(Geom) _geom;
  };

  PT(Geom) make_geom(Rml::Span<const Rml::Vertex> vertices,
                     Rml::Span<const int> indices,
                     GeomEnums::UsageHint uh);
  void render_geom(const Geom *geom, const RenderState *state,
                   Rml::Vector2f translation);

private:
  Mutex _lock;

  bool _enable_scissor = false;
  LVecBase4 _scissor;

  // Filled in temporarily by render()
  CullTraverser *_trav = nullptr;
  CPT(TransformState) _net_transform;
  CPT(RenderState) _net_state;
  Rml::Vector2i _dimensions;
};

#endif
