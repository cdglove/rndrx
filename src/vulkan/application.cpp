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
#include "rndrx/vulkan/application.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
#include <chrono>
#include <memory>
#include "glm/ext/vector_float4.hpp"
#include "imgui.h"
#include "rndrx/assert.hpp"
#include "rndrx/attachment_ops.hpp"
#include "rndrx/frame_graph_description.hpp"
#include "rndrx/log.hpp"
#include "rndrx/scope_exit.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"
#include "rndrx/vulkan/composite_render_pass.hpp"
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/frame_graph.hpp"
#include "rndrx/vulkan/frame_graph_builder.hpp"
#include "rndrx/vulkan/imgui_render_pass.hpp"
#include "rndrx/vulkan/model.hpp"
#include "rndrx/vulkan/render_context.hpp"
#include "rndrx/vulkan/renderer.hpp"
#include "rndrx/vulkan/shader_cache.hpp"
#include "rndrx/vulkan/submission_context.hpp"
#include "rndrx/vulkan/swapchain.hpp"
#include "rndrx/vulkan/window.hpp"

#include "rndrx/vulkan/gltf_model_creator.hpp"
#include "tiny_gltf.h"

namespace rndrx::vulkan {

namespace {

VKAPI_ATTR vk::Bool32 VKAPI_CALL vulkan_validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    VkDebugUtilsMessengerCallbackDataEXT const* message_data,
    void* user_data) {
  if(!(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(severity) &
       vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)) {
    LOG(Info) << "validation layer: " << message_data->pMessage << std::endl;
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
  loader.load("simple_static_model.vsmain");
  loader.load("simple_static_model.phong");
  return cache;
}

std::array<char const*, 1> constexpr kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

} // namespace

Application::Application() {
  create_instance();
  create_surface();
  select_device();
}

Application::~Application() = default;

std::uint32_t Application::find_graphics_queue_family_idx() const {
  auto queue_family_properties = selected_device().getQueueFamilyProperties();
  auto graphics_queue = std::ranges::find_if(
      queue_family_properties,
      [](vk::QueueFamilyProperties const& props) {
        return (props.queueFlags & vk::QueueFlagBits::eGraphics) ==
               vk::QueueFlagBits::eGraphics;
      });

  RNDRX_ASSERT(graphics_queue != queue_family_properties.end());
  return std::distance(queue_family_properties.begin(), graphics_queue);
}

std::uint32_t Application::find_transfer_queue_family_idx() const {
  auto queue_family_properties = selected_device().getQueueFamilyProperties();

  // Try to find a dedicated transfer queue family
  auto transfer_queue = std::ranges::find_if(
      queue_family_properties,
      [](vk::QueueFamilyProperties const& props) {
        if(props.queueFlags & vk::QueueFlagBits::eGraphics) {
          return false;
        }

        if(props.queueFlags & vk::QueueFlagBits::eCompute) {
          return false;
        }

        return (props.queueFlags & vk::QueueFlagBits::eTransfer) ==
               vk::QueueFlagBits::eTransfer;
      });

  // If we can't find a dedicated family with multiple queues
  // that supports transfer.
  if(transfer_queue == queue_family_properties.end()) {
    transfer_queue = std::ranges::find_if(
        queue_family_properties,
        [](vk::QueueFamilyProperties const& props) {
          if(props.queueCount == 1) {
            return false;
          }

          return (props.queueFlags & vk::QueueFlagBits::eTransfer) ==
                 vk::QueueFlagBits::eTransfer;
        });
  }

  // If we can't find a dedicated family multiple queues try to get one that
  // at least supports transfer.
  if(transfer_queue == queue_family_properties.end()) {
    transfer_queue = std::ranges::find_if(
        queue_family_properties,
        [](vk::QueueFamilyProperties const& props) {
          return (props.queueFlags & vk::QueueFlagBits::eTransfer) ==
                 vk::QueueFlagBits::eTransfer;
        });
  }

  // Otherwise, fall back to the graphics queue and hope for the best
  if(transfer_queue == queue_family_properties.end()) {
    transfer_queue = std::ranges::find_if(
        queue_family_properties,
        [](vk::QueueFamilyProperties const& props) {
          return (props.queueFlags & vk::QueueFlagBits::eGraphics) ==
                 vk::QueueFlagBits::eGraphics;
        });
  }

  RNDRX_ASSERT(transfer_queue != queue_family_properties.end());
  return std::distance(queue_family_properties.begin(), transfer_queue);
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
  std::vector<char const*> extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
      VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
      VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
      VK_KHR_MULTIVIEW_EXTENSION_NAME,
      VK_KHR_MAINTENANCE_2_EXTENSION_NAME,
  };
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

void Application::run() {
  while(run_result_ != RunResult::Exit) {
    run_result_ = RunResult::None;
    run_status_ = RunStatus::Initialising;

    LOG(Info) << "Compatible adapters:";
    for(auto&& device : physical_devices()) {
      auto properties = device.getProperties();
      LOG(Info) << "    " << properties.deviceName
                << ((*selected_device() == *device) ? " (selected)" : "");
    }

    on_pre_create_renderer();

    renderer_ = std::make_unique<Renderer>(*this);

    auto exit_main_loop = on_scope_exit([this] {
      run_status_ = RunStatus::DestroyingDeviceObjects;
      device().vk().waitIdle();
      on_pre_destroy_renderer();
      renderer_.reset();
      run_status_ = RunStatus::DeviceObjectsDestroyed;
      on_renderer_destroyed();
    });

    run_status_ = RunStatus::DeviceObjectsCreated;
    on_renderer_created();
    main_loop();
  }
}

void Application::main_loop() {
  std::array<SubmissionContext, 3> submission_contexts = {{
      SubmissionContext(device()),
      SubmissionContext(device()),
      SubmissionContext(device()),
  }};

  initialise_device_resources(submission_contexts[0]);

  run_status_ = RunStatus::Running;
  auto scope_exit_set_status = on_scope_exit(
      [this] { run_status_ = RunStatus::ShuttingDown; });

  auto last_frame_ts = std::chrono::high_resolution_clock::now();
  std::uint32_t frame_id = 0;
  while(!glfwWindowShouldClose(window_.glfw())) {
    glfwPollEvents();

    on_begin_frame();

    using namespace std::chrono;
    auto now = high_resolution_clock::now();
    auto frame_duration = now - last_frame_ts;
    last_frame_ts = now;
    float dt_s = duration_cast<duration<float>>(frame_duration).count();

    update(dt_s);

    if(run_result_ != RunResult::None) {
      return;
    }

    auto submission_index = frame_id % submission_contexts.size();
    SubmissionContext& sc = submission_contexts[submission_index];
    render(sc);

    on_end_frame();

    ++frame_id;
  }

  run_result_ = RunResult::Exit;
}

Device& Application::device() {
  return renderer_->device();
}

Swapchain& Application::swapchain() {
  return renderer_->swapchain();
}

ShaderCache& Application::shaders() {
  return renderer_->shaders();
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
  get<0>(create_info) //
      .setPApplicationInfo(&app_info)
      .setPEnabledLayerNames(request_layer_names)
      .setPEnabledExtensionNames(required_extensions);
  get<1>(create_info) //
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
        bool has_all_required_extensions = std::ranges::all_of(
            required_device_extensions,
            [&dev](std::string_view extension_name) {
              return std::ranges::any_of(
                  dev.enumerateDeviceExtensionProperties(),
                  [&extension_name](
                      vk::ExtensionProperties const& extension_properties) {
                    return extension_name == extension_properties.extensionName;
                  });
            });

        auto features = dev.getFeatures();
        return has_all_required_extensions && features.samplerAnisotropy;
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

void Application::initialise_device_resources(SubmissionContext& ctx) {
  ctx.begin_rendering(vk::Rect2D());
  on_begin_initialise_device_resources(ctx);
  ctx.finish_rendering();
  ctx.wait_for_fence();
  on_end_initialise_device_resources();
}

void Application::update(float dt_s) {
  on_begin_update();

  update_adapter_info(dt_s);

  on_end_update();
}

void Application::update_adapter_info(float dt_s) {
  if(ImGui::Begin("Adapter Info")) {
    ImGui::LabelText("", "Framerate: %3.1ffps (%3.2fms)", 1 / dt_s, dt_s * 1000);
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
            run_result_ = RunResult::Restart;
          }
        }
      }
      ImGui::EndCombo();
    }
    ImGui::End();
  }
}

