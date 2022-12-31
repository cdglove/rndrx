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
#ifndef RNDRX_VULKAN_RENDERTARGET_HPP_
#define RNDRX_VULKAN_RENDERTARGET_HPP_
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace rndrx::vulkan {

class RenderTarget {
 public:
  RenderTarget(vk::Image image, vk::ImageView view, vk::Framebuffer framebuffer)
      : image_(image)
      , view_(view)
      , framebuffer_(framebuffer) {
  }

  vk::Image const& image() const {
    return image_;
  }

  vk::ImageView const& view() const {
    return view_;
  }

  vk::Framebuffer const& framebuffer() const {
    return framebuffer_;
  }

 private:
  vk::Image image_;
  vk::ImageView view_;
  vk::Framebuffer framebuffer_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_RENDERTARGET_HPP_