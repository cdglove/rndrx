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
#ifndef RNDRX_VULKAN_SWAPCHAIN_HPP_
#define RNDRX_VULKAN_SWAPCHAIN_HPP_
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "rndrx/noncopyable.hpp"
#include "image.hpp"

namespace rndrx::vulkan {

class PresentContext : noncopyable {
 public:
  PresentContext(
      vk::raii::SwapchainKHR const& swapchain,
      std::vector<VkImage> swapchain_images,
      std::vector<vk::raii::ImageView> const& swapchain_image_views,
      vk::raii::Queue const& present_queue,
      vk::Semaphore image_ready_semaphore)
      : swapchain_(swapchain)
      , swapchain_images_(swapchain_images)
      , swapchain_image_views_(swapchain_image_views)
      , present_queue_(present_queue)
      , image_ready_semaphore_(image_ready_semaphore) {
  }

  Image acquire_next_image() {
    auto result = swapchain_.acquireNextImage(
        std::numeric_limits<std::uint64_t>::max(),
        image_ready_semaphore_);
    if(result.first != vk::Result::eSuccess) {
      throw_runtime_error("Failed to handle swapchain acquire failure");
    }

    image_idx_ = result.second;

    return {
        vk::Image(swapchain_images_[image_idx_]),
        *swapchain_image_views_[image_idx_]};
  }

  void present() {
    vk::PresentInfoKHR present_info(0, nullptr, 1, &*swapchain_, &image_idx_);
    auto result = present_queue_.presentKHR(present_info);
    if(result != vk::Result::eSuccess) {
      throw_runtime_error("Failed to handle swapchain present failure");
    }
  }

 private:
  vk::raii::SwapchainKHR const& swapchain_;
  std::vector<VkImage> swapchain_images_;
  std::vector<vk::raii::ImageView> const& swapchain_image_views_;
  vk::raii::Queue const& present_queue_;
  vk::Semaphore image_ready_semaphore_;
  std::uint32_t image_idx_ = 0;
};

class Swapchain : noncopyable {
 public:
  Swapchain(Application const& app, Device& device)
      : device_(device)
      , swapchain_(nullptr)
      , composite_render_pass_(nullptr) {
    create_swapchain(app);
    create_sync_objects();
  }

  PresentContext create_present_context(std::uint32_t frame_id) {
    auto frame_idx = frame_id % image_ready_semaphores_.size();
    return {
        swapchain_,
        swapchain_images_,
        swapchain_image_views_,
        device_.graphics_queue(),
        *image_ready_semaphores_[frame_idx]};
  }

  vk::SurfaceFormatKHR surface_format() const {
    return surface_format_;
  }

  std::uint32_t num_images() const {
    return swapchain_images_.size();
  }

