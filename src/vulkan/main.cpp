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
#include <limits>
#include <memory>
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

#define TINYOBJLOADER_IMPLEMENTATION 1
#define STB_IMAGE_IMPLEMENTATION     1
#include <stb_image.h>
#include <tiny_obj_loader.h>

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

rndrx::vulkan::ShaderCache load_shaders(rndrx::vulkan::Device& device) {
  rndrx::vulkan::ShaderCache cache;
  rndrx::vulkan::ShaderLoader loader(device, cache);
  loader.load("fullscreen_quad.vsmain");
  loader.load("fullscreen_quad.copyimageopaque");
  loader.load("fullscreen_quad.blendimageinv");
  loader.load("fullscreen_quad.blendimage");
  loader.load("static_model.vsmain");
  loader.load("static_model.phong");
  return cache;
}

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
    vk::Viewport viewport = rc.full_viewport();
    command_buffer().setViewport(0, 1, &viewport);
    auto full_extent = rc.extents();
    command_buffer().setScissor(0, 1, &full_extent);

    vk::ClearValue clear_value;
    clear_value.color.setFloat32({0, 1, 1, 0});
    vk::RenderingAttachmentInfo colour_info;
    colour_info //
        .setImageView(rc.colour_target())
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setClearValue(clear_value)
        .setResolveMode(vk::ResolveModeFlagBits::eNone);

    vk::RenderingInfo rendering_info;
    rendering_info //
        .setLayerCount(1)
        .setViewMask(0)
        .setColorAttachments(colour_info)
        .setRenderArea(rc.extents());

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
    submit_semaphore_ = device.vk().createSemaphore(semaphore_create_info);
    vk::FenceCreateInfo fence_create_info(vk::FenceCreateFlagBits::eSignaled);
    submit_fence_ = device.vk().createFence(fence_create_info);
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
  class RenderContext;
  FinalCompositeRenderPass(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::ShaderCache const& sc)
      : copy_image_pipeline_(nullptr) {
    create_render_pass(device);
    create_pipeline_layout(device);
    create_pipeline(device, sc);
  }

 private:
  void create_render_pass(rndrx::vulkan::Device const& device) {
    vk::AttachmentDescription attachment_desc[1];
    attachment_desc[0] //
        .setFormat(vk::Format::eB8G8R8A8Unorm)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentReference attachment_ref[1];
    attachment_ref[0] //
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    vk::SubpassDescription subpass[1];
    subpass[0] //
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(attachment_ref);

    vk::RenderPassCreateInfo create_info;
    create_info //
        .setAttachments(attachment_desc)
        .setSubpasses(subpass);

    render_pass_ = device.vk().createRenderPass(create_info);
  }

  void create_pipeline_layout(rndrx::vulkan::Device const& device) {
    vk::SamplerCreateInfo sampler_create_info;
    sampler_ = device.vk().createSampler(sampler_create_info);

    vk::DescriptorSetLayoutBinding sampler_binding;
    sampler_binding //
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setDescriptorCount(1)
        .setImmutableSamplers(*sampler_);

    vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
    descriptor_set_layout_create_info.setBindings(sampler_binding);

    fs_layout_ = device.vk().createDescriptorSetLayout(
        descriptor_set_layout_create_info);

    vk::PipelineLayoutCreateInfo layout_create_info;
    layout_create_info.setSetLayouts(*fs_layout_);

    pipeline_layout_ = device.vk().createPipelineLayout(layout_create_info);
  }

  void create_pipeline(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::ShaderCache const& sc) {
    std::array<vk::PipelineShaderStageCreateInfo, 2> stage_info;
    stage_info[0] //
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*sc.get("fullscreen_quad.vsmain").module)
        .setPName("main");
    stage_info[1] //
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*sc.get("fullscreen_quad.blendimage").module)
        .setPName("main");

    std::array<vk::DynamicState, 2> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dynamic_state_create_info;
    dynamic_state_create_info.setDynamicStates(dynamic_states);

    vk::PipelineVertexInputStateCreateInfo vertex_input_state_create_info;
    vertex_input_state_create_info.setVertexBindingDescriptionCount(0);

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state_create_info;
    input_assembly_state_create_info.setTopology(
        vk::PrimitiveTopology::eTriangleStrip);

    vk::PipelineViewportStateCreateInfo viewport_state_create_info;
    viewport_state_create_info //
        .setViewportCount(1)
        .setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rasterization_state_create_info;
    rasterization_state_create_info //
        .setPolygonMode(vk::PolygonMode::eFill)
        .setFrontFace(vk::FrontFace::eClockwise)
        .setLineWidth(1.f);

    vk::PipelineColorBlendAttachmentState colour_blend_attachment_state;
    colour_blend_attachment_state //
        .setBlendEnable(VK_TRUE)
        .setColorWriteMask(
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
        .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
        .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
        .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
        .setAlphaBlendOp(vk::BlendOp::eAdd);

    vk::PipelineColorBlendStateCreateInfo colour_blend_state_create_info;
    colour_blend_state_create_info //
        .setLogicOpEnable(VK_FALSE)
        .setAttachments(colour_blend_attachment_state);

    vk::GraphicsPipelineCreateInfo create_info;
    create_info //
        .setStages(stage_info)
        .setPVertexInputState(&vertex_input_state_create_info)
        .setPInputAssemblyState(&input_assembly_state_create_info)
        .setPRasterizationState(&rasterization_state_create_info)
        .setPColorBlendState(&colour_blend_state_create_info)
        .setPViewportState(&viewport_state_create_info)
        .setPDynamicState(&dynamic_state_create_info)
        .setLayout(*pipeline_layout_)
        .setRenderPass(*render_pass_);

    copy_image_pipeline_ = device.vk().createGraphicsPipeline(nullptr, create_info);
  }

  vk::raii::Sampler sampler_ = nullptr;
  vk::raii::DescriptorSetLayout fs_layout_ = nullptr;
  vk::raii::PipelineLayout pipeline_layout_ = nullptr;
  vk::raii::RenderPass render_pass_ = nullptr;
  vk::raii::Pipeline copy_image_pipeline_ = nullptr;
};

