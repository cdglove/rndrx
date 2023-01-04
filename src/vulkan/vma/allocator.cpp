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
#include "allocator.hpp"
#include <cstddef>
#include <vulkan/vulkan_raii.hpp>

namespace rndrx::vulkan::vma {
Allocator::Allocator(
    vk::raii::Instance const& instance,
    vk::raii::Device const& device,
    vk::PhysicalDevice physical_device) {
  VmaVulkanFunctions vulkan_functions = {};

  //   vulkan_funcs.vkGetInstanceProcAddr =
  //   instance.getDispatcher()->vkGetInstanceProcAddr;
  //   vulkan_funcs.vkGetDeviceProcAddr =
  //   device.getDispatcher()->vkGetDeviceProcAddr;

  // clang-format off
 vulkan_functions.vkGetPhysicalDeviceProperties = instance.getDispatcher()->vkGetPhysicalDeviceProperties;
 vulkan_functions.vkGetPhysicalDeviceMemoryProperties = instance.getDispatcher()->vkGetPhysicalDeviceMemoryProperties;
 vulkan_functions.vkAllocateMemory = device.getDispatcher()->vkAllocateMemory;
 vulkan_functions.vkFreeMemory = device.getDispatcher()->vkFreeMemory;
 vulkan_functions.vkMapMemory = device.getDispatcher()->vkMapMemory;
 vulkan_functions.vkUnmapMemory = device.getDispatcher()->vkUnmapMemory;
 vulkan_functions.vkFlushMappedMemoryRanges = device.getDispatcher()->vkFlushMappedMemoryRanges;
 vulkan_functions.vkInvalidateMappedMemoryRanges = device.getDispatcher()->vkInvalidateMappedMemoryRanges;
 vulkan_functions.vkBindBufferMemory = device.getDispatcher()->vkBindBufferMemory;
 vulkan_functions.vkBindImageMemory = device.getDispatcher()->vkBindImageMemory;
 vulkan_functions.vkGetBufferMemoryRequirements = device.getDispatcher()->vkGetBufferMemoryRequirements;
 vulkan_functions.vkGetImageMemoryRequirements = device.getDispatcher()->vkGetImageMemoryRequirements;
 vulkan_functions.vkCreateBuffer = device.getDispatcher()->vkCreateBuffer;
 vulkan_functions.vkDestroyBuffer = device.getDispatcher()->vkDestroyBuffer;
 vulkan_functions.vkCreateImage = device.getDispatcher()->vkCreateImage;
 vulkan_functions.vkDestroyImage = device.getDispatcher()->vkDestroyImage;
 vulkan_functions.vkCmdCopyBuffer = device.getDispatcher()->vkCmdCopyBuffer;
 vulkan_functions.vkGetBufferMemoryRequirements2KHR = device.getDispatcher()->vkGetBufferMemoryRequirements2KHR;
 vulkan_functions.vkGetImageMemoryRequirements2KHR = device.getDispatcher()->vkGetImageMemoryRequirements2KHR;
 vulkan_functions.vkBindBufferMemory2KHR = device.getDispatcher()->vkBindBufferMemory2KHR;
 vulkan_functions.vkBindImageMemory2KHR = device.getDispatcher()->vkBindImageMemory2KHR;
 vulkan_functions.vkGetPhysicalDeviceMemoryProperties2KHR = instance.getDispatcher()->vkGetPhysicalDeviceMemoryProperties2KHR;
 vulkan_functions.vkGetDeviceBufferMemoryRequirements = device.getDispatcher()->vkGetDeviceBufferMemoryRequirements;
 vulkan_functions.vkGetDeviceImageMemoryRequirements = device.getDispatcher()->vkGetDeviceImageMemoryRequirements;
  // clang-format on

  VmaAllocatorCreateInfo create_info = {};
  create_info.device = *device;
  create_info.instance = *instance;
  create_info.physicalDevice = physical_device;
  create_info.pVulkanFunctions = &vulkan_functions;

  vmaCreateAllocator(&create_info, &allocator_);
}

Allocator::~Allocator() {
  vmaDestroyAllocator(allocator_);
}

Allocator::Allocator(Allocator&& other) {
  std::swap(other.allocator_, allocator_);
}

Allocator& Allocator::operator=(Allocator&& rhs) {
  allocator_ = rhs.allocator_;
  rhs.allocator_ = nullptr;
  return *this;
}

Image Allocator::createImage(VkImageCreateInfo const& create_info) const {
  return Image(*this, create_info);
}

namespace detail {
ImageCreator::ImageCreator(
    Allocator const& allocator,
    VkImageCreateInfo const& create_info,
    VkImage& image)
    : allocator_(&allocator) {
  VmaAllocationCreateInfo vma_create_info = {};
  vmaCreateImage(
      allocator.vma(),
      &create_info,
      &vma_create_info,
      &image,
      &allocation_,
      nullptr);
}

} // namespace detail

Image::Image(std::nullptr_t)
    : ImageCreator(nullptr)
    , vk::raii::Image(nullptr) {
}

Image::Image(
    Allocator const& allocator,
    VkImageCreateInfo const& create_info,
    detail::ImageHolder image)
    : ImageCreator(allocator, create_info, image.image)
    , vk::raii::Image(allocator.vma() {
}

Image::~Image() {
  vk::Image image = this->vk::raii::Image::release();
  VkImage vk_image = image;
  vmaDestroyImage(allocator_->vma(), vk_image, allocation_);
}

Image::Image(Image&&) = default;
Image& Image::operator=(Image&& rhs) = default;

} // namespace rndrx::vulkan::vma