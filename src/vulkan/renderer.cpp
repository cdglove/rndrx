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
#include "rndrx/vulkan/renderer.hpp"

namespace rndrx::vulkan {

namespace {
ShaderCache load_essential_shaders(Device& device) {
  ShaderCache cache;
  ShaderLoader loader(device, cache);
  loader.load("fullscreen_quad.vsmain");
  loader.load("fullscreen_quad.copyimageopaque");
  loader.load("fullscreen_quad.blendimageinv");
  loader.load("fullscreen_quad.blendimage");
  loader.load("simple_static_model.vsmain");
  loader.load("simple_static_model.phong");
  return cache;
}
} // namespace

Renderer::Renderer(Application const& app)
    : device_(app)
    , swapchain_(app, device_)
    , shaders_(load_essential_shaders(device_))
    , final_composite_pass_(device_, swapchain_.surface_format().format, shaders_)
    // , present_queue_(
    //       device_,
    //       swapchain_,
    //       device_.graphics_queue(),
    //       *final_composite_pass_.render_pass()) {
  {}
} // namespace rndrx::vulkan
