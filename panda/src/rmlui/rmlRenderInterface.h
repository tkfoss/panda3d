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
#include "lmatrix.h"
#include "pvector.h"
#include "renderState.h"
#include "shader.h"
#include "texture.h"
#include "transformState.h"

#ifndef CPPPARSER
#include <RmlUi/Core/RenderInterface.h>
#endif

class GraphicsOutput;

/**
 * Implements Rml::RenderInterface, dispatching draw calls into Panda3D's cull
 * traverser.  Supports the full optional layer/filter/shader API, enabling
 * CSS filter:, backdrop-filter:, mask-image:, blurred box-shadow:, and
 * linear/radial/conic-gradient decorators.
 *
 * Layer buffers are pre-allocated at init() time and pooled to avoid
 * per-frame allocation.  Filter and shader programs are compiled once from
 * embedded GLSL and stored as Panda Shader objects, making the implementation
 * backend-agnostic (GL and Vulkan both consume SPIR-V after glslang).
 */
class RmlRenderInterface
#ifndef CPPPARSER
  : public Rml::RenderInterface
#endif
{
public:
  void init(GraphicsOutput *window);
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

  // Optional layer / filter / shader interface
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

protected:
  struct CompiledGeometry {
    CPT(Geom) _geom;
  };

  // A layer buffer: one pre-allocated RGBA texture + backing GraphicsOutput.
  // When in use, it receives geometry via its own CullTraverser.
  struct LayerBuffer {
    PT(GraphicsOutput) buf;
    PT(Texture)        tex;
    bool               in_use = false;
  };

  // A layer entry on the stack.  handle=0 → main window.
  struct LayerEntry {
    Rml::LayerHandle handle;  // non-zero index into _layer_pool
    Rml::Rectanglei  scissor; // scissor at push time (restored on pop)
  };

  // -----------------------------------------------------------------------
  // Compiled filter / shader data structs (mirrors GL3 reference renderer)
  // -----------------------------------------------------------------------

  enum class FilterType { Invalid, Passthrough, Blur, DropShadow, ColorMatrix, MaskImage };
  struct CompiledFilterData {
    FilterType type = FilterType::Invalid;
    float      blend_factor = 1.0f; // Passthrough / opacity
    float      sigma        = 0.0f; // Blur / DropShadow
    LVecBase2f offset;              // DropShadow
    LColor     color;               // DropShadow
    LMatrix4f  color_matrix;        // ColorMatrix
    PT(Texture) mask_tex;           // MaskImage
  };

  enum class ShaderType { Invalid, Gradient, Creation };
  enum class GradientFunc { Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic };

  static constexpr int MAX_STOPS = 16;
  struct CompiledShaderData {
    ShaderType   type = ShaderType::Invalid;
    // Gradient
    GradientFunc gradient_func = GradientFunc::Linear;
    LVecBase2f   p, v;
    int          num_stops = 0;
    float        stop_positions[MAX_STOPS];
    LVecBase4f   stop_colors[MAX_STOPS];
    // Creation
    LVecBase2f   dimensions;
  };

  // -----------------------------------------------------------------------

  PT(Geom) make_geom(Rml::Span<const Rml::Vertex> vertices,
                     Rml::Span<const int> indices,
                     GeomEnums::UsageHint uh);
  void render_geom(const Geom *geom, const RenderState *state,
                   Rml::Vector2f translation);

  // Submit a fullscreen quad to the cull handler using the given render state.
  void composite_quad(const RenderState *state);

  // Lazy-init shader programs (called at first use).
  void ensure_shaders();

  // Obtain a free layer buffer from the pool (or create one).
  LayerBuffer *alloc_layer();
  // Return a layer buffer to the pool.
  void free_layer(LayerBuffer *lb);

  // Resolve the LayerHandle → LayerBuffer *.  handle=0 → nullptr (main window).
  LayerBuffer *get_layer(Rml::LayerHandle handle);

  // Apply a sequence of compiled filters in postprocess ping-pong.
  // Returns the texture currently holding the filtered result.
  PT(Texture) apply_filters(PT(Texture) src,
                             Rml::Span<const Rml::CompiledFilterHandle> filters);

private:
#ifndef CPPPARSER
  Mutex _lock;

  // Window/buffer we composite into (set by init()).
  GraphicsOutput *_window = nullptr;

  // Pre-allocated layer buffer pool.
  pvector<LayerBuffer *> _layer_pool;

  // Active layer stack.  Bottom entry is the base layer (handle=0).
  pvector<LayerEntry> _layer_stack;

  // Additional "ping-pong" scratch textures for multi-pass filter chains.
  PT(Texture) _scratch[2];
  PT(GraphicsOutput) _scratch_buf[2];

  // Blend-mask scratch texture (SaveLayerAsMaskImage).
  PT(Texture) _mask_tex;
  PT(GraphicsOutput) _mask_buf;

  bool _scissor_active = false;
  Rml::Rectanglei _scissor_rect;

  // Filled in by render() for the duration of one frame.
  CullTraverser *_trav = nullptr;
  CPT(TransformState) _net_transform;
  CPT(RenderState) _net_state;
  Rml::Vector2i _dimensions;

  // Cached shader programs (compiled once on first use).
  bool _shaders_ready = false;

  CPT(RenderState) _shader_passthrough;    // blit with optional opacity
  CPT(RenderState) _shader_color_matrix;   // color-matrix filter
  CPT(RenderState) _shader_blend_mask;     // mask-image blend
  CPT(RenderState) _shader_blur_h;         // horizontal Gaussian blur pass
  CPT(RenderState) _shader_blur_v;         // vertical Gaussian blur pass
  CPT(RenderState) _shader_drop_shadow;    // drop-shadow composite
  CPT(RenderState) _shader_gradient;       // linear/radial/conic gradient decorator
  CPT(RenderState) _shader_creation;       // shader("creation") decorator
#endif
};

#endif
