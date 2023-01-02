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
#include "submission_context.hpp"

#include <vulkan/vulkan_raii.hpp>
#include "device.hpp"
#include "render_context.hpp"
#include "rndrx/throw_exception.hpp"

namespace rndrx::vulkan {
void SubmissionContext::begin_rendering() {
  auto result = device_.vk().waitForFences(
      *submit_fence_,
      VK_TRUE,
      std::numeric_limits<std::uint64_t>::max());
  if(result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to wait for fences.");
  }

  command_pool_.reset();
  vk::CommandBufferBeginInfo begin_info;
  command_buffer().begin(begin_info);
}

void SubmissionContext::finish_rendering() {
  command_buffer().end();
  vk::PipelineStageFlags stage_flags = vk::PipelineStageFlagBits::eAllCommands;
  vk::SubmitInfo submit_info;
  submit_info //
      .setWaitDstStageMask(stage_flags)
      .setCommandBuffers(*command_buffers_[0])
      .setWaitSemaphoreCount(0);
  device_.vk().resetFences(*submit_fence_);
  device_.graphics_queue().submit(submit_info, *submit_fence_);
}

void SubmissionContext::create_command_pool(Device const& device) {
  vk::CommandPoolCreateInfo create_info({}, device.graphics_queue_family_idx());
  command_pool_ = vk::raii::CommandPool(device.vk(), create_info);
}

void SubmissionContext::create_command_buffers(Device const& device) {
  vk::CommandBufferAllocateInfo alloc_info(
      *command_pool_,
      vk::CommandBufferLevel::ePrimary,
      1);
  command_buffers_ = vk::raii::CommandBuffers(device.vk(), alloc_info);
}

void SubmissionContext::create_sync_objects(Device const& device) {
  vk::FenceCreateInfo fence_create_info(vk::FenceCreateFlagBits::eSignaled);
  submit_fence_ = device.vk().createFence(fence_create_info);
}

// If dynbamic rendering
//   void begin_pass(RenderContext const& rc) {
//     vk::Viewport viewport = rc.full_viewport();
//     command_buffer().setViewport(0, 1, &viewport);
//     auto full_extent = rc.extents();
//     command_buffer().setScissor(0, 1, &full_extent);

//     vk::ClearValue clear_value;
//     clear_value.color.setFloat32({0, 1, 1, 0});
//     vk::RenderingAttachmentInfo colour_info;
//     colour_info //
//         .setImageView(rc.colour_target())
//         .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
//         .setLoadOp(vk::AttachmentLoadOp::eClear)
//         .setClearValue(clear_value)
//         .setResolveMode(vk::ResolveModeFlagBits::eNone);

//     vk::RenderingInfo rendering_info;
//     rendering_info //
//         .setLayerCount(1)
//         .setViewMask(0)
//         .setColorAttachments(colour_info)
//         .setRenderArea(rc.extents());

//     command_buffer().beginRendering(rendering_info);
//   }

//   void end_pass() {
//     command_buffer().endRendering();
//   }

} // namespace rndrx::vulkan