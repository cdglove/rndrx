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
#define NOMINMAX                           1
#define WIN32_LEAN_AND_MEAN                1
#define GLFW_EXPOSE_NATIVE_WIN32           1
#define GLM_FORCE_RADIANS                  1
#define GLM_FORCE_DEPTH_ZERO_TO_ONE        1
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES 1
#define TINYOBJLOADER_IMPLEMENTATION       1
#define STB_IMAGE_IMPLEMENTATION           1
#define GLFW_INCLUDE_VULKAN                1

#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/fast_trigonometry.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iostream>
#include <iterator>
#include <memory>
#include <limits>
#include <queue>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "application.hpp"
#include "device.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "rndrx/log.hpp"
#include "rndrx/noncopyable.hpp"
#include "shader_cache.hpp"
#include "swapchain.hpp"

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

void load_shader(
    rndrx::vulkan::Device& device,
    std::vector<std::uint32_t>& buffer,
    rndrx::vulkan::ShaderCache& cache,
    std::string_view shader) {
  std::filesystem::path p(shader);
  auto filesize = std::filesystem::file_size(p);
  std::ifstream instream(p, std::ios::binary);
  buffer.resize(filesize / sizeof(std::uint32_t));
  instream.read(reinterpret_cast<char*>(buffer.data()), filesize);
  cache.add(device, shader, buffer);
}

rndrx::vulkan::ShaderCache load_shaders(rndrx::vulkan::Device& device) {
  rndrx::vulkan::ShaderCache cache;
  std::vector<std::uint32_t> buffer;
  load_shader(
      device,
      buffer,
      cache,
      "assets/shaders/fullscreen_quad.vsmain.spv");
  load_shader(
      device,
      buffer,
      cache,
      "assets/shaders/fullscreen_quad.copyimageinv.spv");
  load_shader(device, buffer, cache, "assets/shaders/static_model.vsmain.spv");
  load_shader(device, buffer, cache, "assets/shaders/static_model.phong.spv");
  return cache;
}

namespace rndrx::vulkan {
class RenderContext : noncopyable {
 public:
  void set_targets(vk::Rect2D extents, vk::ImageView colour_target) {
    target_extents_ = extents;
    colour_target_ = colour_target;
  }

  vk::Rect2D extents() const {
    return target_extents_;
  }

  vk::ImageView colour_target() const {
    return colour_target_;
  }

 private:
  vk::Rect2D target_extents_;
  vk::ImageView colour_target_;
};

class SubmissionContext : noncopyable {
 public:
  explicit SubmissionContext(Device const& device)
      : device_(device)
      , command_pool_(nullptr)
      , command_buffers_(nullptr)
      , submit_semaphore_(nullptr)
      , submit_fence_(nullptr) {
    create_command_pool(device);
    create_command_buffers(device);
    create_sync_objects(device);
  }

  vk::CommandBuffer command_buffer() {
    return *command_buffers_[0];
  };

  void begin_pass(RenderContext const& rc) {
    vk::RenderingAttachmentInfo colour_info(
        rc.colour_target(),
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone);
    vk::RenderingInfo rendering_info({}, rc.extents(), 1, 0, 1, &colour_info);
    command_buffer().beginRendering(rendering_info);
  }

  void end_pass() {
    command_buffer().endRendering();
  }

  void begin_rendering() {
    std::array<vk::Fence, 1> fences = {*submit_fence_};
    auto result = device_.vk().waitForFences(
        fences,
        VK_TRUE,
        std::numeric_limits<std::uint64_t>::max());
    if(result != vk::Result::eSuccess) {
      throw_runtime_error("Failed to wait for fences.");
    }

    command_pool_.reset();
    vk::CommandBufferBeginInfo begin_info;
    command_buffer().begin(begin_info);
  }

