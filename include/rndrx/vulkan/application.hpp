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
#include "composite_render_pass.hpp"
#include "device.hpp"
#include "imgui_render_pass.hpp"
#include "rndrx/assert.hpp"
#include "rndrx/noncopyable.hpp"
#include "shader_cache.hpp"
#include "swapchain.hpp"
#include "window.hpp"

struct GLFWwindow;

namespace rndrx::vulkan {

class Device;
class Swapchain;
class ShaderCache;

class Application : noncopyable {
 public:
  Application();
  ~Application();

  std::uint32_t find_graphics_queue_family_idx() const;
  std::uint32_t find_transfer_queue_family_idx() const;
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

  void run();

 private:
  Device& device();
  Swapchain& swapchain();
  ShaderCache& shaders();
  CompositeRenderPass& final_composite_pass();

  void create_instance();
  void create_surface();
  void select_device();
  bool check_validation_layer_support();

  void main_loop();
  void initialise_device_resources(SubmissionContext& ctx);
  void update(float dt_s);
  void update_adapter_info(float dt_s);
  void render(SubmissionContext& ctx);
  void present(PresentationContext& ctx);

  void on_pre_create_device_objects();
  void on_device_objects_created();
  void on_begin_initialise_device_resources(SubmissionContext&);
  void on_end_initialise_device_resources();
  void on_begin_frame();
  void on_begin_update();
  void on_end_update();
  void on_begin_render(SubmissionContext&);
  void on_end_render(SubmissionContext&);
  void on_pre_present(SubmissionContext&, PresentationContext&);
  void on_post_present(PresentationContext&);
  void on_end_frame();
  void on_pre_destroy_device_objects();
  void on_device_objects_destroyed();

  enum class RunResult {
    None,
    Restart,
    Exit,
  };
  
  enum class RunStatus {
    NotRunning = 0,
    Initialising,
    DeviceObjectsCreated,
    Running,
    ShuttingDown,
    DestroyingDeviceObjects,
    DeviceObjectsDestroyed,
  };

  Window window_;
  vk::raii::Instance instance_ = nullptr;
  int selected_device_idx_;
  vk::raii::Context vk_context_;
  vk::raii::DebugUtilsMessengerEXT messenger_ = nullptr;
  vk::raii::SurfaceKHR surface_ = nullptr;
  std::vector<vk::raii::PhysicalDevice> physical_devices_;
  RunStatus run_status_ = RunStatus::NotRunning;
  RunResult run_result_ = RunResult::None;
  struct DeviceObjects;
  std::unique_ptr<DeviceObjects> device_objects_;
  struct RenderObjects;
  std::unique_ptr<RenderObjects> render_objects_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_APPLICATION_HPP_