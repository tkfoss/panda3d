/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file vulkanGraphicsBuffer.cxx
 * @author rdb
 * @date 2016-04-13
 */

#include "vulkanGraphicsBuffer.h"
#include "vulkanGraphicsStateGuardian.h"

TypeHandle VulkanGraphicsBuffer::_type_handle;

/**
 *
 */
VulkanGraphicsBuffer::
VulkanGraphicsBuffer(GraphicsEngine *engine, GraphicsPipe *pipe,
                     std::string name,
                     const FrameBufferProperties &fb_prop,
                     const WindowProperties &win_prop,
                     int flags,
                     GraphicsStateGuardian *gsg,
                     GraphicsOutput *host) :
  GraphicsBuffer(engine, pipe, std::move(name), fb_prop, win_prop, flags, gsg, host) {
}

/**
 *
 */
VulkanGraphicsBuffer::
~VulkanGraphicsBuffer() {
}

/**
 * Returns true if this particular GraphicsOutput can render directly into a
 * texture, or false if it must always copy-to-texture at the end of each
 * frame to achieve this effect.
 */
bool VulkanGraphicsBuffer::
get_supports_render_texture() const {
  return true;
}

/**
 * Clears the entire framebuffer before rendering, according to the settings
 * of get_color_clear_active() and get_depth_clear_active() (inherited from
 * DrawableRegion).
 *
 * This function is called only within the draw thread.
 */
void VulkanGraphicsBuffer::
clear(Thread *current_thread) {
  // We do the clear in begin_frame(), and the validation layers don't like it
  // if an extra clear is being done at the beginning of a frame.  That's why
  // this is empty for now.  Need a cleaner solution for this.
}

/**
 * This function will be called within the draw thread before beginning
 * rendering for a given frame.  It should do whatever setup is required, and
 * return true if the frame should be rendered, or false if it should be
 * skipped.
 */
bool VulkanGraphicsBuffer::
begin_frame(FrameMode mode, Thread *current_thread) {
  begin_frame_spam(mode);
  if (_gsg == nullptr) {
    return false;
  }

  if (mode != FM_render) {
    return true;
  }

  VulkanGraphicsStateGuardian *vkgsg;
  DCAST_INTO_R(vkgsg, _gsg, false);

  if (vkgsg->needs_reset()) {
    vkQueueWaitIdle(vkgsg->_queue);
    destroy_framebuffer();
    vkgsg->reset_if_new();
  }


  if (!vkgsg->is_valid()) {
    return false;
  }

  vkgsg->_fb_color_tc = nullptr;
  vkgsg->_fb_depth_tc = nullptr;

  if (_creation_flags & GraphicsPipe::BF_size_track_host) {
    if (_host != nullptr) {
      _size = _host->get_size();
    }
  }

  {
    CDReader cdata(_cycler);
    if (cdata->_textures_seq != _last_textures_seq ||
        _framebuffer._size != _size) {
      // The buffer was resized or the attachments were changed.
      destroy_framebuffer();
      if (!create_framebuffer(cdata)) {
        return false;
      }
    }
  }

  // Instruct the GSG that we are commencing a new frame.  This will cause it
  // to create a command buffer.
  vkgsg->set_current_properties(&get_fb_properties());
  if (!vkgsg->begin_frame(current_thread)) {
    return false;
  }

  copy_async_screenshot();

  /*if (mode == FM_render) {
    clear_cube_map_selection();
  }*/

  // Now that we have a command buffer, start our render pass.  A cumulative
  // buffer preserves its textures' previous contents when clears are inactive.
  bool cumulative = (_creation_flags & GraphicsPipe::BF_rtt_cumulative) != 0;
  return _framebuffer.begin_rendering(vkgsg, this, false, cumulative);
}

/**
 * This function will be called within the draw thread after rendering is
 * completed for a given frame.  It should do whatever finalization is
 * required.
 */
void VulkanGraphicsBuffer::
end_frame(FrameMode mode, Thread *current_thread) {
  end_frame_spam(mode);

  if (mode == FM_render) {
    VulkanGraphicsStateGuardian *vkgsg;
    DCAST_INTO_V(vkgsg, _gsg);

    _framebuffer.end_rendering(vkgsg);

    // Now we can do copy-to-texture, now that the render pass has ended.
    copy_to_textures();

    // Note: this will close the command buffer.
    _gsg->end_frame(current_thread);

    trigger_flip();
    clear_cube_map_selection();
  }
}

/**
 * Closes the buffer right now.  Called from the window thread.
 */
void VulkanGraphicsBuffer::
close_buffer() {
  if (!_gsg.is_null()) {
    VulkanGraphicsStateGuardian *vkgsg;
    DCAST_INTO_V(vkgsg, _gsg);

    destroy_framebuffer();

    _gsg.clear();
  }
}

/**
 * Opens the buffer right now.  Called from the window thread.  Returns true
 * if the buffer is successfully opened, or false if there was a problem.
 */
