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
#include "window.hpp"

#include <GLFW/glfw3.h>
#include "rndrx/throw_exception.hpp"

namespace rndrx::vulkan {
namespace {
void glfw_error_callback(int error, const char* description) {
  std::cerr << "`Glfw Error " << error << ": " << description;
}

} // namespace

Window::Window() {
  glfwSetErrorCallback(glfw_error_callback);
  if(!glfwInit()) {
    throw_runtime_error("Failed to initialise glfw");
  }

  if(!glfwVulkanSupported()) {
    throw_runtime_error("Vulkan not supported in glfw.");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(
      width_,
      height_,
      "rndrx-vulkan",
      /* glfwGetPrimaryMonitor() */ nullptr,
      nullptr);
}

Window::~Window() {
  glfwDestroyWindow(window_);
  glfwTerminate();
}

Window::SizeEvent Window::handle_window_size() {
  int old_width = width_;
  int old_height = height_;
  glfwGetFramebufferSize(window_, &width_, &height_);
  if(width_ != old_width || height_ != old_height) {
    return SizeEvent::Changed;
  }

  return SizeEvent::None;
}
} // namespace rndrx::vulkan