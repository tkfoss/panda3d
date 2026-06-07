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
#include "displayRegion.h"
#include "geom.h"
#include "graphicsOutput.h"
#include "graphicsStateGuardian.h"
#include "lmatrix.h"
#include "pvector.h"
#include "renderState.h"
#include "shader.h"
#include "texture.h"
#include "thread.h"
#include "transformState.h"

#ifndef CPPPARSER
#include <RmlUi/Core/RenderInterface.h>
#endif

/**
 * Implements Rml::RenderInterface, dispatching draw calls into Panda3D's cull
 * traverser with full layer / filter / shader support.
 *
 * Layer lifecycle (PushLayer / CompositeLayers / PopLayer):
 *   PushLayer   — calls buf->begin_frame(FM_render) to bind the layer FBO;
 *                 subsequent RenderGeometry calls go to that FBO via the GSG.
 *   CompositeLayers — calls end_frame on the source, then rebinds the
 *                 destination and submits a fullscreen quad with the
 *                 post-processed source texture.
 *   PopLayer    — ends the current layer frame and rebinds the parent.
 *
 * All filter/shader programs are compiled from embedded GLSL strings via
 * Shader::make(SL_GLSL).  glslang compiles them to SPIR-V; the GL backend
 * cross-compiles via SPIRV-Cross; the Vulkan backend uses SPIR-V natively.
 * No raw GL calls anywhere — the implementation is fully backend-agnostic.
 */
class RmlRenderInterface
#ifndef CPPPARSER
  : public Rml::RenderInterface
#endif
{
public:
  // Called once by RmlRegion after the window is created.
  void init(GraphicsOutput *window);

  // Called once per frame from RmlRegion::do_cull.
  void render(Rml::Context *context, CullTraverser *trav,
              GraphicsStateGuardian *gsg, Thread *current_thread);

  // -----------------------------------------------------------------------
  // Required Rml::RenderInterface
  // -----------------------------------------------------------------------
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

  // -----------------------------------------------------------------------
  // Optional Rml::RenderInterface — layers / filters / shaders
  // -----------------------------------------------------------------------
  Rml::LayerHandle PushLayer() override;
  void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                       Rml::BlendMode blend_mode,
                       Rml::Span<const Rml::CompiledFilterHandle> filters) override;
  void PopLayer() override;

  Rml::TextureHandle SaveLayerAsTexture() override;
  Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

  Rml::CompiledFilterHandle CompileFilter(const Rml::String &name,
                                          const Rml::Dictionary &parameters) override;
  void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

  Rml::CompiledShaderHandle CompileShader(const Rml::String &name,
                                          const Rml::Dictionary &parameters) override;
  void RenderShader(Rml::CompiledShaderHandle shader,
                    Rml::CompiledGeometryHandle geometry,
                    Rml::Vector2f translation,
                    Rml::TextureHandle texture) override;
  void ReleaseShader(Rml::CompiledShaderHandle shader) override;

public:
  // One pre-allocated RGBA offscreen buffer for a single UI layer.
  // Public so the file-scope make_layer_buffer helper can name the type.
  struct LayerBuffer {
    PT(GraphicsOutput) buf;
    PT(DisplayRegion)  dr;   // a plain DR on buf, used for clear/prepare
    PT(Texture)        tex;
    bool               frame_open = false;
    bool               in_use     = false;
  };

