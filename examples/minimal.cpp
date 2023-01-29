#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

std::vector<const char*> GetRequiredExtensionNames() {
  uint32_t glfwExtensionCount = 0;
  auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
  return std::vector(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

int main() {
  glfwInit();

  auto instanceExtensionNames = GetRequiredExtensionNames();

  vk::raii::Context context;
  vk::raii::Instance instance(
      context,
      vk::InstanceCreateInfo() //
          .setPEnabledExtensionNames(instanceExtensionNames));

  std::array deviceExtensionNames = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  std::array queuePriorities = {1.0F};
  std::array queueCreateInfos = {
      vk::DeviceQueueCreateInfo() //
          .setQueueFamilyIndex(0)
          .setQueueCount(1)
          .setQueuePriorities(queuePriorities) //
  };

  vk::raii::PhysicalDevice adapter = instance.enumeratePhysicalDevices()[0];
  vk::raii::Device device(
      adapter,
      vk::DeviceCreateInfo()
          .setPEnabledExtensionNames(deviceExtensionNames)
          .setQueueCreateInfos(queueCreateInfos));

  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  int windowWidth = 1280;
  int windowHeight = 720;
  GLFWwindow* window = glfwCreateWindow( //
      windowWidth,
      windowHeight,
      "Test",
      nullptr,
      nullptr);
  VkSurfaceKHR cSurface;
  glfwCreateWindowSurface(*instance, window, nullptr, &cSurface);
  vk::raii::SurfaceKHR surface(instance, cSurface);

  uint32_t requestedImageCount = 3;
  vk::Format swapChainFormat = vk::Format::eR8G8B8A8Srgb;

  vk::raii::SwapchainKHR swapChain(
      device,
      vk::SwapchainCreateInfoKHR()
          .setSurface(*surface)
          .setMinImageCount(requestedImageCount)
          .setImageFormat(swapChainFormat)
          .setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear)
          .setImageExtent(vk::Extent2D(windowWidth, windowHeight))
          .setImageArrayLayers(1)
          .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
          .setImageSharingMode(vk::SharingMode::eExclusive)
          .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
          .setPresentMode(vk::PresentModeKHR::eFifo)
          .setClipped(VK_TRUE));

  auto images = swapChain.getImages();
  std::vector<vk::raii::ImageView> imageViews;
  imageViews.reserve(images.size());
  for(vk::Image image : images) {
    imageViews.emplace_back(
        device,
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(swapChainFormat)
            .setSubresourceRange( //
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)));
  }

  while(!glfwWindowShouldClose(window)) {
    glfwPollEvents();
  }
}