 private:
  void create_render_pass(Device const& device, vk::SurfaceFormatKHR surface_format) {
    vk::AttachmentDescription attachment_desc(
        {},
        surface_format.format,
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eDontCare,
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
    composite_render_pass_ = device.vk().createRenderPass(create_info);
  }

  void create_swapchain(Application const& app) {
    SwapChainSupportDetails support(*app.selected_device(), *app.surface());
    surface_format_ = support.choose_surface_format();
    queue_family_idx_ = app.find_graphics_queue();

    auto&& device = device_.vk();

    create_render_pass(device_, surface_format_);

    vk::SwapchainCreateInfoKHR create_info(
        {},
        *app.surface(),
        support.choose_image_count(),
        surface_format_.format,
        surface_format_.colorSpace,
        support.choose_extent(app.window()),
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        1,
        &queue_family_idx_,
        vk::SurfaceTransformFlagBitsKHR::eIdentity,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        support.choose_present_mode());
    swapchain_ = device.createSwapchainKHR(create_info);

    swapchain_images_ = swapchain_.getImages();
    std::ranges::transform(
        swapchain_images_,
        std::back_inserter(swapchain_image_views_),
        [this, &device](VkImage img) {
          vk::ImageViewCreateInfo create_info(
              {},
              img,
              vk::ImageViewType::e2D,
              surface_format_.format,
              {},
              vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
          return vk::raii::ImageView(device, create_info);
        });

    std::ranges::transform(
        swapchain_image_views_,
        std::back_inserter(framebuffers_),
        [&app, &device, this](vk::raii::ImageView const& image_view) {
          vk::FramebufferCreateInfo create_info(
              {},
              *composite_render_pass_,
              1,
              &*image_view,
              app.window().width(),
              app.window().height(),
              1);
          return vk::raii::Framebuffer(device, create_info);
        });
  }

  void create_sync_objects() {
    auto create_semaphore_at = [this](int) {
      vk::SemaphoreCreateInfo create_info;
      return vk::raii::Semaphore(device_.vk(), create_info);
    };

    constexpr int kNumFramesInFlight = 2;
    auto rng = std::ranges::views::iota(0, kNumFramesInFlight);
    std::ranges::transform(
        rng,
        std::back_inserter(image_ready_semaphores_),
        create_semaphore_at);
  }

  class SwapChainSupportDetails {
   public:
    SwapChainSupportDetails(vk::PhysicalDevice const& pd, vk::SurfaceKHR const& surface) {
      capabilities_ = pd.getSurfaceCapabilitiesKHR(surface);
      formats_ = pd.getSurfaceFormatsKHR(surface);
      present_modes_ = pd.getSurfacePresentModesKHR(surface);
    }

    vk::SurfaceFormatKHR choose_surface_format() const {
      auto selected_format = std::ranges::find_if(
          formats_,
          [](vk::SurfaceFormatKHR available_format) {
            return available_format.format == vk::Format::eB8G8R8A8Srgb &&
                   available_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
          });

      if(selected_format != formats_.end()) {
        return *selected_format;
      }

      // Hope for the best
      return formats_[0];
    }

    vk::PresentModeKHR choose_present_mode() const {
      auto selected_present_mode = std::ranges::find_if(
          present_modes_,
          [](vk::PresentModeKHR const& available_present_mode) {
            return available_present_mode == vk::PresentModeKHR::eMailbox;
          });

      if(selected_present_mode != present_modes_.end()) {
        return *selected_present_mode;
      }

      // Hope for the best
      return vk::PresentModeKHR::eFifo;
    }

    VkExtent2D choose_extent(Window const& window) const {
      if(capabilities_.currentExtent.width !=
         std::numeric_limits<std::uint32_t>::max()) {
        return capabilities_.currentExtent;
      }
      else {
        // We need to refetch the window size after creating the surface
        // because we we requested might not be the same.
        int width, height;
        glfwGetFramebufferSize(window.glfw(), &width, &height);

        vk::Extent2D actual_extent(
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height));

        actual_extent.width = std::clamp(
            actual_extent.width,
            capabilities_.minImageExtent.width,
            capabilities_.maxImageExtent.width);
        actual_extent.height = std::clamp(
            actual_extent.height,
            capabilities_.minImageExtent.height,
            capabilities_.maxImageExtent.height);

        return actual_extent;
      }
    }

    std::uint32_t choose_image_count() const {
      std::uint32_t image_count = capabilities_.minImageCount + 1;
      if(capabilities_.maxImageCount > 0 &&
         image_count > capabilities_.maxImageCount) {
        image_count = capabilities_.maxImageCount;
      }

      return image_count;
    }

   private:
    vk::SurfaceCapabilitiesKHR capabilities_;
    std::vector<vk::SurfaceFormatKHR> formats_;
    std::vector<vk::PresentModeKHR> present_modes_;
  };

  Device const& device_;
  vk::raii::SwapchainKHR swapchain_;
  std::vector<VkImage> swapchain_images_;
  std::vector<vk::raii::ImageView> swapchain_image_views_;
  std::vector<vk::raii::Framebuffer> framebuffers_;
  std::vector<vk::raii::Semaphore> image_ready_semaphores_;
  vk::raii::RenderPass composite_render_pass_;
  vk::SurfaceFormatKHR surface_format_;
  std::uint32_t queue_family_idx_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_SWAPCHAIN_HPP_