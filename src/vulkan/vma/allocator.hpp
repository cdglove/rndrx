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
#ifndef RNDRX_VULKAN_VMA_ALLOCATOR_HPP_
#define RNDRX_VULKAN_VMA_ALLOCATOR_HPP_
#pragma once

#include <vulkan/vulkan_core.h>
#include <cstddef>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"
#include "vk_mem_alloc.h"

namespace vk {
class Instance;
class Device;
class PhysicalDevice;
}; // namespace vk

namespace rndrx::vulkan {
class Device;

namespace vma {

class Image;

class Allocator : noncopyable {
 public:
  Allocator(std::nullptr_t){};
  Allocator(
      vk::raii::Instance const& instance,
      vk::raii::Device const& device,
      vk::PhysicalDevice physical_device);
  ~Allocator();

  Allocator(Allocator&&);
  Allocator& operator=(Allocator&&);

  vk::raii::Device const& device() const {
    return *device_;
  }
  
  VmaAllocator vma() {
    return allocator_;
  }

  Image createImage(VkImageCreateInfo const& create_info);

 private:
  vk::raii::Device const* device_ = nullptr;
  VmaAllocator allocator_ = nullptr;
};

class Image : noncopyable {
 public:
  Image(std::nullptr_t) {};
  Image(Allocator& allocator, VkImageCreateInfo const& create_info);
  ~Image();

  Image(Image&&) = default;
  Image& operator=(Image&&) = default;

  vk::raii::Image const& vk() const {
    return image_;
  }

 private:
  vk::raii::Image image_ = nullptr;
  Allocator* allocator_ = nullptr;
  VmaAllocation allocation_ = {};
};

} // namespace vma
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_VMA_ALLOCATOR_HPP_