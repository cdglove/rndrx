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
#include "rndrx/vulkan/submission_context.hpp"
#pragma once

#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "glm/vec4.hpp"
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"
#include "rndrx/vulkan/vma/image.hpp"

namespace rndrx {
class FrameGraphAttachmentOutputDescription;
class FrameGraphBufferDescription;
class FrameGraphDescription;
} // namespace rndrx

namespace rndrx::vulkan {

class Device;
class FrameGraphBuilder;
class FrameGraphNode;
class FrameGraph;
class SubmissionContext;

class FrameGraphRenderPass {
 public:
  virtual void pre_render(SubmissionContext& sc) = 0;
  virtual void render(SubmissionContext& sc) = 0;
  virtual void post_render(SubmissionContext& sc) = 0;
};

class FrameGraphAttachment : noncopyable {
 public:
  FrameGraphAttachment(
      Device& device,
      FrameGraphAttachmentOutputDescription const& description);

  vma::Image const& image() const;
  vk::raii::ImageView const& image_view() const;
  vk::Format format() const;
  int width() const;
  int height() const;
  vk::AttachmentLoadOp load_op() const;
  glm::vec4 clear_colour() const;
  float clear_depth() const;
  int clear_stencil() const;

 private:
  vma::Image image_ = nullptr;
  vk::raii::ImageView image_view_ = nullptr;
  vk::Format format_ = vk::Format::eUndefined;
  int width_ = 0;
  int height_ = 0;
  vk::AttachmentLoadOp load_op_ = vk::AttachmentLoadOp::eDontCare;
  glm::vec4 clear_colour_;
  float clear_depth_ = 0.f;
  int clear_stencil_ = 0;
};

// class FrameGraphImage {
//  public:
//   vk::Image image() const {
//     return image_;
//   }

//  private:
//   vk::Image image_;
// };

class FrameGraphBuffer : noncopyable {
 public:
  FrameGraphBuffer(Device& device, FrameGraphBufferDescription const& description);
  vma::Buffer const& buffer() const {
    return buffer_;
  }

 private:
  vma::Buffer buffer_ = nullptr;
};

class FrameGraphResource : noncopyable {
 public:
  FrameGraphResource(std::string name, FrameGraphNode* producer)
      : name_(std::move(name))
      , producer_(producer) {
  }

  std::string_view name() const {
    return name_;
  }

  FrameGraphNode* producer() const {
    return producer_;
  }

  void set_render_resource(FrameGraphAttachment* attachment);
  void set_render_resource(FrameGraphBuffer* buffer);

  FrameGraphAttachment* get_attachment() const;

 private:
  std::variant< //
      std::nullptr_t,
      FrameGraphAttachment*,
      FrameGraphBuffer*>
      render_resource_ = nullptr;

  FrameGraphNode* producer_ = nullptr;
  std::string name_;
};

class FrameGraphNode : noncopyable {
 public:
  FrameGraphNode(std::string name, FrameGraphRenderPass* render_pass);

  void create_vk_render_pass(Device& device);

  void set_resources(
      std::vector<FrameGraphResource const*> inputs,
      std::vector<FrameGraphResource const*> outputs);

  void add_dependent(FrameGraphNode* node) {
    dependents_.push_back(node);
  }

  FrameGraphRenderPass* render_pass() const {
    return render_pass_;
  }

  vk::RenderPass vk_render_pass() const {
    return *vk_render_pass_;
  }

  std::span<FrameGraphResource const* const> inputs() const {
    return inputs_;
  }

  std::span<FrameGraphResource const* const> outputs() const {
    return outputs_;
  }

  std::span<FrameGraphNode* const> dependents() const {
    return dependents_;
  }

  std::string_view name() const {
    return name_;
  }

 private:
  vk::raii::RenderPass vk_render_pass_ = nullptr;
  vk::raii::Framebuffer vk_frame_buffer_ = nullptr;
  FrameGraphRenderPass* render_pass_ = nullptr;
  std::vector<FrameGraphResource const*> inputs_;
  std::vector<FrameGraphResource const*> outputs_;
  std::vector<FrameGraphNode*> dependents_;
  std::string name_;
};

class FrameGraph : noncopyable {
 public:
  FrameGraph() = default;
  FrameGraph(FrameGraphBuilder const& builder, FrameGraphDescription const& description);
  void render(SubmissionContext& sc);
  FrameGraphNode* find_node(std::string_view name);

 private:
  void parse_description(
      FrameGraphBuilder const& builder,
      FrameGraphDescription const& description);
  void build_edges();
  void sort_nodes();
  void allocate_graphics_resources(Device& device, FrameGraphDescription const& description);

  FrameGraphResource* find_resource(std::string_view name);

  // Custom hashers and comparers so we can use the name
  // field in the object itself. Saves a bunch of conversions
  // and some memory. The consequence of this is that the set value
  // type in const (to avoid corrupting the data structure), so we
  // need to const cast to get mutable types out of the sets.
  struct HashName {
    using is_transparent = std::true_type;

    std::size_t operator()(FrameGraphResource const& obj) const {
      return std::hash<std::string_view>()(obj.name());
    }

    std::size_t operator()(FrameGraphNode const& obj) const {
      return std::hash<std::string_view>()(obj.name());
    }

    std::size_t operator()(std::string_view name) const {
      return std::hash<std::string_view>()(name);
    }

    std::size_t operator()(std::string const& name) const {
      return std::hash<std::string_view>()(name);
    }
  };

  struct EqualName {
    using is_transparent = std::true_type;

    std::size_t operator()(FrameGraphResource const& a, FrameGraphResource const& b) const {
      return a.name() == b.name();
    }

    std::size_t operator()(FrameGraphResource const& obj, std::string_view name) const {
      return obj.name() == name;
    }

    std::size_t operator()(std::string_view name, FrameGraphResource const& obj) const {
      return name == obj.name();
    }

    std::size_t operator()(FrameGraphNode const& a, FrameGraphNode const& b) const {
      return a.name() == b.name();
    }

    std::size_t operator()(FrameGraphNode const& obj, std::string_view name) const {
      return obj.name() == name;
    }

    std::size_t operator()(std::string_view name, FrameGraphNode const& obj) const {
      return name == obj.name();
    }
  };

  std::unordered_set<FrameGraphResource, HashName, EqualName> resources_;
  std::unordered_set<FrameGraphNode, HashName, EqualName> nodes_;
  std::vector<FrameGraphNode*> sorted_nodes_;
  std::vector<std::unique_ptr<FrameGraphAttachment>> attachments_;
  std::vector<std::unique_ptr<FrameGraphBuffer>> buffers_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_FRAMEGRAPH_HPP_