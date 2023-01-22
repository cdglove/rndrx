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
#include <iterator>
#include <limits>
#include <ranges>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "application.hpp"
#include "device.hpp"
#include "render_target.hpp"
#include "rndrx/noncopyable.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"
#include "window.hpp"

namespace rndrx::vulkan {

auto dereference = [](auto&& obj) { return *obj; };

template <typename SourceRange, typename DestinationRange>
void transform_from_raii(SourceRange&& src, DestinationRange&& dest) {
  src | std::ranges::views::transform(dereference) | to_vector_ref(dest);
}

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

  vk::Extent2D choose_extent(Window const& window) const {
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
          static_cast<std::uint32_t>(width),
          static_cast<std::uint32_t>(height));

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

PresentationQueue::~PresentationQueue() {
  std::vector<vk::Fence> wait_fences;
  transform_from_raii(image_ready_fences_, wait_fences);
  auto wait_result = swapchain_.vk().getDevice().waitForFences(
      wait_fences,
      VK_TRUE,
      std::numeric_limits<std::uint64_t>::max());

  if(wait_result != vk::Result::eSuccess) {
    // Don't throw from destructor -- hope for the best.
    // throw_runtime_error("Failed to wait for fence.");
  }
}

PresentationContext PresentationQueue::acquire_context() {
  sync_idx_ = (sync_idx_ + 1) % image_ready_fences_.size();
  auto wait_result = swapchain_.vk().getDevice().waitForFences(
      *image_ready_fences_[sync_idx_],
      VK_TRUE,
      std::numeric_limits<std::uint64_t>::max());

  if(wait_result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to wait for fence.");
  }

  swapchain_.vk().getDevice().resetFences(*image_ready_fences_[sync_idx_]);

  auto result = swapchain_.vk().getDevice().acquireNextImage2KHR(
      vk::AcquireNextImageInfoKHR()
          .setSwapchain(*swapchain_.vk())
          .setTimeout(std::numeric_limits<std::uint64_t>::max())
          .setFence(*image_ready_fences_[sync_idx_])
          .setDeviceMask(1));
  if(result.result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to handle swapchain acquire failure");
  }

  image_idx_ = result.value;

  return {
      swapchain_.images()[image_idx_],
      *image_views_[image_idx_],
      *framebuffers_[image_idx_],
      image_idx_,
      sync_idx_};
}

void PresentationQueue::present(PresentationContext const& ctx) const {
  vk::PresentInfoKHR present_info;
  present_info.setSwapchains(*swapchain_.vk()).setImageIndices(ctx.image_idx_);
  auto result = present_queue_.presentKHR(present_info);
  if(result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to handle swapchain present failure");
  }
}

void PresentationQueue::create_image_views(Device const& device) {
  image_views_ = //
      swapchain_.images() |
      std::ranges::views::transform([this, &device](vk::Image img) {
        return device.vk().createImageView(
            vk::ImageViewCreateInfo()
                .setImage(img)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(swapchain_.surface_format().format)
                .setSubresourceRange(vk::ImageSubresourceRange(
                    vk::ImageAspectFlagBits::eColor,
                    0,
                    1,
                    0,
                    1)));
      }) |
      to_vector;
}

void PresentationQueue::create_framebuffers(Device const& device, vk::RenderPass renderpass) {
  framebuffers_ = //
      image_views_ |
      std::ranges::views::transform(
          [this, &device, renderpass](vk::raii::ImageView const& image_view) {
            return device.vk().createFramebuffer(
                vk::FramebufferCreateInfo()
                    .setRenderPass(renderpass)
                    .setAttachments(*image_view)
                    .setWidth(swapchain_.extent().width)
                    .setHeight(swapchain_.extent().height)
                    .setLayers(1));
          }) |
      to_vector;
}

void PresentationQueue::create_sync_objects(Device const& device) {
  image_ready_fences_ = //
      swapchain_.images() |
      std::ranges::views::transform([this, &device](vk::Image img) {
        vk::FenceCreateInfo create_info(vk::FenceCreateFlagBits::eSignaled);
        return device.vk().createFence(create_info);
      }) |
      to_vector;
}

Swapchain::Swapchain(Application const& app, Device& device) {
  create_swapchain(app, device);
}

void Swapchain::create_swapchain(Application const& app, Device& device) {
  SwapChainSupportDetails support(*app.selected_device(), *app.surface());
  surface_format_ = support.choose_surface_format();
  queue_family_idx_ = app.find_graphics_queue_family_idx();
  extent_ = support.choose_extent(app.window());

  swapchain_ = device.vk().createSwapchainKHR(
      vk::SwapchainCreateInfoKHR()
          .setSurface(*app.surface())
          .setMinImageCount(support.choose_image_count())
          .setImageFormat(surface_format_.format)
          .setImageColorSpace(surface_format_.colorSpace)
          .setImageExtent(extent_)
          .setImageArrayLayers(1)
          .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
          .setImageSharingMode(vk::SharingMode::eExclusive)
          .setQueueFamilyIndices(queue_family_idx_)
          .setPresentMode(support.choose_present_mode()));
  images_ = swapchain_.getImages();
}

} // namespace rndrx::vulkan
