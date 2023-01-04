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
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>
#pragma once

#include <cstddef>
#include "rndrx/noncopyable.hpp"
#include "vk_mem_alloc.h"

namespace vk {
class Instance;
class Device;
class PhysicalDevice;
}; // namespace vk

namespace rndrx::vulkan::vma {

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

  VmaAllocator vma() const {
    return allocator_;
  }

  Image createImage(VkImageCreateInfo const& create_info) const;

 private:
  VmaAllocator allocator_ = nullptr;
};

namespace detail {
class ImageCreator : noncopyable {
 public:
  ImageCreator(std::nullptr_t){};
  ImageCreator(Allocator const& alloc, VkImageCreateInfo const& create_info, VkImage& image);

 protected:
  Allocator const* allocator_ = nullptr;
  VmaAllocation allocation_ = {};
};

struct ImageHolder {
  VkImage image = nullptr;
};
} // namespace detail

class Image
    // Derive from ImageCreator first so it will be constructed before
    // vk::raii::Image so we can pretend to be a vk::raii::Image
    : private detail::ImageCreator
    , public vk::raii::Image {
 private:
 public:
  Image(std::nullptr_t);
  Image(
      Allocator const& allocator,
      VkImageCreateInfo const& create_info,
      detail::ImageHolder ignore = detail::ImageHolder());
  ~Image();

  Image(Image&&);
  Image& operator=(Image&&);
};

} // namespace rndrx::vulkan::vma

#endif // RNDRX_VULKAN_VMA_ALLOCATOR_HPP_