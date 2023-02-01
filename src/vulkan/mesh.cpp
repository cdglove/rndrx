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
#include "rndrx/vulkan/mesh.hpp"

#include "rndrx/vulkan/device.hpp"

namespace rndrx::vulkan {

MeshPrimitive::MeshPrimitive(
    std::uint32_t first_index,
    std::uint32_t index_count,
    std::uint32_t vertex_count,
    Material const& material)
    : first_index_(first_index)
    , index_count_(index_count)
    , vertex_count_(vertex_count)
    , material_(material) {
}

void MeshPrimitive::set_bounding_box(glm::vec3 min, glm::vec3 max) {
  bb_ = {min, max};
}

Mesh::Mesh(Device& device, glm::mat4 matrix) {
  uniform_buffer_ = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(sizeof(UniformBlock))
          .setUsage(vk::BufferUsageFlagBits::eUniformBuffer));

  set_world_matrix(matrix);

  descriptor_info_ =
      vk::DescriptorBufferInfo(*uniform_buffer_.vk(), 0, sizeof(UniformBlock));
};

void Mesh::set_bounding_box(glm::vec3 min, glm::vec3 max) {
  bb_ = {min, max};
}

void Mesh::set_world_matrix(glm::mat4 world) {
  // uniform_block_.world_matrix = world;
  mapped_memory()->world_matrix = world;
}

void Mesh::set_joint_matrix(std::size_t idx, glm::mat4 matrix) {
  // uniform_block_.joints[idx] = matrix;
  mapped_memory()->joints[idx] = matrix;
}

void Mesh::set_num_joints(std::size_t count) {
  // uniform_block_.num_joints = static_cast<float>(count);
  mapped_memory()->num_joints = static_cast<float>(count);
}

void Mesh::add_primitive(MeshPrimitive p) {
  primitives_.push_back(std::move(p));
  if(p.bounding_box().valid() && !bb_.valid()) {
    bb_ = p.bounding_box();
  }
  bb_ = merge(bb_, p.bounding_box());
}

Mesh::UniformBlock* Mesh::mapped_memory() {
  return static_cast<UniformBlock*>(uniform_buffer_.mapped_data());
}

} // namespace rndrx::vulkan