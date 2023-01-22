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
#include "image.hpp"

#include "allocator.hpp"
#include <cstddef>

namespace rndrx::vulkan::vma {
Image::Image(Allocator& allocator, vk::ImageCreateInfo const& create_info)
    : allocator_(&allocator) {
  VmaAllocationCreateInfo vma_create_info = {};
  VkImage image = nullptr;
  VkImageCreateInfo const& create_info_ref = create_info;
  vmaCreateImage(
      allocator.vma(),
      &create_info_ref,
      &vma_create_info,
      &image,
      &allocation_,
      nullptr);
  image_ = vk::raii::Image(allocator.device(), image);
}

Image::~Image() {
  clear();
}

Image& Image::operator=(Image&& rhs) {
  clear();
  image_ = std::move(rhs.image_);
  allocator_ = rhs.allocator_;
  allocation_ = rhs.allocation_;
  return *this;
}

void Image::clear() {
  if(*image_) {
    vk::Image img = image_.release();
    VkImage vk_img = img;
    vmaDestroyImage(allocator_->vma(), vk_img, allocation_);
  }
}

} // namespace rndrx::vulkan::vma