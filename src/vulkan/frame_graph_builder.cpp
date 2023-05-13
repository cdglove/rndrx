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
#include "rndrx/vulkan/frame_graph_builder.hpp"
#include <memory>
#include "rndrx/throw_exception.hpp"

namespace rndrx::vulkan {
void FrameGraphBuilder::register_pass(std::string_view name, FrameGraphRenderPass* pass) {
  auto result = render_pass_map_.insert(std::make_pair(name, pass));
  if(result.second == false) {
    RNDRX_THROW_RUNTIME_ERROR()
        << "Renderpass " << name << " is already registered.";
  }
}

FrameGraphRenderPass* FrameGraphBuilder::find_render_pass(std::string_view name) const {
  auto pass = render_pass_map_.find(name);
  if(pass != render_pass_map_.end()) {
    return pass->second;
  }

  return nullptr;
}


} // namespace rndrx::vulkan