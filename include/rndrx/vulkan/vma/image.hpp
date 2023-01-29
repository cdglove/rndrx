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
#ifndef RNDRX_VULKAN_VMA_IMAGE_HPP_
#define RNDRX_VULKAN_VMA_IMAGE_HPP_
#pragma once

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
class Allocator;

class Image : noncopyable {
 public:
  Image(std::nullptr_t){};
  Image(Allocator& allocator, vk::ImageCreateInfo const& create_info);
  ~Image();

  Image(Image&&) = default;
  Image& operator=(Image&&);

  vk::raii::Image const& vk() const {
    return image_;
  }

 private:
  void clear();
  vk::raii::Image image_ = nullptr;
  Allocator* allocator_ = nullptr;
  VmaAllocation allocation_ = {};
};

} // namespace vma
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_VMA_ALLOCATOR_HPP_