void Application::render(SubmissionContext& ctx) {
  ctx.begin_rendering(window_.extents());
  on_begin_render(ctx);

  // PresentationContext present_ctx = device_objects_->present_queue.acquire_context();
  // render_objects_->deferred_frame_graph->render(ctx);
  // on_pre_present(ctx, present_ctx);
  // ctx.finish_rendering();
  // on_end_render(ctx);
  // device_objects_->present_queue.present(present_ctx);
  // on_post_present(present_ctx);
}

void Application::on_pre_create_renderer(){};

void Application::on_renderer_created() {
  // tinygltf::Model gltf_model = gltf::load_model_from_file(
  //     "assets/models/NewSponza_Main_glTF_002.gltf");

  // GltfModelCreator model_creator(gltf_model);
  // Model model(device_objects_->device, device_objects_->shaders, model_creator);

  // FrameGraphBuilder frame_graph_builder(device_objects_->device);
  // frame_graph_builder.register_pass("imgui", &render_objects_->imgui_render_pass);
  // frame_graph_builder.register_pass(
  //     "final_composite",
  //     &device_objects_->final_composite_pass);
  // render_objects_->deferred_frame_graph = std::make_unique<FrameGraph>( //
  //     frame_graph_builder,
  //     FrameGraphDescription()
  //         // .add_render_pass( //
  //         //     FrameGraphRenderPassDescription("gbuffer")
  //         //         .add_output( //
  //         //             FrameGraphAttachmentOutputDescription("depth_stencil")
  //         //                 .format(ImageFormat::D24UnormS8Uint)
  //         //                 .resolution(window_.width(), window_.height())
  //         //                 .load_op(AttachmentLoadOp::Clear)
  //         //                 .clear_depth(1.f)
  //         //                 .clear_stencil(0))
  //         //         .add_output( //
  //         //             FrameGraphAttachmentOutputDescription("albedo")
  //         //                 .format(ImageFormat::B8G8R8A8Unorm)
  //         //                 .resolution(window_.width(), window_.height())
  //         //                 .load_op(AttachmentLoadOp::Clear)
  //         //                 .clear_colour(glm::vec4(0)))
  //         //         .add_output( //
  //         //             FrameGraphAttachmentOutputDescription("normals")
  //         //                 .format(ImageFormat::A2B10G10R10SnormPack32)
  //         //                 .resolution(window_.width(), window_.height())
  //         //                 .load_op(AttachmentLoadOp::Clear)
  //         //                 .clear_colour(glm::vec4(0)))
  //         //         .add_output( //
  //         //             FrameGraphAttachmentOutputDescription("positions")
  //         //                 .format(ImageFormat::A2B10G10R10SnormPack32)
  //         //                 .resolution(window_.width(), window_.height())
  //         //                 .load_op(AttachmentLoadOp::Clear)
  //         //                 .clear_colour(glm::vec4(0))))
  //         .add_render_pass(                            //
  //             FrameGraphRenderPassDescription("imgui") //
  //                 .add_output(                         //
  //                     FrameGraphAttachmentOutputDescription("imgui")
  //                         .format(ImageFormat::B8G8R8A8Unorm)
  //                         .resolution(window_.width(), window_.height())
  //                         .load_op(AttachmentLoadOp::Clear)
  //                         .clear_colour(glm::vec4(0))))
  //         .add_render_pass( //
  //             FrameGraphRenderPassDescription("final_composite")
  //                 .add_input(FrameGraphAttachmentInputDescription("imgui"))
  //                 // .add_input(FrameGraphAttachmentInputDescription("albedo"))
  //                 // .add_input(FrameGraphAttachmentInputDescription("normals"))
  //                 // .add_input(FrameGraphAttachmentInputDescription("positions"))
  //                 .add_output( //
  //                     FrameGraphAttachmentOutputDescription("final")
  //                         .format(ImageFormat::B8G8R8A8Unorm)
  //                         .resolution(window_.width(), window_.height())
  //                         .load_op(AttachmentLoadOp::DontCare)
  //                         .clear_colour(glm::vec4(0)))));

  // render_objects_->imgui_render_pass.initialise_imgui(
  //     device_objects_->device,
  //     *this,
  //     device_objects_->swapchain,
  //     render_objects_->deferred_frame_graph->find_node("imgui")->vk_render_pass());
  // vk::ClearValue clear_value;
  // clear_value.color.setFloat32({0, 0, 0, 0});

  // vk::RenderPassBeginInfo begin_pass;
  // begin_pass //
  //     .setRenderPass(*render_pass_)
  //     .setFramebuffer(*framebuffer_)
  //     .setRenderArea(sc.render_extents())
  //     .setClearValues(clear_value);
  // command_buffer.beginRenderPass(begin_pass, vk::SubpassContents::eInline);
};

