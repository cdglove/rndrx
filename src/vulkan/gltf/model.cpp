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
#include "model.hpp"
#include <vulkan/vulkan_core.h>
#include <ranges>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "../device.hpp"
#include "../vma/buffer.hpp"

#include "rndrx/throw_exception.hpp"
#include "tiny_gltf.h"

// ERROR is already defined in wingdi.h and collides with a define in the Draco headers
#if defined(_WIN32) && defined(ERROR) && defined(TINYGLTF_ENABLE_DRACO)
#  undef ERROR
#  pragma message("ERROR constant already defined, undefining")
#endif

namespace {
std::uint32_t compute_mip_level_count(std::uint32_t width, std::uint32_t height) {
  auto count = std::log2(std::max(width, height));
  return std::round(count);
}
} // namespace

namespace rndrx::vulkan::gltf {
BoundingBox BoundingBox::get_aabb(glm::mat4 const& aligned_to) const {
  glm::vec3 aabb_min = glm::vec3(aligned_to[3]);
  glm::vec3 aabb_max = aabb_min;
  glm::vec3 v0, v1;

  glm::vec3 right = glm::vec3(aligned_to[0]);
  v0 = right * min.x;
  v1 = right * max.x;
  aabb_min += glm::min(v0, v1);
  aabb_max += glm::max(v0, v1);

  glm::vec3 up = glm::vec3(aligned_to[1]);
  v0 = up * min.y;
  v1 = up * max.y;
  aabb_min += glm::min(v0, v1);
  aabb_max += glm::max(v0, v1);

  glm::vec3 back = glm::vec3(aligned_to[2]);
  v0 = back * min.z;
  v1 = back * max.z;
  aabb_min += glm::min(v0, v1);
  aabb_max += glm::max(v0, v1);

  return {aabb_min, aabb_max};
}

Texture::Texture(Device& device, tinygltf::Image const& image_data, Texture const& sampler)
    : device_(&device) {
  // cglover-todo: Support other formats.
  format_ = vk::Format::eB8G8R8A8Unorm;
  width_ = image_data.width;
  height_ = image_data.height;
  mip_count_ = compute_mip_level_count(width_, height_);

  vma::Buffer staging_buffer( //
      device.allocator(),
      vk::BufferCreateInfo()
          .setSize(image_data.width * image_data.height * 4)
          .setUsage(vk::BufferUsageFlagBits::eTransferSrc));

  if(image_data.component != 4) {
    std::uint32_t* texel_data = static_cast<std::uint32_t*>(
        staging_buffer.mapped_data());
    int component_idx = 0;
    for(int i = 0; i < image_data.height; ++i) {
      for(int j = 0; j < image_data.width; ++j) {
        std::uint32_t texel = 0;
        for(int k = 0; k < image_data.component; ++k) {
          texel <<= 8;
          texel |= image_data.image[component_idx++];
        }
        *texel_data++ = texel;
      }
    }
  }
  else {
    std::memcpy(
        staging_buffer.mapped_data(),
        image_data.image.data(),
        image_data.image.size());
  }

  image_ = device_->allocator().createImage( //
      vk::ImageCreateInfo()
          .setImageType(vk::ImageType::e2D)
          .setFormat(format_)
          .setMipLevels(mip_count_)
          .setArrayLayers(1)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setTiling(vk::ImageTiling::eOptimal)
          .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
          .setSharingMode(vk::SharingMode::eExclusive)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setExtent(vk::Extent3D(width_, height_, 1)));

  auto const whole_image_resource = //
      vk::ImageSubresourceRange()
          .setAspectMask(vk::ImageAspectFlagBits::eColor)
          .setBaseMipLevel(0)
          .setLevelCount(1)
          .setBaseArrayLayer(0)
          .setLayerCount(1);

  vk::raii::CommandBuffer copy_cmd_buf = device.alloc_transfer_command_buffer();
  copy_cmd_buf.begin(vk::CommandBufferBeginInfo().setFlags(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  copy_cmd_buf.pipelineBarrier(
      vk::PipelineStageFlagBits::eTopOfPipe,
      vk::PipelineStageFlagBits::eTransfer,
      {},
      nullptr,
      nullptr,
      vk::ImageMemoryBarrier()
          .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
          .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
          .setSubresourceRange(whole_image_resource)
          .setOldLayout(vk::ImageLayout::eUndefined)
          .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
          .setImage(*image_.vk())
          .setSrcAccessMask(vk::AccessFlagBits::eNone)
          .setDstAccessMask(vk::AccessFlagBits::eTransferWrite));

  copy_cmd_buf.copyBufferToImage2(
      vk::CopyBufferToImageInfo2()
          .setDstImage(*image_.vk())
          .setSrcBuffer(*staging_buffer.vk())
          .setRegions( //
              vk::BufferImageCopy2()
                  .setImageExtent( //
                      vk::Extent3D()
                          .setWidth(image_data.width)
                          .setHeight(image_data.height)
                          .setDepth(1))
                  .setImageSubresource(
                      vk::ImageSubresourceLayers()
                          .setAspectMask(vk::ImageAspectFlagBits::eColor)
                          .setBaseArrayLayer(0)
                          .setLayerCount(1)
                          .setMipLevel(0))));

  copy_cmd_buf.pipelineBarrier(
      vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eBottomOfPipe,
      {},
      nullptr,
      nullptr,
      vk::ImageMemoryBarrier()
          .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
          .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
          .setSubresourceRange(whole_image_resource)
          .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
          .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
          .setImage(*image_.vk())
          .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
          .setDstAccessMask(vk::AccessFlagBits::eShaderRead));

  image_view_ = device.vk().createImageView(
      vk::ImageViewCreateInfo()
          .setImage(*image_.vk())
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(format_)
          .setSubresourceRange(whole_image_resource));

  generate_mip_maps(copy_cmd_buf);

  device.transfer_queue().submit2(vk::SubmitInfo2().setCommandBufferInfos(
      vk::CommandBufferSubmitInfo().setCommandBuffer(*copy_cmd_buf)));
}

void Texture::generate_mip_maps(vk::raii::CommandBuffer& cmd_buf) {
  auto physical_device = device_->physical_device();
  vk::FormatProperties props = physical_device.getFormatProperties(format_);
  if(!(props.optimalTilingFeatures &
       vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
    throw_runtime_error(
        "Texture image format does not support linear blitting!");
  }

  std::uint32_t mip_width = width_;
  std::uint32_t mip_height = height_;

  auto mip_barrier = //
      vk::ImageMemoryBarrier()
          .setImage(*image_.vk())
          .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
          .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
          .setSubresourceRange( //
              vk::ImageSubresourceRange()
                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                  .setBaseMipLevel(0)
                  .setLevelCount(1)
                  .setBaseArrayLayer(0)
                  .setLayerCount(1));

  for(int i = 1; i < mip_count_; ++i) {
    // Transition the previous be transfer readable
    mip_barrier //
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
        .subresourceRange.setBaseMipLevel(i - 1);

    cmd_buf.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        nullptr,
        nullptr,
        mip_barrier);

    // Copy previous mip to current mip
    cmd_buf.blitImage2(
        vk::BlitImageInfo2() //
            .setFilter(vk::Filter::eLinear)
            .setSrcImage(*image_.vk())
            .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setDstImage(*image_.vk())
            .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
            .setRegions(         //
                vk::ImageBlit2() //
                    .setSrcOffsets(
                        {vk::Offset3D(0, 0, 0),
                         vk::Offset3D(mip_width, mip_height, 0)})
                    .setSrcSubresource(              //
                        vk::ImageSubresourceLayers() //
                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1)
                            .setMipLevel(i - 1))
                    .setDstOffsets(
                        {vk::Offset3D(0, 0, 0),
                         vk::Offset3D(mip_width / 2, mip_height / 2, 0)})
                    .setDstSubresource(
                        vk::ImageSubresourceLayers()
                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1)
                            .setMipLevel(i))));

    // We're done with previous mip, transition to shader readable
    mip_barrier //
        .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .subresourceRange.setBaseMipLevel(i - 1);

    cmd_buf.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader,
        {},
        nullptr,
        nullptr,
        mip_barrier);

    if(mip_width >= 2) {
      mip_width /= 2;
    }

    if(mip_height >= 2) {
      mip_height /= 2;
    }
  }

