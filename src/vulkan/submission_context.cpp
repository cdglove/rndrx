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
#include "rndrx/vulkan/submission_context.hpp"

#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <limits>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/throw_exception.hpp"
#include "rndrx/vulkan/device.hpp"

namespace rndrx::vulkan {
SubmissionContext::~SubmissionContext() {
  wait_for_fence();
}

void SubmissionContext::begin_rendering(vk::Rect2D extents) {
  render_extents_ = extents;
  wait_for_fence();
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

void SubmissionContext::wait_for_fence() {
  auto result = device_.vk().waitForFences(
      *submit_fence_,
      VK_TRUE,
      std::numeric_limits<std::uint64_t>::max());
  if(result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to wait for fences.");
  }
}

void SubmissionContext::create_command_pool(Device& device) {
  vk::CommandPoolCreateInfo create_info({}, device.graphics_queue_family_idx());
  command_pool_ = device.vk().createCommandPool(create_info);
}

void SubmissionContext::create_command_buffers(Device& device) {
  vk::CommandBufferAllocateInfo alloc_info(
      *command_pool_,
      vk::CommandBufferLevel::ePrimary,
      1);
  command_buffers_ = vk::raii::CommandBuffers(device.vk(), alloc_info);
}

void SubmissionContext::create_sync_objects(Device& device) {
  vk::FenceCreateInfo fence_create_info(vk::FenceCreateFlagBits::eSignaled);
  submit_fence_ = device.vk().createFence(fence_create_info);
}

} // namespace rndrx::vulkan