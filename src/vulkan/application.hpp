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

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "rndrx/noncopyable.hpp"
#include "rndrx/throw_exception.hpp"
#include <array>
#include "rndrx/to_vector.hpp"

VKAPI_ATTR vk::Bool32 VKAPI_CALL vulkan_validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    VkDebugUtilsMessengerCallbackDataEXT const* message_data,
    void* user_data);

namespace rndrx::vulkan {

std::array<char const*, 1> constexpr kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

class Window : noncopyable {
 public:
  Window() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(
        width_,
        height_,
        "rndrx-vulkan",
        /* glfwGetPrimaryMonitor() */ nullptr,
        nullptr);
  }

  ~Window() {
    glfwDestroyWindow(window_);
  }
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

  SizeEvent handle_window_size() {
    int old_width = width_;
    int old_height = height_;
    glfwGetFramebufferSize(window_, &width_, &height_);
    if(width_ != old_width || height_ != old_height) {
      return SizeEvent::Changed;
    }

    return SizeEvent::None;
  }

 private:
  GLFWwindow* window_ = nullptr;
  int width_ = 1920;
  int height_ = 1080;
};

class Application : noncopyable {
 public:
  Application(Window& window)
      : window_(window)
      , instance_(nullptr)
      , surface_(nullptr)
      , messenger_(nullptr) {
    create_instance();
    create_surface();
    select_device();
  }

  std::uint32_t find_graphics_queue() const {
    auto queue_family_properties = selected_device().getQueueFamilyProperties();
    auto graphics_queue = std::ranges::find_if(
        queue_family_properties,
        [](vk::QueueFamilyProperties const& props) {
          return (props.queueFlags & vk::QueueFlagBits::eGraphics) ==
                 vk::QueueFlagBits::eGraphics;
        });

    return std::distance(queue_family_properties.begin(), graphics_queue);
  }

  std::vector<char const*> get_required_instance_extensions() const {
    std::uint32_t glfw_ext_count = 0;
    char const** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    std::vector<char const*> extensions(glfw_exts, glfw_exts + glfw_ext_count);
#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    return extensions;
  }

  std::vector<char const*> get_required_device_extensions() const {
    std::vector<char const*> extensions;
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
    extensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
    extensions.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
    return extensions;
  }

  vk::raii::Instance const& vk_instance() const {
    return instance_;
  }

  vk::raii::SurfaceKHR const& surface() const {
    return surface_;
  }

  vk::raii::PhysicalDevice const& selected_device() const {
    return physical_devices_[selected_device_idx_];
  }

  Window const& window() const {
    return window_;
  }

  bool run();

 private:
  void create_instance() {
    vk::ApplicationInfo app_info("rndrx-vulkan", 1, "rndrx", 1, VK_API_VERSION_1_3);
    std::vector<char const*> request_layer_names;
#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    if(!check_validation_layer_support()) {
      throw_runtime_error("Debug layer not supported");
    }
#endif

    std::ranges::copy(kValidationLayers, std::back_inserter(request_layer_names));

    auto required_extensions = get_required_instance_extensions();
    vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> create_info(
        {{}, &app_info, request_layer_names, required_extensions},
        {{},
         vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
         vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
             vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
             vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
         &vulkan_validation_callback});

#if !RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    create_info.unlink<vk::DebugUtilsMessengerCreateInfoEXT>();
#endif

    instance_ = vk::raii::Instance(vk_context_, create_info.get());

#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    messenger_ = instance_.createDebugUtilsMessengerEXT(
        create_info.get<vk::DebugUtilsMessengerCreateInfoEXT>());
#endif
  }

  void create_surface() {
    VkSurfaceKHR surface;
    if(glfwCreateWindowSurface(*instance_, window_.glfw(), nullptr, &surface) !=
       VK_SUCCESS) {
      throw_runtime_error("failed to create window surface!");
    }

    surface_ = vk::raii::SurfaceKHR(instance_, surface);
  }

  void select_device() {
    auto required_device_extensions = get_required_device_extensions();
    physical_devices_ =
        instance_.enumeratePhysicalDevices() |
        std::ranges::views::filter([&required_device_extensions](
                                       vk::raii::PhysicalDevice const& dev) {
          return std::ranges::all_of(
              required_device_extensions,
              [&dev](std::string_view extension_name) {
                return std::ranges::any_of(
                    dev.enumerateDeviceExtensionProperties(),
                    [&extension_name](
                        vk::ExtensionProperties const& extension_properties) {
                      return extension_name == extension_properties.extensionName;
                    });
              });
        }) |
        to_vector;
  }

  bool check_validation_layer_support() {
    auto available_layers = vk_context_.enumerateInstanceLayerProperties();
    return std::ranges::any_of(available_layers, [](vk::LayerProperties const& layer) {
      return std::ranges::any_of(
          kValidationLayers,
          [&layer](std::string_view validation_layer) {
            return validation_layer == layer.layerName;
          });
    });

    return true;
  }

  Window& window_;
  vk::raii::Context vk_context_;
  vk::raii::Instance instance_;
  vk::raii::DebugUtilsMessengerEXT messenger_;
  std::vector<vk::raii::PhysicalDevice> physical_devices_;
  vk::raii::SurfaceKHR surface_;
  std::uint32_t selected_device_idx_ = 0;
};
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_APPLICATION_HPP_