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
#ifndef RNDRX_VULKAN_FRAMEGRAPHBUILDER_HPP_
#define RNDRX_VULKAN_FRAMEGRAPHBUILDER_HPP_
#pragma once

#include <unordered_map>
#include "rndrx/config.hpp"
#include "rndrx/frame_graph_description.hpp"

namespace rndrx {
class FrameGraphDescription;
}

namespace rndrx::vulkan {

class FrameGraphRenderPass;
class Device;
class FrameGraph;

class FrameGraphBuilder {
 public:
  FrameGraphBuilder(Device& device)
      : device_(device) {
  }

  void register_pass(std::string_view name, FrameGraphRenderPass* pass);
  FrameGraphRenderPass* find_render_pass(std::string_view name) const;
  Device& device() const {
    return device_;
  }
  // std::unique_ptr<FrameGraph> compile(FrameGraphDescription const& description);

 private:
  Device& device_;
  std::unordered_map<std::string_view, FrameGraphRenderPass*> render_pass_map_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_FRAMEGRAPHBUILDER_HPP_