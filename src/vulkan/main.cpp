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
#include <iostream>
#include <vulkan/vulkan.hpp>
#include "application.hpp"
#include "composite_render_pass.hpp"
#include "imgui_render_pass.hpp"
#include "render_context.hpp"
#include "scene.hpp"

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

 private:
  void on_device_objects_created() override {
    device_objects_ = std::make_unique<DeviceObjects>();
    device_objects_->imgui_render_pass =
        rndrx::vulkan::ImGuiRenderPass(*this, device(), swapchain());

    device_objects_->composite_imgui = rndrx::vulkan::CompositeRenderPass::DrawItem(
        device(),
        final_composite_pass(),
        device_objects_->imgui_render_pass.target().view());
  }

  void on_begin_initialise_device_resources(rndrx::vulkan::SubmissionContext& ctx) override {
    device_objects_->imgui_render_pass.create_fonts_texture(ctx);
  }

  void on_end_initialise_device_resources() override {
    device_objects_->imgui_render_pass.finish_font_texture_creation();
  }

  void on_begin_update() override {
    device_objects_->imgui_render_pass.begin_frame();
  }

  void on_end_update() override {
    device_objects_->imgui_render_pass.end_frame();
  }

  void on_begin_render(rndrx::vulkan::SubmissionContext& sc) override {
    device_objects_->imgui_render_pass.render(sc);
  }

  void on_pre_present(
      rndrx::vulkan::SubmissionContext& sc,
      rndrx::vulkan::PresentationContext& pc) override {
    rndrx::vulkan::RenderContext rc;
    rc.set_targets(window().extents(), pc.target().view(), pc.target().framebuffer());
    final_composite_pass().render(rc, sc, {&device_objects_->composite_imgui, 1});
  }

  void on_pre_destroy_device_objects() override {
    device_objects_.reset();
  }

  struct DeviceObjects {
    rndrx::vulkan::ImGuiRenderPass imgui_render_pass;
    rndrx::vulkan::CompositeRenderPass::DrawItem composite_imgui;
  };

  std::unique_ptr<DeviceObjects> device_objects_;
};

int main(int, char**) {
  // rndrx::vulkan::Scene scene = rndrx::vulkan::load_scene(
  //     "assets/models/NewSponza_Main_glTF_002.gltf");
  RndrxTest app;
  choose_graphics_device(app);

  using rndrx::vulkan::Application;
  try {
    app.run();
  }
  catch(std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