void Application::on_begin_initialise_device_resources(
    rndrx::vulkan::SubmissionContext& ctx) {
  //render_objects_->imgui_render_pass.create_fonts_texture(ctx);
}

void Application::on_end_initialise_device_resources() {
  //render_objects_->imgui_render_pass.finish_font_texture_creation();
}

void Application::on_begin_frame(){};

void Application::on_begin_update() {
  //render_objects_->imgui_render_pass.begin_frame();
}

void Application::on_end_update() {
  //render_objects_->imgui_render_pass.end_frame();
}

void Application::on_begin_render(rndrx::vulkan::SubmissionContext& sc) {
}

void Application::on_end_render(SubmissionContext&){};

void Application::on_pre_present(
    rndrx::vulkan::SubmissionContext& sc,
    rndrx::vulkan::PresentationContext& pc) {
  // rndrx::vulkan::RenderContext rc;
  // rc.set_targets(window().extents(), pc.target().view(),
  // pc.target().framebuffer()); final_composite_pass().render(rc, sc,
  // {&render_objects_->composite_imgui, 1});
}

void Application::on_post_present(PresentationContext&){};

void Application::on_end_frame(){};

void Application::on_pre_destroy_renderer() {

}

void Application::on_renderer_destroyed(){};

} // namespace rndrx::vulkan