protected:
  struct CompiledGeometry {
    CPT(Geom) _geom;
  };

  // One entry on the active layer stack.
  struct LayerEntry {
    Rml::LayerHandle handle;  // 0 = main window; else (LayerBuffer*)+1
    Rml::Rectanglei  scissor; // scissor rect saved at push time
  };

  // -----------------------------------------------------------------------
  // Filter / shader data structs (mirror GL3 reference renderer)
  // -----------------------------------------------------------------------
  enum class FilterType { Invalid, Passthrough, Blur, DropShadow, ColorMatrix, MaskImage };
  struct CompiledFilterData {
    FilterType  type         = FilterType::Invalid;
    float       blend_factor = 1.0f; // opacity passthrough
    float       sigma        = 0.0f; // blur / drop-shadow
    LVecBase2f  offset;              // drop-shadow pixel offset
    LColor      color;               // drop-shadow tint
    LMatrix4f   color_matrix;        // color-matrix filters
    PT(Texture) mask_tex;            // mask-image
  };

  enum class ShaderType { Invalid, Gradient, Creation };
  enum class GradientFunc { Linear, Radial, Conic,
                            RepeatingLinear, RepeatingRadial, RepeatingConic };

  static constexpr int MAX_STOPS = 16;
  struct CompiledShaderData {
    ShaderType   type           = ShaderType::Invalid;
    GradientFunc gradient_func  = GradientFunc::Linear;
    LVecBase2f   p, v;
    int          num_stops      = 0;
    float        stop_positions[MAX_STOPS];
    LVecBase4f   stop_colors[MAX_STOPS];
    LVecBase2f   dimensions;    // creation shader
  };

  // -----------------------------------------------------------------------
  // Internal helpers
  // -----------------------------------------------------------------------
  PT(Geom) make_geom(Rml::Span<const Rml::Vertex> vertices,
                     Rml::Span<const int> indices,
                     GeomEnums::UsageHint uh);

  // Submit a geom+state to the active cull handler.
  void render_geom(const Geom *geom, const RenderState *state,
                   Rml::Vector2f translation);

  // Submit a fullscreen NDC quad to the active cull handler.
  void composite_quad(CPT(RenderState) state, PT(Texture) tex);

  // Compile filter shaders lazily on first use.
  void ensure_shaders();

  // Layer pool management.
  LayerBuffer *alloc_layer();
  void         free_layer(LayerBuffer *lb);
  LayerBuffer *get_layer(Rml::LayerHandle handle); // nullptr if handle==0

  // Bind a layer buffer's FBO as the current render target.
  // Clears to transparent black.  Asserts lb != nullptr.
  void begin_layer(LayerBuffer *lb);

  // End a layer's frame, flushing its texture.  Rebinds destination.
  // dest may be nullptr (= main window).
  void end_layer(LayerBuffer *lb, LayerBuffer *dest);

  // Apply filters in ping-pong between _scratch[0] and _scratch[1].
  // Returns the texture holding the final result.
  PT(Texture) apply_filters(PT(Texture) src,
                             Rml::Span<const Rml::CompiledFilterHandle> filters);

private:
#ifndef CPPPARSER
  Mutex _lock;

  // The parent window (set by init()).
  GraphicsOutput *_window = nullptr;

  // Layer buffer pool.  Pre-allocated in init(), expanded on demand.
  pvector<LayerBuffer *> _layer_pool;

  // Two scratch buffers for ping-pong compositing within apply_filters().
  LayerBuffer *_scratch[2] = { nullptr, nullptr };

  // Mask buffer (SaveLayerAsMaskImage).
  LayerBuffer *_mask_lb = nullptr;

  // Current layer stack.  Bottom entry is the base layer (handle=0).
  pvector<LayerEntry> _layer_stack;

  // Scissor state.
  bool            _scissor_active = false;
  Rml::Rectanglei _scissor_rect;

  // Per-frame state, filled by render() and cleared after Context::Render().
  CullTraverser          *_trav         = nullptr;
  GraphicsStateGuardian  *_gsg          = nullptr;
  Thread                 *_thread       = nullptr;
  CPT(TransformState)     _net_transform;
  CPT(RenderState)        _net_state;
  Rml::Vector2i           _dimensions;

  // Shader programs (compiled once on first use).
  bool _shaders_ready = false;

  CPT(RenderState) _shader_passthrough;
  CPT(RenderState) _shader_color_matrix;
  CPT(RenderState) _shader_blend_mask;
  CPT(RenderState) _shader_blur;
  CPT(RenderState) _shader_drop_shadow;
  CPT(RenderState) _shader_gradient;
  CPT(RenderState) _shader_creation;
#endif
};

#endif