class ImGuiRenderPass : rndrx::noncopyable {
 public:
  ImGuiRenderPass(
      rndrx::vulkan::Window const& window,
      rndrx::vulkan::Application const& app,
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::Swapchain const& swapchain)
      : window_(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().Alpha = 0.9f;

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
    init_info.DescriptorPool = *descriptor_pool_;
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

    vk::ClearValue clear_value;
    clear_value.color.setFloat32({0, 0, 0, 0});

    vk::RenderPassBeginInfo begin_pass;
    begin_pass //
        .setRenderPass(*render_pass_)
        .setFramebuffer(*framebuffer_)
        .setRenderArea(window_.extents())
        .setClearValues(clear_value);
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

  rndrx::vulkan::RenderTarget target() const {
    return {*image_, *image_view_, *framebuffer_};
  }

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

    vk::DescriptorPoolCreateInfo create_info;
    create_info //
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setMaxSets(1000)
        .setPoolSizes(pool_sizes);
    descriptor_pool_ = device.vk().createDescriptorPool(create_info);
  }

  void create_image(
      rndrx::vulkan::Device const& device,
      rndrx::vulkan::Window const& window) {
    vk::ImageCreateInfo image_create_info;
    image_create_info //
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setExtent(vk::Extent3D(window.width(), window.height(), 1))
        .setArrayLayers(1)
        .setMipLevels(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(
            vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eSampled);

    image_ = device.vk().createImage(image_create_info);
    auto image_mem_reqs = image_.getMemoryRequirements();

    vk::MemoryAllocateInfo alloc_info;
    alloc_info //
        .setAllocationSize(image_mem_reqs.size)
        .setMemoryTypeIndex(device.find_memory_type(
            image_mem_reqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal));

    image_memory_ = device.vk().allocateMemory(alloc_info);
    image_.bindMemory(*image_memory_, 0);

    vk::ImageViewCreateInfo image_view_create_info;
    image_view_create_info //
        .setImage(*image_)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSubresourceRange(
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

    image_view_ = device.vk().createImageView(image_view_create_info);

    vk::FramebufferCreateInfo framebuffer_create_info;
    framebuffer_create_info //
        .setRenderPass(*render_pass_)
        .setAttachments(*image_view_)
        .setWidth(window.width())
        .setHeight(window.height())
        .setLayers(1);

    framebuffer_ = device.vk().createFramebuffer(framebuffer_create_info);
  }

  void create_render_pass(rndrx::vulkan::Device const& device) {
    vk::AttachmentDescription attachment_desc[1];
    attachment_desc[0] //
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eReadOnlyOptimal);

    vk::AttachmentReference attachment_ref[1];
    attachment_ref[0] //
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    vk::SubpassDescription subpass;
    subpass //
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(attachment_ref);

    vk::RenderPassCreateInfo create_info;
    create_info //
        .setAttachments(attachment_desc)
        .setSubpasses(subpass);

    render_pass_ = device.vk().createRenderPass(create_info);
  }

  rndrx::vulkan::Window const& window_;
  glm::vec4 clear_colour_ = {0.f, 0.f, 0.f, 1.f};
  vk::raii::DescriptorPool descriptor_pool_ = nullptr;
  vk::raii::RenderPass render_pass_ = nullptr;
  vk::raii::Image image_ = nullptr;
  vk::raii::DeviceMemory image_memory_ = nullptr;
  vk::raii::ImageView image_view_ = nullptr;
  vk::raii::Framebuffer framebuffer_ = nullptr;
};

class FinalCompositeRenderPass::RenderContext : rndrx::noncopyable {
 public:
  RenderContext(
      rndrx::vulkan::Device& device,
      FinalCompositeRenderPass const& fc_rp,
      ImGuiRenderPass const& imgui_rp) {
    create_descriptor_set(device, fc_rp);
    update_descriptor_set(device, fc_rp, imgui_rp);
  }

