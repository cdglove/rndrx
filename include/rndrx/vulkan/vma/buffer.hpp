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
#ifndef RNDRX_VULKAN_VMA_BUFFER_HPP_
#define RNDRX_VULKAN_VMA_BUFFER_HPP_
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

class Buffer : noncopyable {
 public:
  Buffer(std::nullptr_t){};
  Buffer(Allocator& allocator, vk::BufferCreateInfo const& create_info);
  ~Buffer();

  Buffer(Buffer&&) = default;
  Buffer& operator=(Buffer&&);

  void* mapped_data() {
    return info_.pMappedData;
  }

  vk::raii::Buffer const& vk() const {
    return buffer_;
  }

 private:
  void clear();
  vk::raii::Buffer buffer_ = nullptr;
  Allocator* allocator_ = nullptr;
  VmaAllocation allocation_ = {};
  VmaAllocationInfo info_ = {};
};

} // namespace vma
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_VMA_BUFFER_HPP_