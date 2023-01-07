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
#include "device.hpp"
#include "imgui_render_pass.hpp"
#include "rndrx/assert.hpp"
#include "rndrx/noncopyable.hpp"
#include "shader_cache.hpp"
#include "swapchain.hpp"

struct GLFWwindow;

namespace rndrx::vulkan {

class Device;
class Swapchain;
class ShaderCache;
class Window;

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

  void select_device(vk::raii::PhysicalDevice const& device);

  vk::raii::PhysicalDevice const& selected_device() const {
    return physical_devices_[selected_device_idx_];
  }

  int selected_device_index() const {
    return selected_device_idx_;
  }

  Window const& window() const {
    return window_;
  }

  enum class RunResult {
    Restart,
    Exit,
  };

  RunResult run();

  enum class RunState {
    NotRunning = 0,
    Initialising,
    Running,
    Restarting,
    Quitting,
  };

  RunState run_state() const {
    return run_state_;
  }

 protected:
  struct DeviceState : noncopyable {
    DeviceState(Application& app);
    Device device;
    ShaderCache shaders;
    Swapchain swapchain;
    ImGuiRenderPass imgui_render_pass;
  };

  struct LoopState : noncopyable {
    std::uint32_t frame_id = 0;
    float dt_s = 0.f;
  };

 private:
  void create_instance();
  void create_surface();
  void select_device();
  bool check_validation_layer_support();

  void main_loop(DeviceState& ds);
  void update(DeviceState& ds, LoopState& ls);
  class PresentationState;
  void render(DeviceState& ds, LoopState& ls, SubmissionContext& sc, PresentationState& ps);

  virtual void on_pre_create_device_state(){};
  virtual void on_device_state_created(DeviceState& ds){};
  virtual void on_begin_frame(DeviceState& ds, LoopState& ls){};
  virtual void on_begin_update(DeviceState& ds, LoopState& ls){};
  virtual void on_end_update(DeviceState& ds, LoopState& ls){};
  virtual void on_begin_render(DeviceState& ds, LoopState& ls){};
  virtual void on_end_render(DeviceState& ds, LoopState& ls){};
  virtual void on_end_frame(DeviceState& ds, LoopState& ls){};
  virtual void on_pre_destroy_device_state(DeviceState& ds){};
  virtual void on_device_state_destroyed(){};

  Window& window_;
  vk::raii::Instance instance_ = nullptr;
  int selected_device_idx_;
  vk::raii::Context vk_context_;
  vk::raii::DebugUtilsMessengerEXT messenger_ = nullptr;
  vk::raii::SurfaceKHR surface_ = nullptr;
  std::vector<vk::raii::PhysicalDevice> physical_devices_;
  RunState run_state_ = RunState::NotRunning;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_APPLICATION_HPP_