  void finish_rendering() {
    command_buffer().end();
    vk::PipelineStageFlags stage_flags = vk::PipelineStageFlagBits::eAllCommands;
    vk::SubmitInfo submit_info(0, nullptr, &stage_flags, 1, &*command_buffers_[0], 0, nullptr);
    std::array<vk::Fence, 1> fences = {*submit_fence_};
    device_.vk().resetFences(fences);
    device_.graphics_queue().submit(submit_info, *submit_fence_);
  }

 private:
  void create_command_pool(Device const& device) {
    vk::CommandPoolCreateInfo create_info({}, device.graphics_queue_family_idx());
    command_pool_ = vk::raii::CommandPool(device.vk(), create_info);
  }

  void create_command_buffers(Device const& device) {
    vk::CommandBufferAllocateInfo alloc_info(
        *command_pool_,
        vk::CommandBufferLevel::ePrimary,
        1);
    command_buffers_ = vk::raii::CommandBuffers(device.vk(), alloc_info);
  }

  void create_sync_objects(Device const& device) {
    vk::SemaphoreCreateInfo semaphore_create_info;
    submit_semaphore_ = vk::raii::Semaphore(device.vk(), semaphore_create_info);
    vk::FenceCreateInfo fence_create_info(vk::FenceCreateFlagBits::eSignaled);
    submit_fence_ = vk::raii::Fence(device.vk(), fence_create_info);
  }

  Device const& device_;
  vk::raii::CommandPool command_pool_;
  vk::raii::CommandBuffers command_buffers_;
  vk::raii::Semaphore submit_semaphore_;
  vk::raii::Fence submit_fence_;
};

} // namespace rndrx::vulkan

class FinalCompositeRenderPass : rndrx::noncopyable {
 public:
  FinalCompositeRenderPass(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::ShaderCache const& sc)
      : copy_image_pipeline_(nullptr) {
    create_pipeline(device, sc);
  }

 private:
  void create_pipeline(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::ShaderCache const& sc) {
    vk::ShaderModule vertex_shader =
        *sc.get("assets/shaders/fullscreen_quad.vsmain.spv").module;
    vk::ShaderModule fragment_shader =
        *sc.get("assets/shaders/fullscreen_quad.copyimageinv.spv").module;
    std::array<vk::PipelineShaderStageCreateInfo, 2> stage_info = {
        vk::PipelineShaderStageCreateInfo(
            {},
            vk::ShaderStageFlagBits::eVertex,
            vertex_shader,
            "VSMain",
            nullptr),
        vk::PipelineShaderStageCreateInfo(
            {},
            vk::ShaderStageFlagBits::eFragment,
            fragment_shader,
            "CopyImageInv",
            nullptr)};

    std::array<vk::DynamicState, 1> dynamic_states = {vk::DynamicState::eViewport};
    vk::PipelineDynamicStateCreateInfo dynamic_state_create_info(
        {},
        dynamic_states.size(),
        dynamic_states.data());

    // vk::Viewport viewport(
    // vk::PipelineViewportStateCreateInfo viewport_state_create_info({}, 1, &viewport);
    vk::PipelineRasterizationStateCreateInfo rasterization_state_create_info(
        {},
        {},
        VK_TRUE,
        vk::PolygonMode::eFill,
        {},
        vk::FrontFace::eCounterClockwise,
        VK_FALSE,
        0.f,
        0.f,
        0.f,
        1.f);
    vk::GraphicsPipelineCreateInfo create_info(
        {},
        stage_info,
        nullptr,
        nullptr,
        nullptr,
        nullptr, //&viewport_state_create_info,
        &rasterization_state_create_info,
        nullptr,
        nullptr,
        nullptr,
        &dynamic_state_create_info);

    copy_image_pipeline_ = device.vk().createGraphicsPipeline(nullptr, create_info);
  }

  vk::raii::Pipeline copy_image_pipeline_;
};

class ImGuiRenderPass : rndrx::noncopyable {
 public:
  ImGuiRenderPass(
      rndrx::vulkan::Window const& window,
      rndrx::vulkan::Application const& app,
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::Swapchain const& swapchain)
      : window_(window)
      , descriptors_(nullptr)
      , render_pass_(nullptr)
      , image_(nullptr)
      , image_memory_(nullptr)
      , image_view_(nullptr)
      , framebuffer_(nullptr) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    create_descriptor_pool(device);
    create_render_pass(device);
    create_image(device, window);

