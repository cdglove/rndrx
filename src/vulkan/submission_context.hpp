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
#ifndef RNDRX_VULKAN_SUBMISSIONCONTEXT_HPP_
#define RNDRX_VULKAN_SUBMISSIONCONTEXT_HPP_
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class Device;

class SubmissionContext : noncopyable {
 public:
  explicit SubmissionContext(Device const& device)
      : device_(device) {
    create_command_pool(device);
    create_command_buffers(device);
    create_sync_objects(device);
  }

  vk::CommandBuffer command_buffer() {
    return *command_buffers_[0];
  };

  void begin_rendering();
  void finish_rendering();

 private:
  void create_command_pool(Device const& device);
  void create_command_buffers(Device const& device);
  void create_sync_objects(Device const& device);

  Device const& device_;
  vk::raii::CommandPool command_pool_ = nullptr;
  vk::raii::CommandBuffers command_buffers_ = nullptr;
  vk::raii::Semaphore submit_semaphore_ = nullptr;
  vk::raii::Fence submit_fence_ = nullptr;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_SUBMISSIONCONTEXT_HPP_