bool VulkanGraphicsBuffer::
open_buffer() {
  VulkanGraphicsPipe *vkpipe;
  DCAST_INTO_R(vkpipe, _pipe, false);

  // Make sure we have a GSG, which manages a VkDevice.
  VulkanGraphicsStateGuardian *vkgsg;
  uint32_t queue_family_index = 0;
  if (_gsg == nullptr) {
    // Find a queue suitable for graphics.
    if (!vkpipe->find_queue_family(queue_family_index, VK_QUEUE_GRAPHICS_BIT)) {
      vulkandisplay_cat.error()
        << "Failed to find graphics queue for buffer.\n";
      return false;
    }

    // There is no old gsg.  Create a new one.
    vkgsg = new VulkanGraphicsStateGuardian(_engine, vkpipe, nullptr, queue_family_index);
    _gsg = vkgsg;
  } else {
    DCAST_INTO_R(vkgsg, _gsg.p(), false);
  }

  vkgsg->reset_if_new();
  if (!vkgsg->is_valid()) {
    _gsg.clear();
    vulkandisplay_cat.error()
      << "VulkanGraphicsStateGuardian is not valid.\n";
    return false;
  }

  _framebuffer._config_id = vkgsg->choose_fb_config(_framebuffer._config, _fb_properties);

  if (_fb_properties.get_stencil_bits() > 0) {
    _depth_stencil_plane = RTP_depth_stencil;
  } else {
    _depth_stencil_plane = RTP_depth;
  }

  if (_creation_flags & GraphicsPipe::BF_size_track_host) {
    GraphicsOutput *host = _host;
    nassertr(host != nullptr, false);
    _size = host->get_size();
  }

  return true;
}

/**
 * Destroys an existing swapchain.  Before calling this, make sure that no
 * commands are executing on any queue that uses this swapchain.
 */
void VulkanGraphicsBuffer::
destroy_framebuffer() {
  VulkanGraphicsStateGuardian *vkgsg;
  DCAST_INTO_V(vkgsg, _gsg);

  vkgsg->flush();

  _framebuffer.destroy(vkgsg);

  _is_valid = false;
}

/**
 * Creates or recreates the framebuffer.
 */
bool VulkanGraphicsBuffer::
create_framebuffer(CDReader &cdata) {
  VulkanGraphicsPipe *vkpipe;
  VulkanGraphicsStateGuardian *vkgsg;
  DCAST_INTO_R(vkpipe, _pipe, false);
  DCAST_INTO_R(vkgsg, _gsg, false);

  PT(Texture) color_texture;
  PT(Texture) depth_texture;
  for (const RenderTexture &rt : cdata->_textures) {
    if (rt._rtm_mode == RTM_bind_or_copy || rt._rtm_mode == RTM_bind_layered) {
      if (rt._plane == RTP_color) {
        color_texture = rt._texture;
      }
      if (rt._plane == RTP_depth || rt._plane == RTP_depth_stencil) {
        depth_texture = rt._texture;
      }
    }
  }

  if (!_framebuffer._config._color_formats.empty()) {
    if (!create_attachment(RTP_color, _framebuffer._config._color_formats[0], color_texture)) {
      return false;
    }
  }

  // Create a color attachment for each auxiliary bitplane (MRT / G-buffer
  // targets).  These map to _color_formats[1..] in the same order that
  // choose_fb_config() appended them: aux_rgba, then aux_hrgba, then aux_float.
  // Each may have a bound render-texture in cdata->_textures keyed by its plane.
  {
    size_t color_index = 1;
    auto add_aux_planes = [&](RenderTexturePlane first_plane, int count) -> bool {
      for (int i = 0; i < count; ++i) {
        if (color_index >= _framebuffer._config._color_formats.size()) {
          return true;
        }
        RenderTexturePlane plane = (RenderTexturePlane)((int)first_plane + i);
        Texture *aux_texture = nullptr;
        for (const RenderTexture &rt : cdata->_textures) {
          if ((rt._rtm_mode == RTM_bind_or_copy || rt._rtm_mode == RTM_bind_layered) &&
              rt._plane == plane) {
            aux_texture = rt._texture;
            break;
          }
        }
        if (!create_attachment(plane, _framebuffer._config._color_formats[color_index], aux_texture)) {
          return false;
        }
        ++color_index;
      }
      return true;
    };
    if (!add_aux_planes(RTP_aux_rgba_0, _fb_properties.get_aux_rgba())) return false;
    if (!add_aux_planes(RTP_aux_hrgba_0, _fb_properties.get_aux_hrgba())) return false;
    if (!add_aux_planes(RTP_aux_float_0, _fb_properties.get_aux_float())) return false;
  }

  if (_framebuffer._config._depth_format != VK_FORMAT_UNDEFINED) {
    if (!create_attachment(_depth_stencil_plane, _framebuffer._config._depth_format, depth_texture)) {
      return false;
    }
  }

  _framebuffer._size = _size;
  _is_valid = true;
  _last_textures_seq = cdata->_textures_seq;
  return true;
}