  void draw(
      FinalCompositeRenderPass const& pass,
      rndrx::vulkan::RenderContext& rc,
      rndrx::vulkan::SubmissionContext& sc) {
    auto&& cb = sc.command_buffer();

    vk::ClearValue clear_value;
    clear_value.color.setFloat32({0, 1, 1, 0});

    vk::RenderPassBeginInfo begin_pass;
    begin_pass //
        .setRenderPass(*pass.render_pass_)
        .setFramebuffer(rc.framebuffer())
        .setRenderArea(rc.extents())
        .setClearValues(clear_value);

    sc.command_buffer().beginRenderPass(begin_pass, vk::SubpassContents::eInline);

    // sc.begin_pass(rc);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *pass.copy_image_pipeline_);
    cb.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pass.pipeline_layout_,
        0,
        1,
        &*descriptor_set_,
        0,
        nullptr);
    sc.command_buffer().draw(3, 1, 0, 0);
    sc.command_buffer().endRenderPass();
    // sc.end_pass();
  }

 private:
  void create_descriptor_set(
      rndrx::vulkan::Device& device,
      FinalCompositeRenderPass const& rp) {
    vk::DescriptorSetAllocateInfo alloc_info(device.descriptor_pool(), 1, &*rp.fs_layout_);
    auto v = device.vk().allocateDescriptorSets(alloc_info);
    descriptor_set_ = std::move(v[0]);
  }

  void update_descriptor_set(
      rndrx::vulkan::Device& device,
      FinalCompositeRenderPass const& fc_rp,
      ImGuiRenderPass const& imgui_rp) {
    vk::DescriptorImageInfo image_info(
        *fc_rp.sampler_,
        imgui_rp.target().view(),
        vk::ImageLayout::eReadOnlyOptimal);
    std::array<vk::WriteDescriptorSet, 1> write = {vk::WriteDescriptorSet(
        *descriptor_set_,
        0,
        0,
        1,
        vk::DescriptorType::eCombinedImageSampler,
        &image_info)};
    device.vk().updateDescriptorSets(write, {});
  }

  vk::raii::DescriptorSet descriptor_set_ = nullptr;
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
  LOG(Info) << "Compatible adapters:";
  for(auto&& device : physical_devices()) {
    auto properties = device.getProperties();
    LOG(Info) << "    " << properties.deviceName
              << ((*selected_device() == *device) ? " (selected)" : "");
  }

