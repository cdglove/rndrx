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
#include "rndrx/vulkan/composite_render_pass.hpp"

#include <vulkan/vulkan_core.h>
#include <array>
#include <type_traits>
#include <vector>
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/render_context.hpp"
#include "rndrx/vulkan/shader_cache.hpp"
#include "rndrx/vulkan/submission_context.hpp"

namespace rndrx::vulkan {

void CompositeRenderPass::render(
    RenderContext& rc,
    SubmissionContext& sc,
    std::span<DrawItem> draw_list) {
  auto&& cb = sc.command_buffer();

  vk::ClearValue clear_value;
  clear_value.color.setFloat32({0, 1, 1, 0});

  sc.command_buffer().beginRenderPass(
      vk::RenderPassBeginInfo()
          .setRenderPass(*render_pass_)
          .setFramebuffer(rc.framebuffer())
          .setRenderArea(rc.extents())
          .setClearValues(clear_value),
      vk::SubpassContents::eInline);

  cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *copy_image_pipeline_);

  for(auto&& item : draw_list) {
    item.draw(*this, sc);
  }

  sc.command_buffer().endRenderPass();
}

void CompositeRenderPass::create_render_pass(Device const& device, vk::Format present_format) {
  vk::AttachmentDescription attachment_desc;
  attachment_desc //
      .setFormat(present_format)
      .setSamples(vk::SampleCountFlagBits::e1)
      .setLoadOp(vk::AttachmentLoadOp::eClear)
      .setStoreOp(vk::AttachmentStoreOp::eStore)
      .setInitialLayout(vk::ImageLayout::eUndefined)
      .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

  vk::AttachmentReference attachment_ref;
  attachment_ref //
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

void CompositeRenderPass::create_pipeline_layout(Device const& device) {
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

  descriptor_layout_ = device.vk().createDescriptorSetLayout(
      descriptor_set_layout_create_info);

  vk::PipelineLayoutCreateInfo layout_create_info;
  layout_create_info.setSetLayouts(*descriptor_layout_);

  pipeline_layout_ = device.vk().createPipelineLayout(layout_create_info);
}

void CompositeRenderPass::create_pipeline(Device const& device, ShaderCache const& sc) {
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

void CompositeRenderPass::DrawItem::draw(
    CompositeRenderPass const& pass,
    SubmissionContext& sc) {
  auto&& cb = sc.command_buffer();
  cb.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      *pass.pipeline_layout_,
      0,
      1,
      &*descriptor_set_,
      0,
      nullptr);
  sc.command_buffer().draw(3, 1, 0, 0);
}

void CompositeRenderPass::DrawItem::create_descriptor_set(
    Device& device,
    CompositeRenderPass const& parent_pass) {
  vk::DescriptorSetAllocateInfo alloc_info;
  alloc_info //
      .setDescriptorPool(device.descriptor_pool())
      .setSetLayouts(*parent_pass.descriptor_layout_);

  auto v = device.vk().allocateDescriptorSets(alloc_info);
  descriptor_set_ = std::move(v[0]);
}

void CompositeRenderPass::DrawItem::update_descriptor_set(
    Device& device,
    CompositeRenderPass const& parent_pass,
    vk::ImageView source) {
  vk::DescriptorImageInfo image_info;
  image_info //
      .setSampler(*parent_pass.sampler_)
      .setImageView(source)
      .setImageLayout(vk::ImageLayout::eReadOnlyOptimal);

  vk::WriteDescriptorSet write;
  write //
      .setDstSet(*descriptor_set_)
      .setDstBinding(0)
      .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
      .setImageInfo(image_info);

  device.vk().updateDescriptorSets(write, {});
}

} // namespace rndrx::vulkan