    ImGui_ImplGlfw_InitForVulkan(window.glfw(), true);

    // font_view_ = device_.srv_pool().allocate();
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = *app.vk_instance();
    init_info.PhysicalDevice = *app.selected_device();
    init_info.Device = *device.vk();
    init_info.Queue = *device.graphics_queue();
    init_info.CheckVkResultFn = &check_vk_result;
    init_info.DescriptorPool = *descriptors_;
    init_info.MinImageCount = 2;
    init_info.ImageCount = swapchain.num_images();

    ImGui_ImplVulkan_Init(&init_info, *render_pass_);
  }

  ~ImGuiRenderPass() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  void update() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void render(rndrx::vulkan::SubmissionContext& sc) {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    vk::CommandBuffer command_buffer = sc.command_buffer();
    rndrx::vulkan::RenderContext rc;
    rc.set_targets(window_.extents(), *image_view_);
    vk::ClearValue clear_value(vk::ClearColorValue{});
    vk::RenderPassBeginInfo begin_pass(
        *render_pass_,
        *framebuffer_,
        window_.extents(),
        1,
        &clear_value);
    command_buffer.beginRenderPass(begin_pass, vk::SubpassContents::eInline);
    ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
    command_buffer.endRenderPass();
  }

  void initialise_font(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::SubmissionContext& sc) {
    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    auto command_buffer = sc.command_buffer();
    command_buffer.begin(begin_info);
    ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
    command_buffer.end();

    vk::SubmitInfo submit_info({}, {}, {}, 1, &command_buffer);
    device.graphics_queue().submit(submit_info);
    device.vk().waitIdle();
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }

  // TargetableImage const& target() const { return target_; }

 private:
  static void check_vk_result(VkResult result) {
    if(static_cast<vk::Result>(result) != vk::Result::eSuccess) {
      rndrx::throw_runtime_error("ImGui Vulkan call failed.");
    }
  }

  void create_descriptor_pool(rndrx::vulkan::Device const& device) {
    std::array<vk::DescriptorPoolSize, 11> pool_sizes = {
        {{vk::DescriptorType::eSampler, 1000},
         {vk::DescriptorType::eCombinedImageSampler, 1000},
         {vk::DescriptorType::eSampledImage, 1000},
         {vk::DescriptorType::eStorageImage, 1000},
         {vk::DescriptorType::eUniformTexelBuffer, 1000},
         {vk::DescriptorType::eStorageTexelBuffer, 1000},
         {vk::DescriptorType::eUniformBuffer, 1000},
         {vk::DescriptorType::eStorageBuffer, 1000},
         {vk::DescriptorType::eUniformBufferDynamic, 1000},
         {vk::DescriptorType::eStorageBufferDynamic, 1000},
         {vk::DescriptorType::eInputAttachment, 1000}}};
    vk::DescriptorPoolCreateInfo create_info({}, 1000, pool_sizes);
    descriptors_ = vk::raii::DescriptorPool(device.vk(), create_info);
  }

  void create_image(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::Window const& window) {
    vk::ImageCreateInfo image_create_info(
        {},
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Unorm,
        vk::Extent3D(window.width(), window.height(), 1),
        1,
        1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment);

    image_ = device.vk().createImage(image_create_info);
    auto image_mem_reqs = image_.getMemoryRequirements();

    vk::MemoryAllocateInfo alloc_info(
        image_mem_reqs.size,
        device.find_memory_type(
            image_mem_reqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal));
    image_memory_ = device.vk().allocateMemory(alloc_info);
    image_.bindMemory(*image_memory_, 0);

    vk::ImageViewCreateInfo image_view_create_info(
        {},
        *image_,
        vk::ImageViewType::e2D,
        vk::Format::eR8G8B8A8Unorm,
        {},
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

    image_view_ = device.vk().createImageView(image_view_create_info);

    vk::FramebufferCreateInfo framebuffer_create_info(
        {},
        *render_pass_,
        1,
        &*image_view_,
        window.width(),
        window.height(),
        1);

    framebuffer_ = device.vk().createFramebuffer(framebuffer_create_info);
  }

  void create_render_pass(rndrx::vulkan::Device const& device) {
    vk::AttachmentDescription attachment_desc(
        {},
        vk::Format::eR8G8B8A8Unorm,
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eReadOnlyOptimal);
    vk::AttachmentReference attachment_ref(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDependency subpass_dep(
        VK_SUBPASS_EXTERNAL,
        0,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite);
    vk::SubpassDescription subpass(
        {},
        vk::PipelineBindPoint::eGraphics,
        0,
        nullptr,
        1,
        &attachment_ref);
    vk::RenderPassCreateInfo
        create_info({}, 1, &attachment_desc, 1, &subpass, 1, &subpass_dep);
    render_pass_ = device.vk().createRenderPass(create_info);
  }

  rndrx::vulkan::Window const& window_;
  glm::vec4 clear_colour_ = {0.f, 0.f, 0.f, 1.f};
  vk::raii::DescriptorPool descriptors_;
  vk::raii::RenderPass render_pass_;
  vk::raii::Image image_;
  vk::raii::DeviceMemory image_memory_;
  vk::raii::ImageView image_view_;
  vk::raii::Framebuffer framebuffer_;
};

static void glfw_error_callback(int error, const char* description) {
  std::cerr << "`Glfw Error " << error << ": " << description;
}

class Glfw : rndrx::noncopyable {
 public:
  Glfw() {
    glfwSetErrorCallback(glfw_error_callback);
    if(!glfwInit()) {
      rndrx::throw_runtime_error("Failed to initialise glfw");
    }

    if(!glfwVulkanSupported()) {
      rndrx::throw_runtime_error("Vulkan not supported in glfw.");
    }
  }

  ~Glfw() {
    glfwTerminate();
  }

 private:
};

bool rndrx::vulkan::Application::run() {
  Device device(*this);
  Swapchain swapchain(*this, device);
  ImGuiRenderPass imgui(window_, *this, device, swapchain);

  ShaderCache shaders = load_shaders(device);
  FinalCompositeRenderPass final_composite(device, shaders);

  std::array<SubmissionContext, 3> submission_contexts = {
      {SubmissionContext(device),
       SubmissionContext(device),
       SubmissionContext(device)}};

  imgui.initialise_font(device, submission_contexts[0]);

  // device.vk().waitIdle();
  std::uint32_t frame_id = 0;
  while(!glfwWindowShouldClose(window_.glfw())) {
    glfwPollEvents();

    imgui.update();

    ImGui::ShowDemoWindow();

    SubmissionContext& sc = submission_contexts[frame_id % submission_contexts.size()];
    sc.begin_rendering();
    imgui.render(sc);

    PresentContext present_context = swapchain.create_present_context(frame_id);
    Image final_image = present_context.acquire_next_image();

    // RenderContext composite_context;
    // composite_context.set_targets(window_.extents(), final_image.view());

    vk::ImageMemoryBarrier image_transition(
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eColorAttachmentRead,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR,
        device.graphics_queue_family_idx(),
        device.graphics_queue_family_idx(),
        final_image.image(),
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
    sc.command_buffer().pipelineBarrier(
        vk::PipelineStageFlagBits::eAllGraphics,
        vk::PipelineStageFlagBits::eAllGraphics,
        vk::DependencyFlagBits::eByRegion,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &image_transition);
    sc.finish_rendering();
    present_context.present();

    ++frame_id;
  }

  device.vk().waitIdle();

  return false;
}

int main(int, char**) {
  Glfw glfw;
  rndrx::vulkan::Window window;
  rndrx::vulkan::Application app(window);

  try {
    while(app.run())
      ;
  }
  catch(std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
