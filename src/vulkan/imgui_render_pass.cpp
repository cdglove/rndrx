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
#include "imgui_render_pass.hpp"

#include "application.hpp"
#include "device.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "rndrx/throw_exception.hpp"
#include "submission_context.hpp"

namespace rndrx::vulkan {

ImGuiRenderPass::ImGuiRenderPass(
    Application const& app,
    Device& device,
    Swapchain const& swapchain) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetStyle().Alpha = 0.9f;

  create_descriptor_pool(device);
  create_render_pass(device);
  create_image(device, app.window());

  ImGui_ImplGlfw_InitForVulkan(app.window().glfw(), true);

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = *app.vk_instance();
  init_info.PhysicalDevice = *app.selected_device();
  init_info.Device = *device.vk();
  init_info.Queue = *device.graphics_queue();
  init_info.CheckVkResultFn = &check_vk_result;
  init_info.DescriptorPool = *descriptor_pool_;
  init_info.MinImageCount = 2;
  init_info.ImageCount = swapchain.images().size();

  ImGui_ImplVulkan_Init(&init_info, *render_pass_);
}

ImGuiRenderPass::~ImGuiRenderPass() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void ImGuiRenderPass::update() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiRenderPass::render(SubmissionContext& sc) {
  ImGui::Render();
  ImDrawData* draw_data = ImGui::GetDrawData();
  vk::CommandBuffer command_buffer = sc.command_buffer();

  vk::ClearValue clear_value;
  clear_value.color.setFloat32({0, 0, 0, 0});

  vk::RenderPassBeginInfo begin_pass;
  begin_pass //
      .setRenderPass(*render_pass_)
      .setFramebuffer(*framebuffer_)
      .setRenderArea(sc.render_extents())
      .setClearValues(clear_value);
  command_buffer.beginRenderPass(begin_pass, vk::SubpassContents::eInline);
  ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
  command_buffer.endRenderPass();
}

void ImGuiRenderPass::initialise_font(Device const& device, SubmissionContext& sc) {
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

void ImGuiRenderPass::check_vk_result(VkResult result) {
  if(static_cast<vk::Result>(result) != vk::Result::eSuccess) {
    rndrx::throw_runtime_error("ImGui Vulkan call failed.");
  }
}

void ImGuiRenderPass::create_descriptor_pool(Device const& device) {
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

void ImGuiRenderPass::create_image(Device& device, Window const& window) {
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

  image_ = device.allocator().createImage(image_create_info);

  vk::ImageViewCreateInfo image_view_create_info;
  image_view_create_info //
      .setImage(*image_.vk())
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

void ImGuiRenderPass::create_render_pass(Device const& device) {
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

} // namespace rndrx::vulkan