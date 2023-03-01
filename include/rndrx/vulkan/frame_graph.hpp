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
#ifndef RNDRX_VULKAN_FRAMEGRAPH_HPP_
#define RNDRX_VULKAN_FRAMEGRAPH_HPP_
#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <variant>
#include <vulkan/vulkan.hpp>
#include "rndrx/frame_graph.hpp"

namespace rndrx {
class FrameGraphAttachmentOutputDescription;
class FrameGraphAttachmentInputDescription;
class FrameGraphBufferDescription;
class FrameGraphDescription;

} // namespace rndrx
namespace rndrx::vulkan {

class FrameGraphAttachmentOutput {
 public:
  FrameGraphAttachmentOutput(FrameGraphAttachmentOutputDescription const& desc);

 private:
  std::string name_;
  vk::Format format_ = vk::Format::eUndefined;
  int width_ = 0;
  int height_ = 0;
  vk::AttachmentLoadOp load_op_;
  glm::vec4 clear_colour_;
  float clear_depth_;
  int clear_stencil_;
};

class FrameGraphAttachmentInput {
 public:
  FrameGraphAttachmentInput(FrameGraphAttachmentInputDescription const& desc);

 private:
  std::string name_;
};

class FrameGraphBuffer {
 public:
  FrameGraphBuffer(FrameGraphBufferDescription const& desc);

 private:
  std::string name_;
};

using FrameGraphInput = std::variant<FrameGraphAttachmentInput, FrameGraphBuffer>;
using FrameGraphOutput = std::variant<FrameGraphAttachmentOutput, FrameGraphBuffer>;

// class FrameGraphRenderPass {
//  public:
//  private:
//   std::vector<FrameGraphInput> inputs_;
//   std::vector<FrameGraphOutput> outputs_;
// };

class FrameGraphNode {
 public:
 private:
  std::vector<FrameGraphInput> inputs_;
  std::vector<FrameGraphOutput> outputs_;
};

class FrameGraph : public ::rndrx::FrameGraph {
 public:
  FrameGraph(FrameGraphDescription const& description);

 private:
  std::vector<FrameGraphNode> nodes_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_FRAMEGRAPH_HPP_