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
#include "rndrx/vulkan/texture.hpp"

#include <cmath>
#include "rndrx/throw_exception.hpp"
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"
#include "tiny_gltf.h"

namespace {
std::uint32_t compute_mip_level_count(std::uint32_t width, std::uint32_t height) {
  auto count = std::log2(std::max(width, height));
  return std::round(count);
}
} // namespace

namespace rndrx::vulkan {
Texture::Texture(Device& device, TextureCreateInfo const& create_info) {
  // cglover-todo: Support other formats.
  format_ = vk::Format::eB8G8R8A8Unorm;
  width_ = create_info.width;
  height_ = create_info.height;
  mip_count_ = compute_mip_level_count(width_, height_);

  auto const whole_image_resource = //
      vk::ImageSubresourceRange()
          .setAspectMask(vk::ImageAspectFlagBits::eColor)
          .setBaseMipLevel(0)
          .setLevelCount(1)
          .setBaseArrayLayer(0)
          .setLayerCount(1);

  image_ = device.allocator().create_image( //
      vk::ImageCreateInfo()
          .setImageType(vk::ImageType::e2D)
          .setFormat(format_)
          .setMipLevels(mip_count_)
          .setArrayLayers(1)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setTiling(vk::ImageTiling::eOptimal)
          .setUsage(
              vk::ImageUsageFlagBits::eTransferSrc |
              vk::ImageUsageFlagBits::eTransferDst | //
              vk::ImageUsageFlagBits::eSampled)
          .setSharingMode(vk::SharingMode::eExclusive)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setExtent(vk::Extent3D(width_, height_, 1)));

  image_view_ = device.vk().createImageView(
      vk::ImageViewCreateInfo()
          .setImage(*image_.vk())
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(format_)
          .setSubresourceRange(whole_image_resource));

  sampler_ = device.vk().createSampler(
      vk::SamplerCreateInfo()
          .setMagFilter(create_info.sampler.mag_filter)
          .setMinFilter(create_info.sampler.min_filter)
          .setMipmapMode(vk::SamplerMipmapMode::eLinear)
          .setAddressModeU(create_info.sampler.address_mode_u)
          .setAddressModeV(create_info.sampler.address_mode_v)
          .setAddressModeW(create_info.sampler.address_mode_w)
          .setCompareOp(vk::CompareOp::eNever)
          .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
          .setMinLod(0.f)
          .setMaxLod(VK_LOD_CLAMP_NONE)
          .setMaxAnisotropy(8.0f)
          .setAnisotropyEnable(VK_TRUE));

  vma::Buffer staging_buffer = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(width_ * height_ * 4)
          .setUsage(vk::BufferUsageFlagBits::eTransferSrc));

  if(create_info.component_count != 4) {
    std::uint32_t* texel_data = static_cast<std::uint32_t*>(
        staging_buffer.mapped_data());
    int component_idx = 0;
    for(int i = 0; i < height_; ++i) {
      for(int j = 0; j < width_; ++j) {
        std::uint32_t texel = 0;
        for(int k = 0; k < create_info.component_count; ++k) {
          texel <<= 8;
          texel |= create_info.image_data[component_idx++];
        }
        *texel_data++ = texel;
      }
    }
  }
  else {
    std::memcpy(
        staging_buffer.mapped_data(),
        create_info.image_data.data(),
        create_info.image_data.size());
  }

  vk::raii::CommandBuffer copy_cmd_buf = device.alloc_transfer_command_buffer();
  copy_cmd_buf.begin( //
      vk::CommandBufferBeginInfo().setFlags(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  copy_cmd_buf.pipelineBarrier(
      vk::PipelineStageFlagBits::eAllCommands,
      vk::PipelineStageFlagBits::eAllCommands,
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
          .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
          .setSrcBuffer(*staging_buffer.vk())
          .setRegions( //
              vk::BufferImageCopy2()
                  .setImageExtent( //
                      vk::Extent3D().setWidth(width_).setHeight(height_).setDepth(1))
                  .setImageSubresource(
                      vk::ImageSubresourceLayers()
                          .setAspectMask(vk::ImageAspectFlagBits::eColor)
                          .setBaseArrayLayer(0)
                          .setLayerCount(1)
                          .setMipLevel(0))));

  generate_mip_maps(device, copy_cmd_buf);

  copy_cmd_buf.end();

  vk::raii::Fence fence = device.vk().createFence({});

  device.transfer_queue().submit2(
      vk::SubmitInfo2() //
          .setCommandBufferInfos(
              vk::CommandBufferSubmitInfo().setCommandBuffer(*copy_cmd_buf)),
      *fence);

  vk::Result wait_result = device.vk().waitForFences(
      *fence,
      VK_TRUE,
      std::numeric_limits<std::uint64_t>::max());

  if(wait_result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to wait for fence.");
  }
}

void Texture::generate_mip_maps(Device& device, vk::raii::CommandBuffer& cmd_buf) {
  auto physical_device = device.physical_device();
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
    // Transition the previous mip to be transfer readable
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

    // Transition the current mip to be transfer targetable
    mip_barrier //
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eNone)
        .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
        .subresourceRange.setBaseMipLevel(i);

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
                         vk::Offset3D(mip_width, mip_height, 1)})
                    .setSrcSubresource(              //
                        vk::ImageSubresourceLayers() //
                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1)
                            .setMipLevel(i - 1))
                    .setDstOffsets(
                        {vk::Offset3D(0, 0, 0),
                         vk::Offset3D(mip_width / 2, mip_height / 2, 1)})
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
        .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
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
      .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
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
  descriptor_.imageView = *image_view_;
  descriptor_.imageLayout = image_layout_;
}

} // namespace rndrx::vulkan