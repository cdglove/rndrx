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

#include <vulkan/vulkan_core.h>
#include <memory>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/frame_graph.hpp"
#include "rndrx/vulkan/render_target.hpp"
#include "rndrx/vulkan/vma/image.hpp"

class ImDrawData;
class ImDrawList;
namespace rndrx::vulkan {
class Application;
class Device;
class SubmissionContext;
class Swapchain;
class Window;
} // namespace rndrx::vulkan

namespace rndrx::vulkan {

class ImGuiRenderPass
    : public FrameGraphRenderPass
    , noncopyable {
 public:
  ImGuiRenderPass();
  ImGuiRenderPass(Device& device);
  ImGuiRenderPass(ImGuiRenderPass&&);
  ImGuiRenderPass& operator=(ImGuiRenderPass&&);
  ~ImGuiRenderPass();
  void initialise_imgui(
      Device& device,
      Application const& app,
      Swapchain const& swapchain,
      vk::RenderPass render_pass);
  void begin_frame();
  void end_frame();
  void pre_render(SubmissionContext& sc) override;
  void render(SubmissionContext& sc) override;
  void post_render(SubmissionContext& sc) override;
  void create_fonts_texture(SubmissionContext& sc);
  void finish_font_texture_creation();

 private:
  static void check_vk_result(VkResult result);
  void create_descriptor_pool(Device const& device);

  vk::raii::DescriptorPool descriptor_pool_ = nullptr;
  std::unique_ptr<ImDrawData> draw_data_;
  std::vector<ImDrawList*> draw_list_memory_;
};
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_IMGUIRENDERPASS_HPP_