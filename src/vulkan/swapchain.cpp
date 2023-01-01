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
#include "swapchain.hpp"

#include <GLFW/glfw3.h>
#include <ranges>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "application.hpp"
#include "device.hpp"
#include "render_target.hpp"
#include "rndrx/noncopyable.hpp"
#include "rndrx/throw_exception.hpp"

namespace rndrx::vulkan {

namespace {
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
          return available_format.format == vk::Format::eB8G8R8A8Unorm &&
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

} // namespace

PresentationContext PresentationQueue::acquire_context() {
  sync_idx_ = (sync_idx_ + 1) % image_ready_semaphores_.size();
  auto result = swapchain_.vk().acquireNextImage(
      std::numeric_limits<std::uint64_t>::max(),
      *image_ready_semaphores_[sync_idx_]);
  if(result.first != vk::Result::eSuccess) {
    throw_runtime_error("Failed to handle swapchain acquire failure");
  }

  image_idx_ = result.second;

  return {
      swapchain_.images()[image_idx_],
      *image_views_[image_idx_],
      *framebuffers_[image_idx_],
      image_idx_,
      sync_idx_};
}

void PresentationQueue::present_context(PresentationContext const& ctx) const {
  vk::PresentInfoKHR present_info;
  present_info //
      .setWaitSemaphores(*image_ready_semaphores_[ctx.sync_idx_])
      .setSwapchains(*swapchain_.vk())
      .setImageIndices(ctx.image_idx_);
  auto result = present_queue_.presentKHR(present_info);
  if(result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to handle swapchain present failure");
  }
}

void PresentationQueue::create_image_views(Device const& device) {
  std::ranges::transform(
      swapchain_.images(),
      std::back_inserter(image_views_),
      [this, &device](vk::Image img) {
        vk::ImageViewCreateInfo create_info;
        create_info //
            .setImage(img)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(swapchain_.surface_format().format)
            .setSubresourceRange(
                vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
        return device.vk().createImageView(create_info);
      });
}

void PresentationQueue::create_framebuffers(
    Application const& app,
    Device const& device,
    vk::RenderPass renderpass) {
  std::ranges::transform(
      image_views_,
      std::back_inserter(framebuffers_),
      [&](vk::raii::ImageView const& image_view) {
        vk::FramebufferCreateInfo create_info;
        create_info //
            .setRenderPass(renderpass)
            .setAttachments(*image_view)
            .setWidth(app.window().width())
            .setHeight(app.window().height())
            .setLayers(1);
        return device.vk().createFramebuffer(create_info);
      });
}

void PresentationQueue::create_sync_objects(Device const& device) {
  auto create_semaphore_at = [this, &device](int) {
    vk::SemaphoreCreateInfo create_info;
    return device.vk().createSemaphore(create_info);
  };

  constexpr int kNumFramesInFlight = 2;
  auto rng = std::ranges::views::iota(0, kNumFramesInFlight);
  std::ranges::transform(
      rng,
      std::back_inserter(image_ready_semaphores_),
      create_semaphore_at);
}

Swapchain::Swapchain(Application const& app, Device& device)
    : device_(device)
    , swapchain_(nullptr)
    , composite_render_pass_(nullptr) {
  create_swapchain(app);
}

void Swapchain::create_swapchain(Application const& app) {
  SwapChainSupportDetails support(*app.selected_device(), *app.surface());
  surface_format_ = support.choose_surface_format();
  queue_family_idx_ = app.find_graphics_queue();

  auto&& device = device_.vk();

  vk::SwapchainCreateInfoKHR create_info;
  create_info //
      .setSurface(*app.surface())
      .setMinImageCount(support.choose_image_count())
      .setImageFormat(surface_format_.format)
      .setImageColorSpace(surface_format_.colorSpace)
      .setImageExtent(support.choose_extent(app.window()))
      .setImageArrayLayers(1)
      .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
      .setImageSharingMode(vk::SharingMode::eExclusive)
      .setQueueFamilyIndices(queue_family_idx_)
      .setPresentMode(support.choose_present_mode());

  swapchain_ = device.createSwapchainKHR(create_info);
  images_ = swapchain_.getImages();
}

} // namespace rndrx::vulkan