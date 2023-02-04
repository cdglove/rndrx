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
#include "rndrx/vulkan/model.hpp"

#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <numeric>
#include <ranges>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "glm/gtx/dual_quaternion.hpp"
#include "rndrx/assert.hpp"
#include "rndrx/log.hpp"
#include "rndrx/throw_exception.hpp"
#include "rndrx/to_vector.hpp"
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/material.hpp"
#include "rndrx/vulkan/texture.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"

namespace rndrx::vulkan {

Node::Node(Node const* parent)
    : parent(parent) {
}

glm::mat4 Node::local_matrix() const {
  return glm::translate(glm::mat4(1.f), translation) * glm::mat4(rotation) *
         glm::scale(glm::mat4(1.f), scale) * matrix;
}

glm::mat4 Node::resolve_transform_hierarchy() const {
  glm::mat4 m = local_matrix();
  Node const* p = parent;
  while(p) {
    m = p->local_matrix() * m;
    p = p->parent;
  }
  return m;
}

void Node::update() {
  if(mesh) {
    glm::mat4 m = resolve_transform_hierarchy();
    mesh->set_world_matrix(m);
    if(skeleton) {
      glm::mat4 inverse_transform = glm::inverse(m);
      size_t num_joints = std::min(skeleton->joints.size(), kMaxNumJoints);
      mesh->set_num_joints(num_joints);
      for(size_t i = 0; i < num_joints; i++) {
        Node const* joint_node = skeleton->joints[i];
        glm::mat4 joint_mat = joint_node->resolve_transform_hierarchy() *
                              skeleton->inverse_bind_matrices[i];
        joint_mat = inverse_transform * joint_mat;
        mesh->set_joint_matrix(i, joint_mat);
      }
      // memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock,
      // sizeof(mesh->uniformBlock));
    }
    else {
      // memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
    }
  }

  for(auto& child : children) {
    child.update();
  }
}

void Model::create_device_buffers(
    Device& device,
    std::vector<std::uint32_t> const& index_buffer,
    std::vector<Model::Vertex> const& vertex_buffer) {
  std::size_t vertex_buffer_size = vertex_buffer.size() * sizeof(Vertex);
  std::size_t index_buffer_size = index_buffer.size() * sizeof(std::uint32_t);

  RNDRX_ASSERT(vertex_buffer_size > 0);

  vma::Buffer vertex_staging = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(vertex_buffer_size)
          .setUsage(vk::BufferUsageFlagBits::eTransferSrc));

