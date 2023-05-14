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
#ifndef RNDRX_FRAMEGRAPHDESCRIPTION_HPP_
#define RNDRX_FRAMEGRAPHDESCRIPTION_HPP_
#pragma once

#include "rndrx/config.hpp"

#include <glm/vec4.hpp>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>
#include "rndrx/attachment_ops.hpp"
#include "rndrx/formats.hpp"

namespace rndrx {

class FrameGraphRenderPass {
 public:
};

class FrameGraphNamedObject {
 public:
  FrameGraphNamedObject() = default;
  FrameGraphNamedObject(std::string name);
  FrameGraphNamedObject& name(std::string name);
  std::string_view name() const {
    return name_;
  }

 private:
  std::string name_;
};

class FrameGraphAttachmentOutputDescription : public FrameGraphNamedObject {
 public:
  using FrameGraphNamedObject::FrameGraphNamedObject;
  FrameGraphAttachmentOutputDescription& format(ImageFormat format);
  FrameGraphAttachmentOutputDescription& resolution(int width, int height);
  FrameGraphAttachmentOutputDescription& load_op(AttachmentLoadOp op);
  FrameGraphAttachmentOutputDescription& clear_colour(glm::vec4 colour);
  FrameGraphAttachmentOutputDescription& clear_depth(float depth);
  FrameGraphAttachmentOutputDescription& clear_stencil(int stencil);

  ImageFormat format() const {
    return format_;
  }

  int width() const {
    return width_;
  }

  int height() const {
    return height_;
  }

  AttachmentLoadOp load_op() const {
    return load_op_;
  }

  glm::vec4 clear_colour() const {
    return clear_colour_;
  }

  float clear_depth() const {
    return clear_depth_;
  }

  int clear_stencil() const {
    return clear_stencil_;
  }

 private:
  ImageFormat format_ = ImageFormat::Undefined;
  int width_ = 0;
  int height_ = 0;
  AttachmentLoadOp load_op_ = AttachmentLoadOp::DontCare;
  glm::vec4 clear_colour_;
  float clear_depth_ = 0.f;
  int clear_stencil_ = 0;
};

class FrameGraphAttachmentInputDescription : public FrameGraphNamedObject {
 public:
  using FrameGraphNamedObject::FrameGraphNamedObject;
};

class FrameGraphInputImageDescription : public FrameGraphNamedObject {
 public:
  using FrameGraphNamedObject::FrameGraphNamedObject;
};

class FrameGraphBufferDescription : public FrameGraphNamedObject {
 public:
  using FrameGraphNamedObject::FrameGraphNamedObject;
};

using FrameGraphInputDescription = std::variant< //
    FrameGraphAttachmentInputDescription,
    FrameGraphInputImageDescription,
    FrameGraphBufferDescription>;

using FrameGraphOutputDescription = std::variant< //
    FrameGraphAttachmentOutputDescription,
    FrameGraphBufferDescription>;

class FrameGraphRenderPassDescription : public FrameGraphNamedObject {
 public:
  using FrameGraphNamedObject::FrameGraphNamedObject;
  FrameGraphRenderPassDescription& add_input(FrameGraphInputDescription input);
  FrameGraphRenderPassDescription& add_output(FrameGraphOutputDescription output);

  std::span<FrameGraphInputDescription const> inputs() const {
    return inputs_;
  }

  std::span<FrameGraphOutputDescription const> outputs() const {
    return outputs_;
  }

 private:
  std::vector<FrameGraphInputDescription> inputs_;
  std::vector<FrameGraphOutputDescription> outputs_;
};

class FrameGraphDescription {
 public:
  FrameGraphDescription() = default;
  FrameGraphDescription& add_render_pass(FrameGraphRenderPassDescription render_pass);

  std::span<FrameGraphRenderPassDescription const> passes() const {
    return render_passes_;
  }

 private:
  std::vector<FrameGraphRenderPassDescription> render_passes_;
};

struct FrameGraphNamedObjectFromResourceDescription {
  FrameGraphNamedObject const& operator()(
      FrameGraphAttachmentInputDescription const& obj) const {
    return obj;
  }

  FrameGraphNamedObject const& operator()(
      FrameGraphInputImageDescription const& obj) const {
    return obj;
  }

  FrameGraphNamedObject const& operator()(
      FrameGraphAttachmentOutputDescription const& obj) const {
    return obj;
  }

  FrameGraphNamedObject const& operator()(FrameGraphBufferDescription const& obj) const {
    return obj;
  }

  FrameGraphNamedObject const& operator()(
      FrameGraphRenderPassDescription const& obj) const {
    return obj;
  }
};

} // namespace rndrx

#endif // RNDRX_FRAMEGRAPHDESCRIPTION_HPP_