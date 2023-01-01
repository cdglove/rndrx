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
#ifndef RNDRX_VULKAN_RENDERCONTEXT_HPP_
#define RNDRX_VULKAN_RENDERCONTEXT_HPP_
#pragma once

#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class RenderContext : noncopyable {
 public:
  void set_targets(
      vk::Rect2D extents,
      vk::ImageView colour_target,
      vk::Framebuffer framebuffer) {
    target_extents_ = extents;
    colour_target_ = colour_target;
    framebuffer_ = framebuffer;
  }

  vk::Rect2D extents() const {
    return target_extents_;
  }

  vk::Viewport full_viewport() const {
    return vk::Viewport(
        target_extents_.offset.x,
        target_extents_.offset.y,
        target_extents_.extent.width,
        target_extents_.extent.height,
        0.f,
        1.f);
  }

  vk::ImageView colour_target() const {
    return colour_target_;
  }

  vk::Framebuffer framebuffer() const {
    return framebuffer_;
  }

 private:
  vk::Rect2D target_extents_;
  vk::ImageView colour_target_;
  vk::Framebuffer framebuffer_;
};

} // namespace rndrx::vulkan
#endif // RNDRX_VULKAN_RENDERCONTEXT_HPP_