  vma::Buffer index_staging = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(index_buffer_size)
          .setUsage(vk::BufferUsageFlagBits::eTransferSrc));

  vertices_ = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(vertex_buffer_size)
          .setUsage(
              vk::BufferUsageFlagBits::eTransferDst |
              vk::BufferUsageFlagBits::eVertexBuffer));

  std::memcpy(vertices_.mapped_data(), vertex_buffer.data(), vertex_buffer_size);

  indices_ = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(index_buffer_size)
          .setUsage(
              vk::BufferUsageFlagBits::eTransferDst |
              vk::BufferUsageFlagBits::eIndexBuffer));

  std::memcpy(indices_.mapped_data(), index_buffer.data(), index_buffer_size);

  auto transfer_cmd = device.alloc_transfer_command_buffer();
  transfer_cmd.begin( //
      vk::CommandBufferBeginInfo().setFlags(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  transfer_cmd.copyBuffer(
      *vertex_staging.vk(),
      *vertices_.vk(),
      vk::BufferCopy(0, 0, vertex_buffer_size));

  transfer_cmd.copyBuffer(
      *index_staging.vk(),
      *indices_.vk(),
      vk::BufferCopy(0, 0, index_buffer_size));

  transfer_cmd.end();

  vk::raii::Fence fence = device.vk().createFence({});

  device.transfer_queue().submit2(
      vk::SubmitInfo2() //
          .setCommandBufferInfos(
              vk::CommandBufferSubmitInfo().setCommandBuffer(*transfer_cmd)),
      *fence);
  vk::Result wait_result = device.vk().waitForFences(
      *fence,
      VK_TRUE,
      std::numeric_limits<std::uint64_t>::max());

  if(wait_result != vk::Result::eSuccess) {
    throw_runtime_error("Failed to wait for fence.");
  }
}

// void Model::drawNode(Node* node, VkCommandBuffer commandBuffer) {
//   if(node->mesh) {
//     for(Primitive* primitive : node->mesh->primitives) {
//       vkCmdDrawIndexed(
//           commandBuffer,
//           primitive->index_count,
//           1,
//           primitive->firstIndex,
//           0,
//           0);
//     }
//   }
//   for(auto& child : node->children) {
//     drawNode(child, commandBuffer);
//   }
// }

// void Model::draw(VkCommandBuffer commandBuffer) {
//   const VkDeviceSize offsets[1] = {0};
//   vkCmdBindvertex_buffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
//   vkCmdBindindex_buffer(commandBuffer, indices.buffer, 0,
//   VK_INDEX_TYPE_UINT32); for(auto& node : nodes) {
//     drawNode(node, commandBuffer);
//   }
// }

// void Model::calculateBoundingBox(Node* node, Node* parent) {
//   BoundingBox parentBvh = parent ? parent->bvh
//                                  : BoundingBox(dimensions.min, dimensions.max);

//   if(node->mesh) {
//     if(node->mesh->bb.valid) {
//       node->aabb = node->mesh->bb.getAABB(node->getMatrix());
//       if(node->children.size() == 0) {
//         node->bvh.min = node->aabb.min;
//         node->bvh.max = node->aabb.max;
//         node->bvh.valid = true;
//       }
//     }
//   }

//   parentBvh.min = glm::min(parentBvh.min, node->bvh.min);
//   parentBvh.max = glm::min(parentBvh.max, node->bvh.max);

//   for(auto& child : node->children) {
//     calculateBoundingBox(child, node);
//   }
// }

// void Model::getSceneDimensions() {
//   // Calculate binary volume hierarchy for all nodes in the scene
//   for(auto node : linearNodes) {
//     calculateBoundingBox(node, nullptr);
//   }

//   dimensions.min = glm::vec3(FLT_MAX);
//   dimensions.max = glm::vec3(-FLT_MAX);

//   for(auto node : linearNodes) {
//     if(node->bvh.valid) {
//       dimensions.min = glm::min(dimensions.min, node->bvh.min);
//       dimensions.max = glm::max(dimensions.max, node->bvh.max);
//     }
//   }

//   // Calculate scene aabb
//   aabb = glm::scale(
//       glm::mat4(1.f),
//       glm::vec3(
//           dimensions.max[0] - dimensions.min[0],
//           dimensions.max[1] - dimensions.min[1],
//           dimensions.max[2] - dimensions.min[2]));
//   aabb[3][0] = dimensions.min[0];
//   aabb[3][1] = dimensions.min[1];
//   aabb[3][2] = dimensions.min[2];
// }

// void Model::updateAnimation(uint32_t index, float time) {
//   if(animations.empty()) {
//     std::cout << ".glTF does not contain animation." << std::endl;
//     return;
//   }
//   if(index > static_cast<uint32_t>(animations.size()) - 1) {
//     std::cout << "No animation with index " << index << std::endl;
//     return;
//   }
//   Animation& animation = animations[index];

//   bool updated = false;
//   for(auto& channel : animation.channels) {
//     AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
//     if(sampler.inputs.size() > sampler.outputsVec4.size()) {
//       continue;
//     }

//     for(size_t i = 0; i < sampler.inputs.size() - 1; i++) {
//       if((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
//         float u = std::max(0.f, time - sampler.inputs[i]) /
//                   (sampler.inputs[i + 1] - sampler.inputs[i]);
//         if(u <= 1.f) {
//           switch(channel.path) {
//             case AnimationChannel::PathType::TRANSLATION: {
//               glm::vec4 trans =
//                   glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i +
//                   1], u);
//               channel.node->translation = glm::vec3(trans);
//               break;
//             }
//             case AnimationChannel::PathType::SCALE: {
//               glm::vec4 trans =
//                   glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i +
//                   1], u);
//               channel.node->scale = glm::vec3(trans);
//               break;
//             }
//             case AnimationChannel::PathType::ROTATION: {
//               glm::quat q1;
//               q1.x = sampler.outputsVec4[i].x;
//               q1.y = sampler.outputsVec4[i].y;
//               q1.z = sampler.outputsVec4[i].z;
//               q1.w = sampler.outputsVec4[i].w;
//               glm::quat q2;
//               q2.x = sampler.outputsVec4[i + 1].x;
//               q2.y = sampler.outputsVec4[i + 1].y;
//               q2.z = sampler.outputsVec4[i + 1].z;
//               q2.w = sampler.outputsVec4[i + 1].w;
//               channel.node->rotation = glm::normalize(glm::slerp(q1, q2,
//               u)); break;
//             }
//           }
//           updated = true;
//         }
//       }
//     }
//   }
//   if(updated) {
//     for(auto& node : nodes) {
//       node->update();
//     }
//   }
// }

} // namespace rndrx::vulkan