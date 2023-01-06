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
#ifndef RNDRX_VULKAN_IMGUIRENDERPASS_HPP_
#define RNDRX_VULKAN_IMGUIRENDERPASS_HPP_
#pragma once

#include <vulkan/vulkan.hpp>
#include "render_target.hpp"
#include "rndrx/noncopyable.hpp"
#include "vma/allocator.hpp"

namespace rndrx::vulkan {
class Application;
class Window;
class Swapchain;
class SubmissionContext;

class ImGuiRenderPass : noncopyable {
 public:
  ImGuiRenderPass(Application const& app, Device& device, Swapchain const& swapchain);
  ~ImGuiRenderPass();
  void update();
  void render(SubmissionContext& sc);
  void initialise_font(Device const& device, SubmissionContext& sc);

  RenderTarget target() const {
    return {*image_.vk(), *image_view_, *framebuffer_};
  }

 private:
  static void check_vk_result(VkResult result);
  void create_descriptor_pool(Device const& device);
  void create_image(Device& device, Window const& window);
  void create_render_pass(Device const& device);

  vk::raii::DescriptorPool descriptor_pool_ = nullptr;
  vk::raii::RenderPass render_pass_ = nullptr;
  vma::Image image_ = nullptr;
  vk::raii::DeviceMemory image_memory_ = nullptr;
  vk::raii::ImageView image_view_ = nullptr;
  vk::raii::Framebuffer framebuffer_ = nullptr;
};
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_IMGUIRENDERPASS_HPP_