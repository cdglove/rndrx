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
#include <memory>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <xutility>
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

vk::AttachmentLoadOp to_vulkan_load_op(AttachmentLoadOp op) {
  switch(op) {
    case AttachmentLoadOp::Load:
      return vk::AttachmentLoadOp::eLoad;
    case AttachmentLoadOp::Clear:
      return vk::AttachmentLoadOp::eClear;
    case AttachmentLoadOp::DontCare:
      return vk::AttachmentLoadOp::eDontCare;
    case AttachmentLoadOp::None:
      return vk::AttachmentLoadOp::eNoneEXT;
    default:
      RNDRX_ASSERT(false);
  };
}

vk::AttachmentStoreOp to_vulkan_store_op(AttachmentStoreOp op) {
  switch(op) {
    case AttachmentStoreOp::Store:
      return vk::AttachmentStoreOp::eStore;
    case AttachmentStoreOp::DontCare:
      return vk::AttachmentStoreOp::eDontCare;
    case AttachmentStoreOp::None:
      return vk::AttachmentStoreOp::eNone;
    default:
      RNDRX_ASSERT(false);
  };
}

} // namespace

FrameGraphAttachment::FrameGraphAttachment(
    Device& device,
    FrameGraphAttachmentOutputDescription const& description) {
  format_ = to_vulkan_format(description.format());
  width_ = description.width();
  height_ = description.height();
  load_op_ = to_vulkan_load_op(description.load_op());
  clear_colour_ = description.clear_colour();
  clear_depth_ = description.clear_depth();
  clear_stencil_ = description.clear_stencil();

  image_ = device.allocator().create_image( //
      vk::ImageCreateInfo()
          .setFormat(format_)
          .setImageType(vk::ImageType::e2D)
          .setExtent(vk::Extent3D(width_, height_, 1))
          .setUsage(
              vk::ImageUsageFlagBits::eColorAttachment |
              vk::ImageUsageFlagBits::eInputAttachment)
          .setMipLevels(1)
          .setArrayLayers(1));

  image_view_ = device.vk().createImageView(
      vk::ImageViewCreateInfo()
          .setImage(*image_.vk())
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(format_)
          .setSubresourceRange( //
              vk::ImageSubresourceRange()
                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                  .setBaseMipLevel(0)
                  .setLevelCount(1)
                  .setBaseArrayLayer(0)
                  .setLayerCount(1)));

#if RNDRX_ENABLE_VULKAN_DEBUG_LAYER
  VkImage image = *image_.vk();
  std::uint64_t image_handle = reinterpret_cast<std::uint64_t>(image);
  device.vk().setDebugUtilsObjectNameEXT( //
      vk::DebugUtilsObjectNameInfoEXT()
          .setObjectType(image_.vk().objectType)
          .setObjectHandle(image_handle)
          .setPObjectName(description.name().data()));

  VkImageView image_view = *image_view_;
  std::uint64_t image_view_handle = reinterpret_cast<std::uint64_t>(image);
  device.vk().setDebugUtilsObjectNameEXT( //
      vk::DebugUtilsObjectNameInfoEXT()
          .setObjectType(image_.vk().objectType)
          .setObjectHandle(image_view_handle)
          .setPObjectName(description.name().data()));
#endif
}

vma::Image const& FrameGraphAttachment::image() const {
  return image_;
}

vk::raii::ImageView const& FrameGraphAttachment::image_view() const {
  return image_view_;
}

vk::Format FrameGraphAttachment::format() const {
  return format_;
}

int FrameGraphAttachment::width() const {
  return width_;
}

int FrameGraphAttachment::height() const {
  return height_;
}

vk::AttachmentLoadOp FrameGraphAttachment::load_op() const {
  return load_op_;
}

glm::vec4 FrameGraphAttachment::clear_colour() const {
  return clear_colour_;
}

float FrameGraphAttachment::clear_depth() const {
  return clear_depth_;
}

int FrameGraphAttachment::clear_stencil() const {
  return clear_stencil_;
}

FrameGraphBuffer::FrameGraphBuffer(
    Device& device,
    FrameGraphBufferDescription const& description) {
  RNDRX_ASSERT(false && "Not implemented");
  buffer_ = vma::Buffer(device.allocator(), vk::BufferCreateInfo());
}

void FrameGraphResource::set_render_resource(FrameGraphAttachment* attachment) {
  render_resource_ = attachment;
}

void FrameGraphResource::set_render_resource(FrameGraphBuffer* buffer) {
  render_resource_ = buffer;
}

FrameGraphAttachment* FrameGraphResource::get_attachment() const {
  FrameGraphAttachment* const* ret_ref = std::get_if<FrameGraphAttachment*>(
      &render_resource_);
  if(ret_ref) {
    return *ret_ref;
  }
  return nullptr;
}

FrameGraphNode::FrameGraphNode(std::string name, FrameGraphRenderPass* render_pass)
    : name_(std::move(name))
    , render_pass_(render_pass) {
}

