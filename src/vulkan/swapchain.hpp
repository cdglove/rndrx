// Copyright (c) 2022 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef RNDRX_VULKAN_SWAPCHAIN_HPP_
#define RNDRX_VULKAN_SWAPCHAIN_HPP_
#pragma once

#include <limits>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "render_target.hpp"
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class Application;
class Device;
class Swapchain;

class PresentationContext {
 public:
  RenderTarget const& target() const {
    return rt_;
  }

 private:
  friend class PresentationQueue;

  PresentationContext(
      vk::Image image,
      vk::ImageView image_view,
      vk::Framebuffer framebuffer,
      std::uint32_t image_idx,
      std::uint32_t sync_idx)
      : rt_(image, image_view, framebuffer)
      , image_idx_(image_idx)
      , sync_idx_(sync_idx) {
  }

  RenderTarget rt_;
  std::uint32_t image_idx_;
  std::uint32_t sync_idx_;
};

class PresentationQueue : noncopyable {
 public:
  PresentationQueue(
      Device const& device,
      Swapchain const& swapchain,
      vk::raii::Queue const& present_queue,
      vk::RenderPass renderpass)
      : swapchain_(swapchain)
      , present_queue_(present_queue) {
    create_image_views(device);
    create_framebuffers(device, renderpass);
    create_sync_objects(device);
  }

  ~PresentationQueue();

  PresentationContext acquire_context();
  void present_context(PresentationContext const& ctx) const;

 private:
  void create_image_views(Device const& device);
  void create_framebuffers(Device const& device, vk::RenderPass renderpass);
  void create_sync_objects(Device const& device);

  std::vector<vk::raii::ImageView> image_views_;
  std::vector<vk::raii::Framebuffer> framebuffers_;
  std::vector<vk::raii::Fence> image_ready_fences_;
  Swapchain const& swapchain_;
  vk::raii::Queue const& present_queue_;
  std::uint32_t image_idx_ = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t sync_idx_ = 0;
};

class Swapchain : noncopyable {
 public:
  Swapchain(Application const& app, Device& device);

  vk::SurfaceFormatKHR surface_format() const {
    return surface_format_;
  }

  vk::Extent2D const& extent() const {
    return extent_;
  }

  std::span<vk::Image const> images() const {
    return images_;
  }

  vk::raii::SwapchainKHR const& vk() const {
    return swapchain_;
  }

 private:
  void create_swapchain(Application const& app, Device& device);

  vk::raii::SwapchainKHR swapchain_;
  std::vector<vk::Image> images_;
  vk::raii::RenderPass composite_render_pass_;
  vk::SurfaceFormatKHR surface_format_;
  std::uint32_t queue_family_idx_;
  vk::Extent2D extent_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_SWAPCHAIN_HPP_