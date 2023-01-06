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
#include "device.hpp"
#include <vulkan/vulkan.hpp>

#include "application.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"

namespace rndrx::vulkan {
Device::Device(Application const& app) {
  create_device(app);
  create_descriptor_pool();
}

std::uint32_t Device::find_memory_type(
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

void Device::create_device(Application const& app) {
  physical_device_ = *app.selected_device();

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

  device_ = app.selected_device().createDevice(create_info.get());
  graphics_queue_ = device_.getQueue(gfx_queue_idx_, 0);
  allocator_ = vma::Allocator(app.vk_instance(), device_, physical_device_);
}

void Device::create_descriptor_pool() {
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

} // namespace rndrx::vulkan