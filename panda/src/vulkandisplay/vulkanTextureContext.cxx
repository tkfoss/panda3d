/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file vulkanTextureContext.cxx
 * @author rdb
 * @date 2016-02-19
 */

#include "vulkanTextureContext.h"
#include "vulkanCommandBuffer.h"
#include "vulkanFrameData.h"

TypeHandle VulkanTextureContext::_type_handle;

/**
 * Returns true if the texture needs to be recreated because of a change to the
 * size or format.
 */
bool VulkanTextureContext::
needs_recreation() const {
  Texture *tex = get_texture();
  if (tex->get_render_to_texture() && !_supports_render_to_texture) {
    return true;
  }

  int num_views = tex->get_num_views();

  VkExtent3D extent;
  extent.width = tex->get_x_size();
  extent.height = tex->get_y_size();
  uint32_t arrayLayers;

  if (tex->get_texture_type() == Texture::TT_3d_texture) {
    extent.depth = tex->get_z_size();
    arrayLayers = 1;
  } else if (tex->get_texture_type() == Texture::TT_1d_texture_array) {
    extent.height = 1;
    extent.depth = 1;
    arrayLayers = tex->get_y_size();
  } else {
    extent.depth = 1;
    arrayLayers = tex->get_z_size();
  }
  arrayLayers *= num_views;

  //VkFormat format = get_image_format(tex);

  return (//format != _format ||
          extent.width != _extent.width ||
          extent.height != _extent.height ||
          extent.depth != _extent.depth ||
          arrayLayers != _array_layers ||
          (size_t)num_views != _image_views.size());
}

/**
 * Returns an image view addressing a single mip level (and optionally a single
 * array layer) of this image, for binding as a storage image (image2D etc).
 * Created on demand and cached, keyed by (view, mip, layer).
 *
 * mip is the mip level to address (clamped into range; <0 treated as 0).
 * layer >= 0 selects a single array layer / cube face / z-slice; layer < 0
 * keeps all layers (a layered view).
 *
 * Needed because a shader image input bound with a specific mip/layer must
 * imageStore into exactly that subresource; get_image_view() returns the
 * whole-image view (baseMipLevel 0, all levels), which would send every store
 * to mip 0 and leave the other levels unwritten.
 */
VkImageView VulkanTextureContext::
get_storage_image_view(VkDevice device, int view, int mip, int layer) {
  if (_image == VK_NULL_HANDLE) {
    return VK_NULL_HANDLE;
  }

  mip = std::min(std::max(mip, 0), (int)_mip_levels - 1);

  // Each "view" of a multiview texture occupies its own contiguous block of
  // array layers (see create_texture); fold the view offset into the layer.
  uint32_t layers_per_view = (view >= 0 && _array_layers > 0)
    ? _array_layers / std::max(1u, (uint32_t)get_texture()->get_num_views())
    : _array_layers;
  if (layers_per_view == 0) {
    layers_per_view = _array_layers;
  }
  uint32_t base_layer = (uint32_t)std::max(view, 0) * layers_per_view;

  bool single_layer = (layer >= 0);
  if (single_layer) {
    base_layer += (uint32_t)layer;
  }

  uint64_t key = ((uint64_t)(uint32_t)std::max(view, 0) << 40)
               | ((uint64_t)(uint32_t)mip << 20)
               | (uint64_t)(uint32_t)(layer + 1); // +1 so layer == -1 is distinct from 0
  auto it = _storage_image_views.find(key);
  if (it != _storage_image_views.end()) {
    return it->second;
  }

  VkImageViewCreateInfo view_info;
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.pNext = nullptr;
  view_info.flags = 0;
  view_info.image = _image;

  // A storage image is addressed as a non-arrayed 2D/3D/1D image when a single
  // layer is selected; otherwise as the layered type matching the texture.
  switch (get_texture()->get_texture_type()) {
  case Texture::TT_1d_texture:
    view_info.viewType = VK_IMAGE_VIEW_TYPE_1D;
    break;
  case Texture::TT_3d_texture:
    view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
    break;
  case Texture::TT_2d_texture_array:
  case Texture::TT_cube_map:
  case Texture::TT_cube_map_array:
  case Texture::TT_1d_texture_array:
    view_info.viewType = single_layer ? VK_IMAGE_VIEW_TYPE_2D
                                      : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    break;
  default:
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    break;
  }

  view_info.format = _format;
  view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_info.subresourceRange.aspectMask = _aspect_mask;
  view_info.subresourceRange.baseMipLevel = (uint32_t)mip;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = base_layer;
  view_info.subresourceRange.layerCount =
    single_layer ? 1 : (layers_per_view > 0 ? layers_per_view : VK_REMAINING_ARRAY_LAYERS);

  VkImageView image_view = VK_NULL_HANDLE;
  VkResult err = vkCreateImageView(device, &view_info, nullptr, &image_view);
  if (err) {
    vulkan_error(err, "Failed to create storage image view");
    return VK_NULL_HANDLE;
  }

  _storage_image_views[key] = image_view;
  return image_view;
}

/**
 * Schedules the deletion of the image resources for the end of the frame.
 */
