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
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include "application.hpp"
#include "window.hpp"

// #define TINYOBJLOADER_IMPLEMENTATION 1
// #define STB_IMAGE_IMPLEMENTATION     1
// #include <stb_image.h>
// #include <tiny_obj_loader.h>

void choose_graphics_device(rndrx::vulkan::Application& app) {
  auto devices = app.physical_devices();
  for(auto&& device : app.physical_devices()) {
    auto properties = device.getProperties();
    if(properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
      app.select_device(device);
    }
  }
}

class RndrxTest : public rndrx::vulkan::Application {
 public:
  using Application::Application;
};

int main(int, char**) {
  rndrx::vulkan::Window window;
  RndrxTest app(window);
  choose_graphics_device(app);

  using rndrx::vulkan::Application;
  try {
    while(app.run() != Application::RunResult::Exit)
      ;
  }
  catch(std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
