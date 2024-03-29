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
#ifndef RNDRX_VULKAN_PRESENTRENDERPASS_HPP_
#define RNDRX_VULKAN_PRESENTRENDERPASS_HPP_
#pragma once

#include <span>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/frame_graph.hpp"

namespace rndrx::vulkan {

class SubmissionContext;
class RenderContext;
class ShaderCache;
class Device;

class CompositeRenderPass
    : public FrameGraphRenderPass
    , noncopyable {
 public:
  class DrawItem;
  CompositeRenderPass() = default;
  CompositeRenderPass(
      Device const& device,
      vk::Format present_format,
      ShaderCache const& sc) {
    // create_render_pass(device, present_format);
    create_pipeline_layout(device);
    create_pipeline(device, sc);
  }

  RNDRX_DEFAULT_MOVABLE(CompositeRenderPass);

  void pre_render(SubmissionContext& sc) override;
  void render(SubmissionContext& sc) override;
  void post_render(SubmissionContext& sc) override;

  vk::raii::RenderPass const& render_pass() {
    return render_pass_;
  }

 private:
  // void create_render_pass(Device const& device, vk::Format present_format);
  void create_pipeline_layout(Device const& device);
  void create_pipeline(Device const& device, ShaderCache const& sc);

  vk::raii::Sampler sampler_ = nullptr;
  vk::raii::DescriptorSetLayout descriptor_layout_ = nullptr;
  vk::raii::PipelineLayout pipeline_layout_ = nullptr;
  vk::raii::RenderPass render_pass_ = nullptr;
  vk::raii::Pipeline copy_image_pipeline_ = nullptr;
};

// class CompositeRenderPass::DrawItem : rndrx::noncopyable {
//  public:
//   DrawItem() {
//   }
//   DrawItem(Device& device, CompositeRenderPass const& parent_pass,
//   vk::ImageView source) {
//     create_descriptor_set(device, parent_pass);
//     update_descriptor_set(device, parent_pass, source);
//   }

//   DrawItem(DrawItem&&) = default;
//   DrawItem& operator=(DrawItem&&) = default;

//   void draw(CompositeRenderPass const& pass, SubmissionContext& sc);

//  private:
//   void create_descriptor_set(Device& device, CompositeRenderPass const&
//   parent_pass); void update_descriptor_set(
//       Device& device,
//       CompositeRenderPass const& parent_pass,
//       vk::ImageView source);

//   vk::raii::DescriptorSet descriptor_set_ = nullptr;
// };

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_PRESENTRENDERPASS_HPP_