  Device device(*this);
  Swapchain swapchain(*this, device);
  ImGuiRenderPass imgui(window_, *this, device, swapchain);

  ShaderCache shaders = load_shaders(device);
  FinalCompositeRenderPass final_composite(device, shaders);
  FinalCompositeRenderPass::RenderContext fcrprc(device, final_composite, imgui);

  std::array<SubmissionContext, 3> submission_contexts = {
      {SubmissionContext(device),
       SubmissionContext(device),
       SubmissionContext(device)}};

  imgui.initialise_font(device, submission_contexts[0]);

  std::uint32_t frame_id = 0;
  while(!glfwWindowShouldClose(window_.glfw())) {
    glfwPollEvents();

    imgui.update();

    // ImGui::ShowDemoWindow();
    if(ImGui::Begin("Adapter Info")) {
      auto const& selected = selected_device();
      auto selected_properties = selected.getProperties();
      if(ImGui::BeginCombo("##name", selected_properties.deviceName)) {
        for(auto&& candidate : physical_devices()) {
          auto candidate_properties = candidate.getProperties();
          if(ImGui::Selectable(
                 candidate_properties.deviceName,
                 *candidate == *selected)) {
            if(*candidate != *selected) {
              select_device(candidate);
              LOG(Info) << "Adapter switch from '"
                        << selected_properties.deviceName << "' to '"
                        << candidate_properties.deviceName << "' detected.\n";
              device.vk().waitIdle();
              //  Return true unwinds the stack, cleaning everything up, and
              //  then calls run again.
              return true;
            }
          }
        }
        ImGui::EndCombo();
      }
      ImGui::End();
    }

    SubmissionContext& sc = submission_contexts[frame_id % submission_contexts.size()];
    sc.begin_rendering();
    imgui.render(sc);

    PresentContext present_context = swapchain.create_present_context(frame_id);
    RenderTarget final_image = present_context.acquire_next_image();
    vk::ImageMemoryBarrier swap_chain_image_transition;
    // swap_chain_image_transition //
    //     .setSrcQueueFamilyIndex(device.graphics_queue_family_idx())
    //     .setDstQueueFamilyIndex(device.graphics_queue_family_idx())
    //     .setImage(final_image.image())
    //     .setSubresourceRange(
    //         vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

    // swap_chain_image_transition //
    //     .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentRead)
    //     .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
    //     .setOldLayout(vk::ImageLayout::eUndefined)
    //     .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal);

    // sc.command_buffer().pipelineBarrier(
    //     vk::PipelineStageFlagBits::eAllGraphics,
    //     vk::PipelineStageFlagBits::eAllGraphics,
    //     vk::DependencyFlagBits::eByRegion,
    //     0,
    //     nullptr,
    //     0,
    //     nullptr,
    //     1,
    //     &swap_chain_image_transition);

    RenderContext composite_context;
    composite_context.set_targets(
        window_.extents(),
        final_image.view(),
        final_image.framebuffer());

    fcrprc.draw(final_composite, composite_context, sc);

    // swap_chain_image_transition //
    //     .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
    //     .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead)
    //     .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
    //     .setNewLayout(vk::ImageLayout::ePresentSrcKHR);

    // sc.command_buffer().pipelineBarrier(
    //     vk::PipelineStageFlagBits::eAllGraphics,
    //     vk::PipelineStageFlagBits::eAllGraphics,
    //     vk::DependencyFlagBits::eByRegion,
    //     0,
    //     nullptr,
    //     0,
    //     nullptr,
    //     1,
    //     &swap_chain_image_transition);
    sc.finish_rendering();
    present_context.present();

    ++frame_id;
  }

  device.vk().waitIdle();

  return false;
}

void choose_graphics_device(rndrx::vulkan::Application& app) {
  auto devices = app.physical_devices();
  for(auto&& device : app.physical_devices()) {
    auto properties = device.getProperties();
    if(properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
      app.select_device(device);
    }
  }
}

int main(int, char**) {
  Glfw glfw;
  rndrx::vulkan::Window window;
  rndrx::vulkan::Application app(window);
  choose_graphics_device(app);

  try {
    while(app.run())
      ;
  }
  catch(std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
