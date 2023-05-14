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

#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
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

class FrameGraphRenderPass {
 public:
  virtual void pre_render(vk::raii::CommandBuffer& cmd) = 0;
  virtual void render(vk::raii::CommandBuffer& cmd) = 0;
  virtual void post_render(vk::raii::CommandBuffer& cmd) = 0;
};

class FrameGraphAttachment {
 public:
  FrameGraphAttachment(
      Device& device,
      FrameGraphAttachmentOutputDescription const& description);

  vma::Image const& image() const {
    return image_;
  }

 private:
  vma::Image image_ = nullptr;
};

// class FrameGraphImage {
//  public:
//   vk::Image image() const {
//     return image_;
//   }

//  private:
//   vk::Image image_;
// };

class FrameGraphBuffer {
 public:
  FrameGraphBuffer(Device& device, FrameGraphBufferDescription const& description);
  vma::Buffer const& buffer() const {
    return buffer_;
  }

 private:
  vma::Buffer buffer_ = nullptr;
};

class FrameGraphResource {
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

 private:
  std::variant< //
      std::nullptr_t,
      FrameGraphAttachment*,
      FrameGraphBuffer*>
      render_resource_ = nullptr;
  FrameGraphNode* producer_ = nullptr;
  std::string name_;
};

class FrameGraphNode {
 public:
  FrameGraphNode(std::string name, FrameGraphRenderPass* render_pass);

  void create_vk_render_pass(Device& device);

  void set_resource_data(
      std::vector<FrameGraphResource const*> inputs,
      std::vector<FrameGraphResource const*> outputs) {
    inputs_ = std::move(inputs);
    outputs_ = std::move(outputs);
  }

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

class FrameGraph {
 public:
  FrameGraph(FrameGraphBuilder const& builder, FrameGraphDescription const& description);
  void render(Device& device);

 private:
  void parse_description(
      FrameGraphBuilder const& builder,
      FrameGraphDescription const& description);
  void build_edges();
  void sort_nodes();
  void allocate_graphics_resources();

  FrameGraphResource* find_resource(std::string_view name);
  FrameGraphNode* find_node(std::string_view name);

  // Custom hashers and comparers so we can use the name
  // field in the object itself. Saves a bunch of conversions
  // and some memory.
  struct HashName {
    using is_transparent = std::true_type;

    template <typename NamedObject>
    std::size_t operator()(NamedObject&& obj) const {
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

    template <typename NamedObject>
    std::size_t operator()(NamedObject&& a, NamedObject&& b) const {
      return a.name() == b.name();
    }

    template <typename NamedObject>
    std::size_t operator()(NamedObject&& obj, std::string_view name) const {
      return obj.name() == name;
    }

    template <typename NamedObject>
    std::size_t operator()(std::string_view name, NamedObject&& obj) const {
      return name == obj.name();
    }

    template <typename NamedObject>
    std::size_t operator()(NamedObject&& obj, std::string const& name) const {
      return obj.name() == name;
    }

    template <typename NamedObject>
    std::size_t operator()(std::string const& name, NamedObject&& obj) const {
      return name == obj.name();
    }
  };

  // cglover: There should be an optimisation here where we ue sets
  // instead of maps
  std::unordered_set<FrameGraphResource, HashName, EqualName> resources_;
  std::unordered_set<FrameGraphNode, HashName, EqualName> nodes_;
  std::vector<FrameGraphNode*> sorted_nodes_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_FRAMEGRAPH_HPP_