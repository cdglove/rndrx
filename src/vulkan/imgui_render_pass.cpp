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
#include "rndrx/vulkan/imgui_render_pass.hpp"

#include <array>
#include <memory>
#include <span>
#include <vulkan/vulkan.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "rndrx/throw_exception.hpp"
#include "rndrx/vulkan/application.hpp"
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/submission_context.hpp"
#include "rndrx/vulkan/swapchain.hpp"
#include "rndrx/vulkan/vma/allocator.hpp"
#include "rndrx/vulkan/vma/image.hpp"
#include "rndrx/vulkan/window.hpp"

namespace rndrx::vulkan {

ImGuiRenderPass::ImGuiRenderPass() = default;

ImGuiRenderPass::ImGuiRenderPass(
    Application const& app,
    Device& device,
    Swapchain const& swapchain)
    : draw_data_(std::make_unique<ImDrawData>()) {
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
  if(draw_data_) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
}

void ImGuiRenderPass::begin_frame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiRenderPass::end_frame() {
  ImGui::Render();
  for(int i = 0; i < draw_data_->CmdListsCount; ++i) {
    IM_FREE(draw_data_->CmdLists[i]);
  }

  ImDrawData* draw_data = ImGui::GetDrawData();
  *draw_data_ = *draw_data;
  draw_list_memory_.resize(draw_data->CmdListsCount);
  draw_data_->CmdLists = draw_list_memory_.data();
  for(int i = 0; i < draw_data->CmdListsCount; ++i) {
    draw_data_->CmdLists[i] = draw_data->CmdLists[i]->CloneOutput();
  }
}

ImGuiRenderPass::ImGuiRenderPass(ImGuiRenderPass&&) = default;
ImGuiRenderPass& ImGuiRenderPass::operator=(ImGuiRenderPass&&) = default;

void ImGuiRenderPass::render(SubmissionContext& sc) {
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
  ImGui_ImplVulkan_RenderDrawData(draw_data_.get(), command_buffer);
  command_buffer.endRenderPass();
}

void ImGuiRenderPass::create_fonts_texture(SubmissionContext& sc) {
  ImGui_ImplVulkan_CreateFontsTexture(sc.command_buffer());
}

void ImGuiRenderPass::finish_font_texture_creation() {
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
  image_ = device.allocator().create_image(
      vk::ImageCreateInfo()
          .setImageType(vk::ImageType::e2D)
          .setFormat(vk::Format::eR8G8B8A8Unorm)
          .setExtent(vk::Extent3D(window.width(), window.height(), 1))
          .setArrayLayers(1)
          .setMipLevels(1)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setTiling(vk::ImageTiling::eOptimal)
          .setUsage(
              vk::ImageUsageFlagBits::eColorAttachment |
              vk::ImageUsageFlagBits::eSampled));

  image_view_ = device.vk().createImageView(
      vk::ImageViewCreateInfo()
          .setImage(*image_.vk())
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(vk::Format::eR8G8B8A8Unorm)
          .setSubresourceRange(
              vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));

  framebuffer_ = device.vk().createFramebuffer( //
      vk::FramebufferCreateInfo()
          .setRenderPass(*render_pass_)
          .setAttachments(*image_view_)
          .setWidth(window.width())
          .setHeight(window.height())
          .setLayers(1));
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