void VulkanTextureContext::
release(VulkanFrameData &frame_data) {
  if (_image != VK_NULL_HANDLE) {
    frame_data._pending_destroy_images.push_back(_image);

    if (vulkandisplay_cat.is_debug()) {
      std::ostream &out = vulkandisplay_cat.debug()
        << "Scheduling image " << _image;

      if (!_image_views.empty()) {
        out << " with views";
        for (VkImageView image_view : _image_views) {
          out << " " << image_view;
        }
      }

      out << " for deletion\n";
    }

    _image = VK_NULL_HANDLE;
  }

  if (!_image_views.empty()) {
    frame_data._pending_destroy_image_views.insert(
      frame_data._pending_destroy_image_views.end(),
      _image_views.begin(), _image_views.end());

    _image_views.clear();
  }

  for (const auto &item : _storage_image_views) {
    frame_data._pending_destroy_image_views.push_back(item.second);
  }
  _storage_image_views.clear();

  if (_buffer != VK_NULL_HANDLE) {
    frame_data._pending_destroy_buffers.push_back(_buffer);

    if (vulkandisplay_cat.is_debug()) {
      std::ostream &out = vulkandisplay_cat.debug()
        << "Scheduling buffer " << _buffer;

      if (!_buffer_views.empty()) {
        out << " with views";
        for (VkBufferView buffer_view : _buffer_views) {
          out << " " << buffer_view;
        }
      }

      out << " for deletion\n";
    }

    _buffer = VK_NULL_HANDLE;
  }

  if (!_buffer_views.empty()) {
    frame_data._pending_destroy_buffer_views.insert(
      frame_data._pending_destroy_buffer_views.end(),
      _buffer_views.begin(), _buffer_views.end());

    _buffer_views.clear();
  }

  // Make sure that the memory remains untouched until the frame is over.
  frame_data._pending_free.push_back(std::move(_block));

  // The memory isn't free yet, but it can be reclaimed by the memory allocator
  // if really necessary by waiting until the frame queue is empty.
  update_data_size_bytes(0);

  _format = VK_FORMAT_UNDEFINED;
  _layout = VK_IMAGE_LAYOUT_UNDEFINED;

  // Reset the cross-frame barrier-tracking state too: it described the image we
  // just released.  Leaving _write_seq/_read_seq/stage+access masks pointing at
  // the destroyed image's history makes the next add_barrier() compute its
  // srcStageMask from stale masks, and a stale _hoisted_barrier_exists=true can
  // mis-modify a barrier index belonging to a prior command buffer.  (oldLayout
  // is UNDEFINED above so it stays spec-legal, but this keeps it clean.)
  _read_seq = 0;
  _write_seq = 0;
  _hoisted_barrier_exists = false;
  _write_access_mask = 0;
  _write_stage_mask = 0;
  _read_stage_mask = 0;
}

/**
 * Destroys the handles associated with this context immediately.
 */
void VulkanTextureContext::
destroy_now(VkDevice device) {
  for (VkImageView image_view : _image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }
  _image_views.clear();

  for (const auto &item : _storage_image_views) {
    vkDestroyImageView(device, item.second, nullptr);
  }
  _storage_image_views.clear();

  if (_image != VK_NULL_HANDLE) {
    vkDestroyImage(device, _image, nullptr);
    _image = VK_NULL_HANDLE;
  }

  for (VkBufferView buffer_view : _buffer_views) {
    vkDestroyBufferView(device, buffer_view, nullptr);
  }
  _buffer_views.clear();

  if (_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, _buffer, nullptr);
    _buffer = VK_NULL_HANDLE;
  }

  update_data_size_bytes(0);

  _format = VK_FORMAT_UNDEFINED;
  _layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

/**
 * Inserts commands to clear the image.
 */
void VulkanTextureContext::
clear_color_image(VulkanCommandBuffer &cmd, const VkClearColorValue &value) {
  nassertv(_aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT);
  nassertv(_image != VK_NULL_HANDLE);

  // We're not interested in whatever was in here before.
  discard();

  cmd.add_barrier(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  cmd.flush_barriers();

  VkImageSubresourceRange range;
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = _mip_levels;
  range.baseArrayLayer = 0;
  range.layerCount = _array_layers;
  vkCmdClearColorImage(cmd, _image, _layout, &value, 1, &range);
}

/**
 * Inserts commands to clear the image.
 */
void VulkanTextureContext::
clear_depth_stencil_image(VulkanCommandBuffer &cmd, const VkClearDepthStencilValue &value) {
  nassertv(_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT);
  nassertv(_image != VK_NULL_HANDLE);

  // We're not interested in whatever was in here before.
  discard();

  cmd.add_barrier(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  cmd.flush_barriers();

  VkImageSubresourceRange range;
  range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  range.baseMipLevel = 0;
  range.levelCount = _mip_levels;
  range.baseArrayLayer = 0;
  range.layerCount = _array_layers;
  vkCmdClearDepthStencilImage(cmd, _image, _layout, &value, 1, &range);
}

/**
 * Inserts commands to clear the image.
 */
void VulkanTextureContext::
clear_buffer(VulkanCommandBuffer &cmd, uint32_t fill) {
  nassertv(_buffer != VK_NULL_HANDLE);

  discard();
  cmd.add_barrier(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

  vkCmdFillBuffer(cmd, _buffer, 0, VK_WHOLE_SIZE, fill);
}
