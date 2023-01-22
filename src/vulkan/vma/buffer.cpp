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
#include "buffer.hpp"

#include <cstddef>
#include "allocator.hpp"

namespace rndrx::vulkan::vma {
Buffer::Buffer(Allocator& allocator, vk::BufferCreateInfo const& create_info)
    : allocator_(&allocator) {
  VmaAllocationCreateInfo vma_create_info = {};
  vma_create_info.usage = VMA_MEMORY_USAGE_AUTO;
  vma_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
  VkBuffer buffer = nullptr;
  VkBufferCreateInfo const& create_info_ref = create_info;
  vmaCreateBuffer(
      allocator.vma(),
      &create_info_ref,
      &vma_create_info,
      &buffer,
      &allocation_,
      &info_);
  buffer_ = vk::raii::Buffer(allocator.device(), buffer);
}

Buffer::~Buffer() {
  clear();
}

Buffer& Buffer::operator=(Buffer&& rhs) {
  clear();
  buffer_ = std::move(rhs.buffer_);
  allocator_ = rhs.allocator_;
  allocation_ = rhs.allocation_;
  return *this;
}

void Buffer::clear() {
  if(*buffer_) {
    VkBuffer buffer = buffer_.release();
    vmaDestroyBuffer(allocator_->vma(), buffer, allocation_);
  }
}

} // namespace rndrx::vulkan::vma