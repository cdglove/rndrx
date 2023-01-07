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
#include "composite_render_pass.hpp"
#include "device.hpp"
#include "imgui.h"
#include "imgui_render_pass.hpp"
#include "render_context.hpp"
#include "rndrx/assert.hpp"
#include "rndrx/log.hpp"
#include "rndrx/scope_exit.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"
#include "shader_cache.hpp"
#include "submission_context.hpp"
#include "swapchain.hpp"

namespace rndrx::vulkan {

namespace {

VKAPI_ATTR vk::Bool32 VKAPI_CALL vulkan_validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    VkDebugUtilsMessengerCallbackDataEXT const* message_data,
    void* user_data) {
  if(!(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(severity) &
       vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)) {
    std::cerr << "validation layer: " << message_data->pMessage << std::endl;
    return VK_TRUE;
  }
  return VK_FALSE;
}

ShaderCache load_essential_shaders(Device& device) {
  ShaderCache cache;
  ShaderLoader loader(device, cache);
  loader.load("fullscreen_quad.vsmain");
  loader.load("fullscreen_quad.copyimageopaque");
  loader.load("fullscreen_quad.blendimageinv");
  loader.load("fullscreen_quad.blendimage");
  loader.load("static_model.vsmain");
  loader.load("static_model.phong");
  return cache;
}
} // namespace
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

  RNDRX_ASSERT(item != physical_devices_.end());
  selected_device_idx_ = std::distance(physical_devices_.begin(), item);
}

Application::RunResult Application::run() {
  run_state_ = RunState::Initialising;
  LOG(Info) << "Compatible adapters:";
  for(auto&& device : physical_devices()) {
    auto properties = device.getProperties();
    LOG(Info) << "    " << properties.deviceName
              << ((*selected_device() == *device) ? " (selected)" : "");
  }

  // Limit scope of device state.
  on_pre_create_device_state();
  {
    DeviceState ds(*this);
    on_device_state_created(ds);

    auto exit_main_loop = on_scope_exit([this, &ds] {
      on_pre_destroy_device_state(ds);
      ds.device.vk().waitIdle();
    });

    main_loop(ds);
  }

  on_device_state_destroyed();

  switch(run_state()) {
    case RunState::Restarting:
      return RunResult::Restart;
    case RunState::Quitting:
      return RunResult::Exit;
    default:
      RNDRX_ASSERT(false);
      return RunResult::Exit;
  };
}

Application::DeviceState::DeviceState(Application& app)
    : device(app)
    , swapchain(app, device)
    , imgui_render_pass(app, device, swapchain)
    , shaders(load_essential_shaders(device)){};

class Application::PresentationState : noncopyable {
 public:
  PresentationState(Application& app, DeviceState& ds)
      : final_composite_pass(ds.device, ds.swapchain.surface_format().format, ds.shaders)
      , present_queue(
            ds.device,
            ds.swapchain,
            ds.device.graphics_queue(),
            *final_composite_pass.render_pass())
      , composite_imgui(
            ds.device,
            final_composite_pass,
            ds.imgui_render_pass.target().view()) {
  }

  CompositeRenderPass final_composite_pass;
  PresentationQueue present_queue;
  CompositeRenderPass::DrawItem composite_imgui;
};

void Application::main_loop(DeviceState& ds) {
  LoopState ls;

  Device& device = ds.device;
  Swapchain& swapchain = ds.swapchain;
  ShaderCache& shaders = ds.shaders;

  PresentationState ps(*this, ds);

  std::array<SubmissionContext, 3> submission_contexts = {
      {SubmissionContext(device),
       SubmissionContext(device),
       SubmissionContext(device)}};

  ds.imgui_render_pass.initialise_font(device, submission_contexts[0]);

  run_state_ = RunState::Running;

  auto last_frame_ts = std::chrono::high_resolution_clock::now();
  while(!glfwWindowShouldClose(window_.glfw())) {
    glfwPollEvents();

    on_begin_frame(ds, ls);

    auto now = std::chrono::high_resolution_clock::now();
    auto frame_duration = now - last_frame_ts;
    last_frame_ts = now;
    ls.dt_s = duration_cast<std::chrono::duration<float>>(frame_duration).count();

    update(ds, ls);

    if(run_state_ != RunState::Running) {
      return;
    }

    auto submission_index = ls.frame_id % submission_contexts.size();
    SubmissionContext& sc = submission_contexts[submission_index];
    render(ds, ls, sc, ps);
    on_end_frame(ds, ls);

    ++ls.frame_id;
  }

  run_state_ = RunState::Quitting;
}

void Application::create_instance() {
#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
  if(!check_validation_layer_support()) {
    throw_runtime_error("Debug layer not supported");
  }
#endif

  vk::ApplicationInfo app_info;
  app_info //
      .setPApplicationName("rndrx-vulkan")
      .setApplicationVersion(1)
      .setPEngineName("rndrx")
      .setEngineVersion(1)
      .setApiVersion(VK_API_VERSION_1_3);

  std::vector<char const*> request_layer_names;
  std::ranges::copy(kValidationLayers, std::back_inserter(request_layer_names));

  auto required_extensions = get_required_instance_extensions();
  vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> create_info;
  std::get<0>(create_info) //
      .setPApplicationInfo(&app_info)
      .setPEnabledLayerNames(request_layer_names)
      .setPEnabledExtensionNames(required_extensions);
  std::get<1>(create_info) //
      .setMessageSeverity(
          vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
          vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
          vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)
      .setMessageType(
          vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
          vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
          vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
      .setPfnUserCallback(&vulkan_validation_callback);

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

  // Transfer ownership of the surface to an raii object
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

void Application::update(DeviceState& ds, LoopState& ls) {
  on_begin_update(ds, ls);
  ds.imgui_render_pass.begin_frame();

  if(ImGui::Begin("Adapter Info")) {
    ImGui::LabelText("", "Framerate: %3.1ffps (%3.2fms)", 1 / ls.dt_s, ls.dt_s * 1000);
    auto const& selected = selected_device();
    auto selected_properties = selected.getProperties();
    if(ImGui::BeginCombo("##name", selected_properties.deviceName)) {
      for(auto&& candidate : physical_devices()) {
        auto candidate_properties = candidate.getProperties();
        if(ImGui::Selectable(candidate_properties.deviceName, *candidate == *selected)) {
          if(*candidate != *selected) {
            select_device(candidate);
            LOG(Info) << "Adapter switch from '"
                      << selected_properties.deviceName << "' to '"
                      << candidate_properties.deviceName << "' detected.\n";
            run_state_ = RunState::Restarting;
            return;
          }
        }
      }
      ImGui::EndCombo();
    }
    ImGui::End();
  }

  ds.imgui_render_pass.end_frame();
  on_end_update(ds, ls);
}

void Application::render(
    DeviceState& ds,
    LoopState& ls,
    SubmissionContext& sc,
    PresentationState& ps) {
  sc.begin_rendering(window_.extents());
  on_begin_render(ds, ls);
  ds.imgui_render_pass.render(sc);

  PresentationContext present_context = ps.present_queue.acquire_context();
  RenderContext composite_context;
  composite_context.set_targets(
      window_.extents(),
      present_context.target().view(),
      present_context.target().framebuffer());
  ps.final_composite_pass.render(composite_context, sc, {&ps.composite_imgui, 1});
  sc.finish_rendering();
  ps.present_queue.present(present_context);
  on_end_render(ds, ls);
}

} // namespace rndrx::vulkan
