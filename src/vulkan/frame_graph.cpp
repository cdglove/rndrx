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
#include "rndrx/vulkan/frame_graph.hpp"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <unordered_map>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "rndrx/assert.hpp"
#include "rndrx/frame_graph_description.hpp"
#include "rndrx/log.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/formats.hpp"
#include "rndrx/vulkan/frame_graph_builder.hpp"
#include "rndrx/vulkan/vma/image.hpp"

namespace rndrx::vulkan {

namespace {
std::string to_string(std::string_view v) {
  return std::string(v.begin(), v.end());
}
} // namespace

FrameGraphAttachment::FrameGraphAttachment(
    Device& device,
    FrameGraphAttachmentOutputDescription const& description) {
  image_ = vma::Image(
      device.allocator(),
      vk::ImageCreateInfo() //
          .setFormat(to_vulkan_format(description.format()))
          .setImageType(vk::ImageType::e2D)
          .setExtent(vk::Extent3D(description.width(), description.height(), 1))
          .setUsage(vk::ImageUsageFlagBits::eColorAttachment));
}

FrameGraphBuffer::FrameGraphBuffer(
    Device& device,
    FrameGraphBufferDescription const& description) {
  RNDRX_ASSERT(false && "Not implemented");
  buffer_ = vma::Buffer(device.allocator(), vk::BufferCreateInfo());
}

FrameGraphNode::FrameGraphNode(std::string name, FrameGraphRenderPass* render_pass)
    : name_(std::move(name))
    , render_pass_(render_pass) {
}

void FrameGraphNode::create_vk_render_pass(Device& device) {
  std::array<vk::AttachmentDescription, 32> attachments;
  vk::RenderPassCreateInfo create_info;
  create_info.setAttachments(attachments);
  vk_render_pass_ = device.vk().createRenderPass(create_info);
}

FrameGraph::FrameGraph(
    FrameGraphBuilder const& builder,
    FrameGraphDescription const& description) {
  parse_description(builder, description);
  build_edges();
  sort_nodes();
  allocate_graphics_resources();
}

void FrameGraph::render(Device& device) {
  vk::raii::CommandBuffer cmd = device.alloc_graphics_command_buffer();
  for(auto&& node : sorted_nodes_) {
    // for(auto&& input : node->inputs()) {
    //   if(FrameGraphAttachment const* attachment = input->attachment()) {
    //     // Transition image accordingly.
    //   }
    // }

    vk::Extent2D image_size;
    // for(auto&& output : node->outputs()) {
    //   if(FrameGraphAttachment const* attachment = output->attachment()) {
    //     // Transition image accordingly.
    //     attachment->image();
    //   }
    // }

    vk::Rect2D scissor{{0, 0}, image_size};
    cmd.setScissor(0, scissor);

    vk::Viewport viewport{
        0,
        0,
        static_cast<float>(image_size.width),
        static_cast<float>(image_size.height),
        0.f,
        1.f};
    cmd.setViewport(0, viewport);

    node->render_pass()->pre_render(cmd);

    // Begin rendering

    node->render_pass()->render(cmd);

    // Finish rendering

    node->render_pass()->post_render(cmd);
  }

  // device.graphics_queue().submit()
}

void FrameGraph::parse_description(
    FrameGraphBuilder const& builder,
    FrameGraphDescription const& description) {
  nodes_.reserve(description.passes().size());
  for(auto&& pass : description.passes()) {
    auto render_impl = builder.find_render_pass(pass.name());
    if(render_impl == nullptr) {
      RNDRX_THROW_RUNTIME_ERROR()
          << "Failed to find render pass implementation named " << pass.name()
          << ". Did you forget at call add_render_pass()?";
    }

    nodes_.insert(FrameGraphNode(std::string(pass.name()), render_impl));
  }

  for(auto&& pass : description.passes()) {
    auto producer = find_node(pass.name());
    for(auto&& output : pass.outputs()) {
      auto& output_named_object = std::visit(
          FrameGraphNamedObjectFromResourceDescription(),
          output);
      std::string output_name{output_named_object.name()};
      if(resources_.contains(output_name)) {
        RNDRX_THROW_RUNTIME_ERROR()
            << "Found duplicate output " << quote(output_name) << " on pass "
            << quote(pass.name());
      }

      resources_.insert(FrameGraphResource(std::move(output_name), producer));
    }
  }

  for(auto&& pass : description.passes()) {
    std::vector<FrameGraphResource const*> inputs =
        pass.inputs() |
        std::views::transform([this, &pass](auto const& description) {
          auto& named_object = std::visit(
              FrameGraphNamedObjectFromResourceDescription(),
              description);
          FrameGraphResource const* input = find_resource(named_object.name());
          if(input == nullptr) {
            RNDRX_THROW_RUNTIME_ERROR()
                << "Failed to find output named " << quote(named_object.name())
                << " for pass " << quote(pass.name());
          }
          return input;
        }) |
        to_vector;

    std::vector<FrameGraphResource const*> outputs =
        pass.outputs() |
        std::views::transform([this, &pass](auto const& description) {
          auto& named_object = std::visit(
              FrameGraphNamedObjectFromResourceDescription(),
              description);
          FrameGraphResource const* output = find_resource(named_object.name());
          RNDRX_ASSERT(
              output && "Output should have been found due to first pass.");
          return output;
        }) |
        to_vector;

    auto node = find_node(pass.name());
    node->set_resource_data(std::move(inputs), std::move(outputs));
  }
}

void FrameGraph::build_edges() {
  for(auto&& node : nodes_) {
    for(auto&& input : node.inputs()) {
      FrameGraphNode* producer = input->producer();
      producer->add_dependent(const_cast<FrameGraphNode*>(&node));
    }
  }
}

namespace {
void sort_nodes_recursive(
    FrameGraphNode* node,
    std::vector<FrameGraphNode*>& sorted_nodes,
    std::unordered_map<FrameGraphNode const*, bool>& added) {
  if(node->dependents().empty()) {
    auto is_added = added.find(node);
    if(!is_added->second) {
      is_added->second = true;
      sorted_nodes.push_back(node);
    }
    return;
  }

  for(auto child : node->dependents()) {
    sort_nodes_recursive(child, sorted_nodes, added);
  }

  auto is_added = added.find(node);
  if(!is_added->second) {
    is_added->second = true;
    sorted_nodes.push_back(node);
  }
}

} // namespace

void FrameGraph::sort_nodes() {
  std::unordered_map<FrameGraphNode const*, bool> added;
  for(auto&& node : nodes_) {
    added.insert(std::make_pair(&node, false));
  }

  sorted_nodes_.reserve(nodes_.size());
  for(auto&& node : nodes_) {
    sort_nodes_recursive(const_cast<FrameGraphNode*>(&node), sorted_nodes_, added);
  }

  std::ranges::reverse(sorted_nodes_);
}

void FrameGraph::allocate_graphics_resources() {
  // for(auto&& resource : resources_) {
  //   resource.
}

FrameGraphResource* FrameGraph::find_resource(std::string_view name) {
  auto resource = resources_.find(to_string(name));
  if(resource != resources_.end()) {
    return const_cast<FrameGraphResource*>(&*resource);
  }

  return nullptr;
}

FrameGraphNode* FrameGraph::find_node(std::string_view name) {
  auto node = nodes_.find(to_string(name));
  if(node != nodes_.end()) {
    return const_cast<FrameGraphNode*>(&*node);
  }

  return nullptr;
}

} // namespace rndrx::vulkan