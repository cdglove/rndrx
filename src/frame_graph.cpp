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
#include "rndrx/frame_graph.hpp"

#include "rndrx/assert.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/vulkan/frame_graph.hpp"

namespace rndrx {

std::unique_ptr<FrameGraph> FrameGraph::create(const FrameGraphDescription& desc) {
  switch(SelectedBackend) {
    case Backend::Vulkan:
      return std::make_unique<vulkan::FrameGraph>(desc);
    case Backend::D3D12:
      RNDRX_ASSERT(false);
      return std::make_unique<vulkan::FrameGraph>(desc);
  }
}

#define FRAME_GRAPH_DISATCH(func)                                              \
  void FrameGraph::func() {                                                    \
    switch(SelectedBackend) {                                                  \
      case Backend::Vulkan:                                                    \
        return static_cast<vulkan::FrameGraph*>(this)->func();                 \
      case Backend::D3D12:                                                     \
        return static_cast<vulkan::FrameGraph*>(this)->func();                 \
    }                                                                          \
  }

FRAME_GRAPH_DISATCH(render)

} // namespace rndrx