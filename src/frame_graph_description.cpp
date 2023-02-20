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
#include "rndrx/frame_graph_description.hpp"

#include "rndrx/assert.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/vulkan/frame_graph.hpp"

namespace rndrx {

namespace detail {
FrameGraphNamedObject::FrameGraphNamedObject(std::string name)
    : name_(std::move(name)) {
}
FrameGraphNamedObject& FrameGraphNamedObject::name(std::string name) {
  name_ = std::move(name);
  return *this;
}

} // namespace detail

FrameGraphAttachmentOutputDescription& FrameGraphAttachmentOutputDescription::format(
    ImageFormat format) {
  format_ = format;
  return *this;
}

FrameGraphAttachmentOutputDescription&
FrameGraphAttachmentOutputDescription::resolution(int width, int height) {
  width_ = width;
  height_ = height;
  return *this;
}

FrameGraphAttachmentOutputDescription& FrameGraphAttachmentOutputDescription::load_op(
    AttachmentLoadOp op) {
  load_op_ = op;
  return *this;
}

FrameGraphAttachmentOutputDescription&
FrameGraphAttachmentOutputDescription::clear_colour(glm::vec4 colour) {
  clear_colour_ = colour;
  return *this;
}

FrameGraphAttachmentOutputDescription&
FrameGraphAttachmentOutputDescription::clear_depth(float depth) {
  clear_depth_ = depth;
  return *this;
}

FrameGraphAttachmentOutputDescription&
FrameGraphAttachmentOutputDescription::clear_stencil(int stencil) {
  clear_stencil_ = stencil;
  return *this;
}

FrameGraphRenderPassDescription& FrameGraphRenderPassDescription::add_input(
    FrameGraphInputDescription desc) {
  inputs_.push_back(std::move(desc));
  return *this;
}

FrameGraphRenderPassDescription& FrameGraphRenderPassDescription::add_output(
    FrameGraphOutputDescription desc) {
  outputs_.push_back(std::move(desc));
  return *this;
}

FrameGraphDescription& FrameGraphDescription::add_render_pass(
    FrameGraphRenderPassDescription render_pass) {
  render_passes_.push_back(std::move(render_pass));
  return *this;
}

} // namespace rndrx