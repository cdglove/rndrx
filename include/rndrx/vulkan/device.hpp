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
#ifndef RNDRX_VULKAN_DEVICE_HPP_
#define RNDRX_VULKAN_DEVICE_HPP_
#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"
#include "vma/allocator.hpp"
// #include "rndrx/vulkan/shader_cache.hpp"

namespace rndrx { namespace vulkan {
class Application;
}} // namespace rndrx::vulkan

namespace rndrx::vulkan {

class Device : noncopyable {
 public:
  Device() = default;
  explicit Device(Application const& app);
  ~Device() = default;
  Device(Device&&) = default;
  Device& operator=(Device&&) = default;

  vk::raii::Device const& vk() const {
    return device_;
  }

  vk::PhysicalDevice physical_device() {
    return physical_device_;
  }

  vk::DescriptorPool descriptor_pool() const {
    return *descriptor_pool_;
  }

  std::uint32_t graphics_queue_family_idx() const {
    return queue_family_indices_.graphics;
  }

  vk::raii::Queue& graphics_queue() {
    return graphics_queue_;
  }

  vk::raii::CommandPool& graphics_command_pool() {
    return graphics_command_pool_;
  }

  vk::raii::CommandBuffer alloc_graphics_command_buffer() {
    return std::move(vk().allocateCommandBuffers( //
                             vk::CommandBufferAllocateInfo()
                                 .setCommandBufferCount(1)
                                 .setCommandPool(*graphics_command_pool())
                                 .setLevel(vk::CommandBufferLevel::ePrimary))
                         .front());
  }

  std::uint32_t transfer_queue_family_idx() const {
    return queue_family_indices_.graphics;
  }

  vk::raii::Queue& transfer_queue() {
    return graphics_queue_;
  }

  vk::raii::CommandPool& transfer_command_pool() {
    return transfer_command_pool_;
  }

  vk::raii::CommandBuffer alloc_transfer_command_buffer() {
    return std::move(vk().allocateCommandBuffers( //
                             vk::CommandBufferAllocateInfo()
                                 .setCommandBufferCount(1)
                                 .setCommandPool(*transfer_command_pool())
                                 .setLevel(vk::CommandBufferLevel::ePrimary))
                         .front());
  }

  vma::Allocator& allocator() {
    return allocator_;
  }

  // ShaderCache& shader_cache() {
  //   return shaders_;
  // }

 private:
  void create_device(Application const& app);
  void create_descriptor_pool();
  void create_command_pools();

  vk::raii::Device device_ = nullptr;
  vk::raii::Queue graphics_queue_ = nullptr;
  vk::raii::Queue transfer_queue_ = nullptr;
  vk::raii::CommandPool graphics_command_pool_ = 0;
  vk::raii::CommandPool transfer_command_pool_ = 0;
  vk::raii::DescriptorPool descriptor_pool_ = nullptr;
  vk::PhysicalDevice physical_device_ = nullptr;
  //ShaderCache shaders_;

  struct {
    std::uint32_t graphics = 0;
    std::uint32_t transfer = 0;
  } queue_family_indices_;

  vma::Allocator allocator_ = nullptr;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_DEVICE_HPP_