  // Transition the final mip to shader readable
  mip_barrier //
      .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
      .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
      .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
      .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
      .subresourceRange.setBaseMipLevel(mip_count_ - 1);

  cmd_buf.pipelineBarrier(
      vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eFragmentShader,
      {},
      nullptr,
      nullptr,
      mip_barrier);
}

void Texture::update_descriptor() {
  descriptor_.sampler = *sampler_;
  descriptor_.imageView = *view_;
  descriptor_.imageLayout = image_layout_;
}

Primitive::Primitive(
    std::uint32_t first_index,
    std::uint32_t index_count,
    std::uint32_t vertex_count,
    Material& material)
    : first_index(first_index)
    , index_count(index_count)
    , vertex_count(vertex_count)
    , material(material) {
  has_indices = index_count > 0;
};

void Primitive::set_bounding_box(glm::vec3 min, glm::vec3 max) {
  bb.min = min;
  bb.max = max;
  bb.valid = true;
}

Mesh::Mesh(Device& device, glm::mat4 matrix) : device(&device){
    this->uniform_block.matrix = matrix;
    
    VK_CHECK_RESULT(device->createBuffer(
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sizeof(uniform_block),
        &uniform_buffer.buffer,
        &uniform_buffer.memory,
        &uniform_block));
    VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, uniform_buffer.memory, 0, sizeof(uniform_block), 0, &uniform_buffer.mapped));
    uniform_buffer.descriptor = { uniform_buffer.buffer, 0, sizeof(uniform_block) };
};

Mesh::~Mesh() {
    vkDestroyBuffer(device->logicalDevice, uniform_buffer.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, uniform_buffer.memory, nullptr);
    for (Primitive* p : primitives)
        delete p;
}

void Mesh::set_bounding_box(glm::vec3 min, glm::vec3 max) {
    bb.min = min;
    bb.max = max;
    bb.valid = true;
}


} // namespace rndrx::vulkan::gltf