/**
 * Adds a new attachment to the framebuffer.
 * @return Returns true on success.
 */
bool VulkanGraphicsBuffer::
create_attachment(RenderTexturePlane plane, VkFormat format, Texture *texture) {
  if (format == VK_FORMAT_UNDEFINED) {
    return true;
  }

  VulkanGraphicsPipe *vkpipe;
  DCAST_INTO_R(vkpipe, _pipe, false);

  VulkanGraphicsStateGuardian *vkgsg;
  DCAST_INTO_R(vkgsg, _gsg, false);

  VkExtent3D extent;
  extent.width = _size[0];
  extent.height = _size[1];
  extent.depth = 1;

  bool is_depth_stencil = (plane == RTP_depth || plane == RTP_stencil ||
                           plane == RTP_depth_stencil);
  VkImageAspectFlags aspect_mask;
  VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  std::string debug_name;
  if (is_depth_stencil) {
    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    // Note: a combined depth-stencil attachment uses only the depth aspect
    // for now; the stencil aspect bit is not added.
    aspect_mask = (plane == RTP_stencil) ? VK_IMAGE_ASPECT_STENCIL_BIT
                                         : VK_IMAGE_ASPECT_DEPTH_BIT;
    debug_name = get_name() + ":depth-stencil";
  } else {
    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    debug_name = get_name() + ":color";
  }

  // Obtain the single-sampled image.  This is the render-to-texture target if
  // one was given, otherwise an offscreen image that we can copy from.  When
  // multisampling, this serves as the resolve target; otherwise we render into
  // it directly.
  VulkanTextureContext *tc;
  if (texture != nullptr) {
    Texture::Format tex_format;
    Texture::ComponentType tex_component_type;
    nassertr_always(vkgsg->lookup_image_format(format, tex_format, tex_component_type), false);

    texture->set_format(tex_format);
    texture->set_component_type(tex_component_type);
    texture->set_render_to_texture(true);

    // Size the bound texture to this framebuffer's exact extent BEFORE the
    // needs_recreation() check.  This backend supports non-power-of-two
    // textures, so a render target is sized exactly to the framebuffer (no
    // power-of-two padding) -- otherwise add_render_texture()'s padding leaves a
    // non-zero pad whose get_tex_scale() makes a full-[0,1]-UV consumer (e.g.
    // FilterManager) sample only a sub-rectangle.  set_auto_texture_scale(none)
    // pins this per texture, so it does not depend on the global textures-power-2
    // policy.  Use set_size_padded() so the pad size is reset to zero in step
    // with the new size.
    //
    // Forcing the size here is also required on a resize: the RenderTarget calls
    // only GraphicsBuffer::set_size(), not the texture's, so needs_recreation()
    // (which compares the texture size to tc->_extent) would otherwise see the
    // old size, skip release()+create_texture(), and leave the cached image view
    // addressing the previous VkImage.
    texture->set_auto_texture_scale(ATS_none);
    texture->set_size_padded(extent.width, extent.height, texture->get_z_size());

    DCAST_INTO_R(tc, texture->prepare_now(vkgsg->get_prepared_objects(), vkgsg), false);

    if (tc->needs_recreation()) {
      if (vkgsg->_frame_data != nullptr) {
        tc->release(*vkgsg->_frame_data);
      } else {
        tc->release(*vkgsg->_last_frame_data);
      }
      if (!vkgsg->create_texture(tc)) {
        return false;
      }
      tc->mark_loaded();
    }
  }
  else {
    tc = vkgsg->create_attachment(format, extent, aspect_mask,
                                  VK_SAMPLE_COUNT_1_BIT, usage, debug_name);
    if (tc == nullptr) {
      return false;
    }
  }

  // If multisampling, render into a separate multisample image and resolve the
  // result into the single-sampled image above at the end of the rendering
  // pass, so that it can be sampled or copied as usual.
  VkSampleCountFlagBits samples = _framebuffer._config._sample_count;
  if (samples != VK_SAMPLE_COUNT_1_BIT) {
    VkImageUsageFlags ms_usage = is_depth_stencil
      ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
      : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VulkanTextureContext *ms_tc =
      vkgsg->create_attachment(format, extent, aspect_mask, samples, ms_usage,
                               debug_name + ":ms");
    if (ms_tc == nullptr) {
      return false;
    }
    // The owned multisample image is the render target; the single-sampled
    // image (the bound texture or our owned image) is the resolve target.
    _framebuffer.add_attachment(ms_tc, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                nullptr, tc, texture);
  } else {
    _framebuffer.add_attachment(tc, VK_ATTACHMENT_STORE_OP_STORE, texture);
  }
  return true;
}
