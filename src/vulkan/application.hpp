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
#ifndef RNDRX_VULKAN_APPLICATION_HPP_
#define RNDRX_VULKAN_APPLICATION_HPP_
#pragma once

#include <array>
#include <span>
#include <vulkan/vulkan_raii.hpp>
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

class Application : noncopyable {
 public:
  Application(Window& window);

  std::uint32_t find_graphics_queue() const;
  std::vector<char const*> get_required_instance_extensions() const;
  std::vector<char const*> get_required_device_extensions() const;

  vk::raii::Instance const& vk_instance() const {
    return instance_;
  }

  vk::raii::SurfaceKHR const& surface() const {
    return surface_;
  }

  std::span<const vk::raii::PhysicalDevice> physical_devices() const {
    return physical_devices_;
  }

  vk::raii::PhysicalDevice const& selected_device() const {
    return physical_devices_[selected_device_idx_];
  }

  void select_device(vk::raii::PhysicalDevice const& device);

  int selected_device_index() const {
    return selected_device_idx_;
  }

  Window const& window() const {
    return window_;
  }

  bool run();

 private:
  void create_instance();
  void create_surface();
  void select_device();
  bool check_validation_layer_support();

  Window& window_;
  vk::raii::Instance instance_ = nullptr;
  int selected_device_idx_;
  vk::raii::Context vk_context_;
  vk::raii::DebugUtilsMessengerEXT messenger_ = nullptr;
  vk::raii::SurfaceKHR surface_ = nullptr;
  std::vector<vk::raii::PhysicalDevice> physical_devices_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_APPLICATION_HPP_