void FrameGraphNode::create_vk_render_pass(Device& device) {
  int pass_width = outputs_[0]->get_attachment()->width();
  int pass_height = outputs_[0]->get_attachment()->height();

  std::vector<vk::AttachmentReference> input_attachment_references;
  input_attachment_references.reserve(inputs_.size());

  std::vector<vk::AttachmentReference> output_attachment_references;
  input_attachment_references.reserve(outputs_.size());

  std::vector<vk::AttachmentDescription> attachments;
  attachments.reserve(inputs_.size() + outputs_.size());

  std::vector<vk::ImageView> framebuffer_views;
  framebuffer_views.reserve(inputs_.size() + outputs_.size());
  for(auto&& input : inputs_) {
    FrameGraphAttachment* attachment = input->get_attachment();
    // Could be a buffer or image which are not created as part of the render pass.
    if(attachment == nullptr) {
      continue;
    }

    RNDRX_ASSERT(attachment->width() == pass_width);
    RNDRX_ASSERT(attachment->height() == pass_height);

    input_attachment_references.push_back(
        vk::AttachmentReference()
            .setAttachment(attachments.size())
            .setLayout(vk::ImageLayout::eReadOnlyOptimal));

    attachments.push_back(vk::AttachmentDescription()
                              .setFormat(attachment->format())
                              .setLoadOp(vk::AttachmentLoadOp::eLoad)
                              .setStoreOp(vk::AttachmentStoreOp::eNone)
                              .setSamples(vk::SampleCountFlagBits::e1)
                              .setInitialLayout(vk::ImageLayout::eReadOnlyOptimal)
                              .setFinalLayout(vk::ImageLayout::eReadOnlyOptimal));

    framebuffer_views.push_back(*attachment->image_view());
  }

  for(auto&& output : outputs_) {
    FrameGraphAttachment* attachment = output->get_attachment();
    // Could be a buffer or image which are not created as part of the render pass.
    if(attachment == nullptr) {
      continue;
    }

    RNDRX_ASSERT(attachment->width() == pass_width);
    RNDRX_ASSERT(attachment->height() == pass_height);

    output_attachment_references.push_back(
        vk::AttachmentReference()
            .setAttachment(attachments.size())
            .setLayout(vk::ImageLayout::eColorAttachmentOptimal));

    attachments.push_back(
        vk::AttachmentDescription()
            .setFormat(attachment->format())
            .setLoadOp(attachment->load_op())
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eReadOnlyOptimal));

    framebuffer_views.push_back(*attachment->image_view());
  }

  vk_render_pass_ = device.vk().createRenderPass( //
      vk::RenderPassCreateInfo()
          .setAttachments(attachments)
          .setSubpasses( //
              vk::SubpassDescription()
                  .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                  .setInputAttachments(input_attachment_references)
                  .setColorAttachments(output_attachment_references)));

  vk_frame_buffer_ = device.vk().createFramebuffer( //
      vk::FramebufferCreateInfo()
          .setRenderPass(*vk_render_pass_)
          .setWidth(pass_width)
          .setHeight(pass_height)
          .setLayers(1)
          .setAttachments(framebuffer_views));
}

void FrameGraphNode::set_resources(
    std::vector<FrameGraphResource const*> inputs,
    std::vector<FrameGraphResource const*> outputs) {
  inputs_ = std::move(inputs);
  outputs_ = std::move(outputs);
}

FrameGraph::FrameGraph(
    FrameGraphBuilder const& builder,
    FrameGraphDescription const& description) {
  parse_description(builder, description);
  build_edges();
  sort_nodes();
  allocate_graphics_resources(builder.device(), description);
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
    node->set_resources(std::move(inputs), std::move(outputs));
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

void FrameGraph::allocate_graphics_resources(
    Device& device,
    FrameGraphDescription const& description) {
  struct ResourceAllocator {
    ResourceAllocator(Device& device, FrameGraph& target)
        : device_(device)
        , target_(target) {
    }

    void operator()(FrameGraphAttachmentOutputDescription const& description) {
      FrameGraphResource* resource = target_.find_resource(description.name());
      RNDRX_ASSERT(resource);
      target_.attachments_.push_back(
          std::make_unique<FrameGraphAttachment>(device_, description));
      resource->set_render_resource(target_.attachments_.back().get());
    }

    void operator()(FrameGraphBufferDescription const& description) {
      FrameGraphResource* resource = target_.find_resource(description.name());
      RNDRX_ASSERT(resource);
      target_.buffers_.push_back(
          std::make_unique<FrameGraphBuffer>(device_, description));
      resource->set_render_resource(target_.buffers_.back().get());
    }

    FrameGraph& target_;
    Device& device_;
  };

  ResourceAllocator resource_allocator(device, *this);
  for(auto&& pass : description.passes()) {
    for(auto&& output : pass.outputs()) {
      std::visit(resource_allocator, output);
    }

    FrameGraphNode* node = find_node(pass.name());
    RNDRX_ASSERT(node && "Not possible for a node to not exist at this point.");
    node->create_vk_render_pass(device);
  }
}

FrameGraphResource* FrameGraph::find_resource(std::string_view name) {
  auto resource = resources_.find(name);
  if(resource != resources_.end()) {
    // const_cast required because a value is const in a set.
    // It's safe as long as we don't change the name.
    return const_cast<FrameGraphResource*>(&*resource);
  }

  return nullptr;
}

FrameGraphNode* FrameGraph::find_node(std::string_view name) {
  auto node = nodes_.find(name);
  if(node != nodes_.end()) {
    // const_cast required because a value is const in a set.
    // It's safe as long as we don't change the name.
    return const_cast<FrameGraphNode*>(&*node);
  }

  return nullptr;
}

} // namespace rndrx::vulkan