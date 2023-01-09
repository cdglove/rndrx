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
#ifndef RNDRX_VULKAN_WINDOW_HPP_
#define RNDRX_VULKAN_WINDOW_HPP_
#pragma once

#include <vulkan/vulkan.hpp>
#include "rndrx/noncopyable.hpp"

struct GLFWwindow;

namespace rndrx::vulkan {

class Window : noncopyable {
 public:
  Window();
  ~Window();

  GLFWwindow* glfw() const {
    return window_;
  }

  int width() const {
    return width_;
  }

  int height() const {
    return height_;
  }

  vk::Rect2D extents() const {
    return vk::Rect2D({0, 0}, vk::Extent2D(width(), height()));
  }

  enum class SizeEvent {
    None,
    Changed,
  };

  SizeEvent handle_window_size();

 private:
  GLFWwindow* window_ = nullptr;
  int width_ = 1920;
  int height_ = 1080;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_APPLICATION_HPP_