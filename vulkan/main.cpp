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
// #define NOMINMAX 1
// #define WIN32_LEAN_AND_MEAN 1
#include <vulkan/vulkan_core.h>
#include <limits>
#include <xutility>
#define GLM_FORCE_RADIANS                  1
#define GLM_FORCE_DEPTH_ZERO_TO_ONE        1
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES 1
#define TINYOBJLOADER_IMPLEMENTATION       1
#define STB_IMAGE_IMPLEMENTATION           1
#define GLFW_INCLUDE_VULKAN                1
// #define GLFW_EXPOSE_NATIVE_WIN32 1

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <algorithm>
#include <array>
#include <chrono>
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
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

std::array<char const*, 1> const kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

struct noncopyable {
  noncopyable() = default;
  noncopyable(noncopyable const&) = delete;
  noncopyable& operator=(noncopyable const&) = delete;
  noncopyable(noncopyable&&) = default;
  noncopyable& operator=(noncopyable&&) = default;
};

enum class LogLevel {
  None,
  Error,
  Warn,
  Info,
  Trace,
  NumLogLevels,
};

static VKAPI_ATTR vk::Bool32 VKAPI_CALL vulkan_validation_callback(
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

struct to_vector_t {};

constexpr to_vector_t to_vector;

template <typename Range>
auto operator|(Range&& rng, to_vector_t) {
  return std::vector<std::ranges::range_value_t<Range>>(rng.begin(), rng.end());
}

LogLevel const g_LogLevel = LogLevel::Trace;
struct LogState : noncopyable {
  explicit LogState(std::ostream& os)
      : os_(os) {
  }

  bool is_done() const {
    return done_;
  }

  void done() {
    done_ = true;
    os_ << std::endl;
  }

  std::ostream& os_;
  bool done_ = false;
};

#define LOG(level)                                                             \
  for(LogState log_state(std::cerr);                                           \
      g_LogLevel >= LogLevel::level && !log_state.is_done();                   \
      log_state.done())                                                        \
  std::cerr

template <typename Ex>
void throw_exception(Ex e) {
  throw e;
}

void throw_runtime_error(char const* format) {
  throw_exception(std::runtime_error(format));
}

class Image {
 public:
  Image(vk::Image image, vk::ImageView view)
      : image_(image)
      , view_(view) {
  }

  vk::Image image() const {
    return image_;
  }

  vk::ImageView view() const {
    return view_;
  }

 private:
  vk::Image image_;
  vk::ImageView view_;
};

class Window : noncopyable {
 public:
  Window() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(
        width_,
        height_,
        "rndrx-vulkan",
        /* glfwGetPrimaryMonitor() */ nullptr,
        nullptr);
  }

  ~Window() {
    glfwDestroyWindow(window_);
  }
  GLFWwindow* glfw() const {
    return window_;
  }

  int width() const {
    return width_;
  }

  int height() const {
    return height_;
  }

  vk::Rect2D extents() const {
    return vk::Rect2D({0, 0}, vk::Extent2D(width(), height()));
  }

  enum class SizeEvent {
    None,
    Changed,
  };

  SizeEvent handle_window_size() {
    int old_width = width_;
    int old_height = height_;
    glfwGetFramebufferSize(window_, &width_, &height_);
    if(width_ != old_width || height_ != old_height) {
      return SizeEvent::Changed;
    }

    return SizeEvent::None;
  }

 private:
  GLFWwindow* window_ = nullptr;
  int width_ = 1920;
  int height_ = 1080;
};

class Application : noncopyable {
 public:
  Application(Window& window)
      : window_(window)
      , instance_(nullptr)
      , surface_(nullptr)
      , messenger_(nullptr) {
    create_instance();
    create_surface();
    select_device();
  }

  std::uint32_t find_graphics_queue() const {
    auto queue_family_properties = selected_device().getQueueFamilyProperties();
    auto graphics_queue = std::ranges::find_if(
        queue_family_properties,
        [](vk::QueueFamilyProperties const& props) {
          return (props.queueFlags & vk::QueueFlagBits::eGraphics) ==
                 vk::QueueFlagBits::eGraphics;
        });

    return std::distance(queue_family_properties.begin(), graphics_queue);
  }

  std::vector<char const*> get_required_instance_extensions() const {
    std::uint32_t glfw_ext_count = 0;
    char const** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    std::vector<char const*> extensions(glfw_exts, glfw_exts + glfw_ext_count);
#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    return extensions;
  }

  std::vector<char const*> get_required_device_extensions() const {
    std::vector<char const*> extensions;
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
    extensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
    extensions.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
    return extensions;
  }

  vk::raii::Instance const& vk_instance() const {
    return instance_;
  }

  vk::raii::SurfaceKHR const& surface() const {
    return surface_;
  }

  vk::raii::PhysicalDevice const& selected_device() const {
    return physical_devices_[selected_device_idx_];
  }

  Window const& window() const {
    return window_;
  }

  bool run();

 private:
  void create_instance() {
    vk::ApplicationInfo app_info("rndrx-vulkan", 1, "rndrx", 1, VK_API_VERSION_1_3);
    std::vector<char const*> request_layer_names;
#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    if(!check_validation_layer_support()) {
      throw_runtime_error("Debug layer not supported");
    }
#endif

    std::ranges::copy(kValidationLayers, std::back_inserter(request_layer_names));

