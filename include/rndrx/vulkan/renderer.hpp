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
#ifndef RNDRX_VULKAN_RENDERER_HPP_
#define RNDRX_VULKAN_RENDERER_HPP_
#pragma once

#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/composite_render_pass.hpp"
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/frame_graph.hpp"
#include "rndrx/vulkan/imgui_render_pass.hpp"
#include "rndrx/vulkan/shader_cache.hpp"
#include "rndrx/vulkan/swapchain.hpp"

namespace rndrx::vulkan {
class Renderer : noncopyable {
 public:
  Renderer() = default;
  Renderer(Application const& app);

  Device& device() {
    return device_;
  }

  Swapchain& swapchain() {
    return swapchain_;
  }

  ShaderCache& shaders() {
    return shaders_;
  }

  ShaderCache const& shaders() const {
    return shaders_;
  }

  PresentationContext acquire_present_context();

 private:
  Device device_;
  Swapchain swapchain_;
  ShaderCache shaders_;
  CompositeRenderPass final_composite_pass_;
  // PresentationQueue present_queue_;
  ImGuiRenderPass imgui_render_pass_;
  FrameGraph deferred_frame_graph_;
  FrameGraph gbuffer_debug_frame_graph_;
};

} // namespace rndrx::vulkan
#endif // RNDRX_VULKAN_RENDERER_HPP_