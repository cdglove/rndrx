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
#include "rndrx/vulkan/device.hpp"

#include <vulkan/vulkan.hpp>
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"
#include "rndrx/vulkan/application.hpp"

namespace rndrx::vulkan {
Device::Device(Application const& app) {
  create_device(app);
  create_descriptor_pool();
  create_command_pools();
}

void Device::create_device(Application const& app) {
  physical_device_ = *app.selected_device();

  queue_family_indices_.graphics = app.find_graphics_queue_family_idx();
  queue_family_indices_.transfer = app.find_transfer_queue_family_idx();
  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;

  if(queue_family_indices_.graphics != queue_family_indices_.transfer) {
    float const priority = 1.f;
    queue_create_infos.push_back(
        vk::DeviceQueueCreateInfo()
            .setQueueFamilyIndex(queue_family_indices_.graphics)
            .setQueueCount(1)
            .setPQueuePriorities(&priority));
    queue_create_infos.push_back(
        vk::DeviceQueueCreateInfo()
            .setQueueFamilyIndex(queue_family_indices_.transfer)
            .setQueueCount(1)
            .setPQueuePriorities(&priority));
  }
  else {
    std::array<float, 2> priorities = {1.f, 1.f};
    queue_create_infos.push_back(
        vk::DeviceQueueCreateInfo()
            .setQueueFamilyIndex(queue_family_indices_.graphics)
            .setQueueCount(2)
            .setQueuePriorities(priorities));
  }

  auto required_extensions = app.get_required_device_extensions();

  vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>
      create_info(               //
          vk::DeviceCreateInfo() //
              .setQueueCreateInfos(queue_create_infos)
              .setPEnabledExtensionNames(required_extensions),
          vk::PhysicalDeviceFeatures2().setFeatures( //
              vk::PhysicalDeviceFeatures()           //
                  .setSamplerAnisotropy(VK_TRUE)),
          vk::PhysicalDeviceVulkan13Features() //
              .setSynchronization2(VK_TRUE)
              .setDynamicRendering(VK_TRUE));

  device_ = app.selected_device().createDevice(create_info.get());
  if(queue_family_indices_.graphics != queue_family_indices_.transfer) {
    graphics_queue_ = device_.getQueue(queue_family_indices_.graphics, 0);
    transfer_queue_ = device_.getQueue(queue_family_indices_.transfer, 0);
  }
  else {
    graphics_queue_ = device_.getQueue(queue_family_indices_.graphics, 0);
    transfer_queue_ = device_.getQueue(queue_family_indices_.transfer, 1);
  }

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

  descriptor_pool_ = device_.createDescriptorPool( //
      vk::DescriptorPoolCreateInfo()
          .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
          .setMaxSets(1000)
          .setPoolSizes(pool_sizes));
}

void Device::create_command_pools() {
  graphics_command_pool_ = device_.createCommandPool(
      vk::CommandPoolCreateInfo() //
          .setQueueFamilyIndex(graphics_queue_family_idx()));
  transfer_command_pool_ = device_.createCommandPool(
      vk::CommandPoolCreateInfo() //
          .setQueueFamilyIndex(transfer_queue_family_idx()));
}

} // namespace rndrx::vulkan