    auto required_extensions = get_required_instance_extensions();
    vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> create_info(
        {{}, &app_info, request_layer_names, required_extensions},
        {{},
         vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
         vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
             vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
             vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
         &vulkan_validation_callback});

#if !RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    create_info.unlink<vk::DebugUtilsMessengerCreateInfoEXT>();
#endif

    instance_ = vk::raii::Instance(vk_context_, create_info.get());

#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
    messenger_ = instance_.createDebugUtilsMessengerEXT(
        create_info.get<vk::DebugUtilsMessengerCreateInfoEXT>());
#endif
  }

  void create_surface() {
    VkSurfaceKHR surface;
    if(glfwCreateWindowSurface(*instance_, window_.glfw(), nullptr, &surface) !=
       VK_SUCCESS) {
      throw_runtime_error("failed to create window surface!");
    }

    surface_ = vk::raii::SurfaceKHR(instance_, surface);
  }

  void select_device() {
    auto required_device_extensions = get_required_device_extensions();
    physical_devices_ =
        instance_.enumeratePhysicalDevices() |
        std::ranges::views::filter([&required_device_extensions](
                                       vk::raii::PhysicalDevice const& dev) {
          return std::ranges::all_of(
              required_device_extensions,
              [&dev](std::string_view extension_name) {
                return std::ranges::any_of(
                    dev.enumerateDeviceExtensionProperties(),
                    [&extension_name](
                        vk::ExtensionProperties const& extension_properties) {
                      return extension_name == extension_properties.extensionName;
                    });
              });
        }) |
        to_vector;
  }

  bool check_validation_layer_support() {
    auto available_layers = vk_context_.enumerateInstanceLayerProperties();
    return std::ranges::any_of(available_layers, [](vk::LayerProperties const& layer) {
      return std::ranges::any_of(
          kValidationLayers,
          [&layer](std::string_view validation_layer) {
            return validation_layer == layer.layerName;
          });
    });

    return true;
  }

  Window& window_;
  vk::raii::Context vk_context_;
  vk::raii::Instance instance_;
  vk::raii::DebugUtilsMessengerEXT messenger_;
  std::vector<vk::raii::PhysicalDevice> physical_devices_;
  vk::raii::SurfaceKHR surface_;
  std::uint32_t selected_device_idx_ = 0;
};

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

class Device : noncopyable {
 public:
  explicit Device(Application const& app)
      : device_(nullptr)
      , graphics_queue_(nullptr) {
    create_device(app);
  }

  vk::raii::Device const& vk() const {
    return device_;
  }

  std::uint32_t graphics_queue_family_idx() const {
    return gfx_queue_idx_;
  }

  vk::raii::Queue const& graphics_queue() const {
    return graphics_queue_;
  }

  std::uint32_t find_memory_type(
      std::uint32_t type_filter,
      vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties mem_properties;
    physical_device_.getMemoryProperties(&mem_properties);

    for(std::uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
      if((type_filter & (1 << i)) &&
         (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw_runtime_error("failed to find suitable memory type!");
    return 0;
  }

 private:
  void create_device(Application const& app) {
    float priority = 1.f;
    gfx_queue_idx_ = app.find_graphics_queue();
    vk::DeviceQueueCreateInfo queue_create_info({}, gfx_queue_idx_, 1, &priority);
    auto required_extensions = app.get_required_device_extensions();
    vk::PhysicalDeviceVulkan13Features vulkan_13_features;
    vulkan_13_features.synchronization2 = true;
    vulkan_13_features.dynamicRendering = true;
    vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>
        create_info = {
            {{}, queue_create_info, {}, required_extensions, nullptr},
            {},
            vulkan_13_features};
    physical_device_ = *app.selected_device();
    device_ = vk::raii::Device(app.selected_device(), create_info.get());
    graphics_queue_ = device_.getQueue(gfx_queue_idx_, 0);
  }

  vk::raii::Device device_;
  vk::raii::Queue graphics_queue_;
  vk::PhysicalDevice physical_device_;
  std::uint32_t gfx_queue_idx_;
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

class ImGuiRenderPass : noncopyable {
 public:
  ImGuiRenderPass(
      Window const& window,
      Application const& app,
      Device const& device,
      Swapchain const& swapchain)
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

  void render(SubmissionContext& sc) {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    vk::CommandBuffer command_buffer = sc.command_buffer();
    RenderContext rc;
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

  void initialise_font(Device const& device, SubmissionContext& sc) {
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
      throw_runtime_error("ImGui Vulkan call failed.");
    }
  }

  void create_descriptor_pool(Device const& device) {
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

  void create_image(Device const& device, Window const& window) {
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

  void create_render_pass(Device const& device) {
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

  Window const& window_;
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

class Glfw : noncopyable {
 public:
  Glfw() {
    glfwSetErrorCallback(glfw_error_callback);
    if(!glfwInit()) {
      throw_runtime_error("Failed to initialise glfw");
    }

    if(!glfwVulkanSupported()) {
      throw_runtime_error("Vulkan not supported in glfw.");
    }
  }

  ~Glfw() {
    glfwTerminate();
  }

 private:
};

bool Application::run() {
  Device device(*this);
  Swapchain swapchain(*this, device);
  ImGuiRenderPass imgui(window_, *this, device, swapchain);

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
  Window window;
  Application app(window);

  try {
    while(app.run())
      ;
  }
  catch(std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
