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

#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "render_target.hpp"
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class Application;
class Device;
class PresentContext : noncopyable {
 public:
  PresentContext(
      vk::raii::SwapchainKHR const& swapchain,
      std::vector<vk::Image> swapchain_images,
      std::vector<vk::raii::ImageView> const& swapchain_image_views,
      std::vector<vk::raii::Framebuffer> const& swapchain_framebuffers,
      vk::raii::Queue const& present_queue,
      vk::Semaphore image_ready_semaphore)
      : swapchain_(swapchain)
      , swapchain_images_(std::move(swapchain_images))
      , swapchain_image_views_(swapchain_image_views)
      , swapchain_framebuffers_(swapchain_framebuffers)
      , present_queue_(present_queue)
      , image_ready_semaphore_(image_ready_semaphore) {
  }

  RenderTarget acquire_next_image();
  void present();

 private:
  vk::raii::SwapchainKHR const& swapchain_;
  std::vector<vk::Image> swapchain_images_;
  std::vector<vk::raii::ImageView> const& swapchain_image_views_;
  std::vector<vk::raii::Framebuffer> const& swapchain_framebuffers_;
  vk::raii::Queue const& present_queue_;
  vk::Semaphore image_ready_semaphore_;
  std::uint32_t image_idx_ = 0;
};

class Swapchain : noncopyable {
 public:
  Swapchain(Application const& app, Device& device);

  PresentContext create_present_context(std::uint32_t frame_id);

  vk::SurfaceFormatKHR surface_format() const {
    return surface_format_;
  }

  std::uint32_t num_images() const {
    return swapchain_images_.size();
  }

 private:
  void create_render_pass(Device const& device, vk::SurfaceFormatKHR surface_format);
  void create_swapchain(Application const& app);
  void create_sync_objects();

  Device const& device_;
  vk::raii::SwapchainKHR swapchain_;
  std::vector<vk::Image> swapchain_images_;
  std::vector<vk::raii::ImageView> swapchain_image_views_;
  std::vector<vk::raii::Framebuffer> swapchain_framebuffers_;
  std::vector<vk::raii::Semaphore> image_ready_semaphores_;
  vk::raii::RenderPass composite_render_pass_;
  vk::SurfaceFormatKHR surface_format_;
  std::uint32_t queue_family_idx_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_SWAPCHAIN_HPP_