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
#include "application.hpp"

#include <GLFW/glfw3.h>
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"

VKAPI_ATTR vk::Bool32 VKAPI_CALL vulkan_validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    VkDebugUtilsMessengerCallbackDataEXT const* message_data,
    void* user_data);

namespace rndrx::vulkan {

std::array<char const*, 1> constexpr kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

Window::Window() {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(
      width_,
      height_,
      "rndrx-vulkan",
      /* glfwGetPrimaryMonitor() */ nullptr,
      nullptr);
}

Window::~Window() {
  glfwDestroyWindow(window_);
}

Window::SizeEvent Window::handle_window_size() {
  int old_width = width_;
  int old_height = height_;
  glfwGetFramebufferSize(window_, &width_, &height_);
  if(width_ != old_width || height_ != old_height) {
    return SizeEvent::Changed;
  }

  return SizeEvent::None;
}

Application::Application(Window& window)
    : window_(window) {
  create_instance();
  create_surface();
  select_device();
}

std::uint32_t Application::find_graphics_queue() const {
  auto queue_family_properties = selected_device().getQueueFamilyProperties();
  auto graphics_queue = std::ranges::find_if(
      queue_family_properties,
      [](vk::QueueFamilyProperties const& props) {
        return (props.queueFlags & vk::QueueFlagBits::eGraphics) ==
               vk::QueueFlagBits::eGraphics;
      });

  return std::distance(queue_family_properties.begin(), graphics_queue);
}

std::vector<char const*> Application::get_required_instance_extensions() const {
  std::uint32_t glfw_ext_count = 0;
  char const** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
  std::vector<char const*> extensions(glfw_exts, glfw_exts + glfw_ext_count);
#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
  extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  return extensions;
}

std::vector<char const*> Application::get_required_device_extensions() const {
  std::vector<char const*> extensions;
  extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
  extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
  extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
  extensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
  extensions.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
  return extensions;
}

void Application::select_device(vk::raii::PhysicalDevice const& device) {
  auto item = std::ranges::find_if(
      physical_devices_,
      [&device](vk::raii::PhysicalDevice const& candidate) {
        return *candidate == *device;
      });

  assert(item != physical_devices_.end());
  selected_device_idx_ = std::distance(physical_devices_.begin(), item);
}

bool run();

void Application::create_instance() {
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

void Application::create_surface() {
  VkSurfaceKHR surface;
  if(glfwCreateWindowSurface(*instance_, window_.glfw(), nullptr, &surface) !=
     VK_SUCCESS) {
    throw_runtime_error("failed to create window surface!");
  }

  // Transfer ownership of the surface
  surface_ = vk::raii::SurfaceKHR(instance_, surface);
}

void Application::select_device() {
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

bool Application::check_validation_layer_support() {
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

} // namespace rndrx::vulkan
