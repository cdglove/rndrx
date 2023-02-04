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
#ifndef RNDRX_VULKAN_TEXTURE_HPP_
#define RNDRX_VULKAN_TEXTURE_HPP_
#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/vma/image.hpp"

namespace rndrx { namespace vulkan {
class Device;
}} // namespace rndrx::vulkan

namespace rndrx::vulkan {

struct TextureCreateInfo {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  vk::Sampler sampler;
  std::span<unsigned char const> image_data;
  std::uint32_t component_count = 0;
};

class Texture : noncopyable {
 public:
  Texture() = default;
  Texture(Device& device, TextureCreateInfo const& create_info);
  vk::DescriptorImageInfo descriptor() const;

 private:
  void generate_mip_maps(Device& device, vk::raii::CommandBuffer& cmd_buf);

  vma::Image image_ = nullptr;
  vk::raii::ImageView image_view_ = nullptr;
  vk::Sampler sampler_ = nullptr;
  vk::ImageLayout image_layout_;
  vk::Format format_;
  vk::DescriptorImageInfo descriptor_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint32_t mip_count_ = 0;
  std::uint32_t layer_count_ = 0;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_TEXTURE_HPP_