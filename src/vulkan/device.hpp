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

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "application.hpp"
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class Device : noncopyable {
 public:
  explicit Device(Application const& app) {
    create_device(app);
    create_descriptor_pool();
  }

  vk::raii::Device const& vk() const {
    return device_;
  }

  vk::DescriptorPool descriptor_pool() { // intentionally non-const const since
                                         // descriptor_pool_ can be modified
    return *descriptor_pool_;
  }

  std::uint32_t graphics_queue_family_idx() const {
    return gfx_queue_idx_;
  }

  vk::raii::Queue const& graphics_queue() const {
    return graphics_queue_;
  }

  std::uint32_t find_memory_type(
      std::uint32_t type_filter,
      vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties mem_properties;
    physical_device_.getMemoryProperties(&mem_properties);

    for(std::uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
      if((type_filter & (1 << i)) &&
         (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw_runtime_error("failed to find suitable memory type!");
    return 0;
  }

 private:
  void create_device(Application const& app) {
    float priority = 1.f;
    gfx_queue_idx_ = app.find_graphics_queue();
    vk::DeviceQueueCreateInfo queue_create_info({}, gfx_queue_idx_, 1, &priority);
    auto required_extensions = app.get_required_device_extensions();
    vk::PhysicalDeviceVulkan13Features vulkan_13_features;
    vulkan_13_features.synchronization2 = true;
    vulkan_13_features.dynamicRendering = true;
    vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>
        create_info = {
            {{}, queue_create_info, {}, required_extensions, nullptr},
            {},
            vulkan_13_features};
    physical_device_ = *app.selected_device();
    device_ = vk::raii::Device(app.selected_device(), create_info.get());
    graphics_queue_ = device_.getQueue(gfx_queue_idx_, 0);
  }

  void create_descriptor_pool() {
    std::array<vk::DescriptorPoolSize, 11> pool_sizes = {
        {{vk::DescriptorType::eSampler, 1000},
         {vk::DescriptorType::eCombinedImageSampler, 1000},
         {vk::DescriptorType::eSampledImage, 1000},
         {vk::DescriptorType::eStorageImage, 1000},
         {vk::DescriptorType::eUniformTexelBuffer, 1000},
         {vk::DescriptorType::eStorageTexelBuffer, 1000},
         {vk::DescriptorType::eUniformBuffer, 1000},
         {vk::DescriptorType::eStorageBuffer, 1000},
         {vk::DescriptorType::eUniformBufferDynamic, 1000},
         {vk::DescriptorType::eStorageBufferDynamic, 1000},
         {vk::DescriptorType::eInputAttachment, 1000}}};

    vk::DescriptorPoolCreateInfo create_info;
    create_info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setMaxSets(1000)
        .setPoolSizes(pool_sizes);
    descriptor_pool_ = device_.createDescriptorPool(create_info);
  }

  vk::raii::Device device_ = nullptr;
  vk::raii::Queue graphics_queue_ = nullptr;
  vk::raii::DescriptorPool descriptor_pool_ = nullptr;
  vk::PhysicalDevice physical_device_ = nullptr;
  std::uint32_t gfx_queue_idx_ = 0;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_DEVICE_HPP_