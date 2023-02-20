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
#ifndef RNDRX_VULKAN_MESH_HPP_
#define RNDRX_VULKAN_MESH_HPP_
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "rndrx/bounding_box.hpp"
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"

namespace rndrx { namespace vulkan {
class Device;
struct Material;
}} // namespace rndrx::vulkan

namespace rndrx::vulkan {

// Changing this value also requires updating the skinning shaders.
constexpr std::size_t kMaxNumJoints = 128;

class MeshPrimitive : noncopyable {
 public:
  MeshPrimitive(
      std::uint32_t first_index,
      std::uint32_t index_count,
      Material const& material);

  void set_bounding_box(glm::vec3 min, glm::vec3 max);
  void draw(vk::CommandBuffer command_buffer) const;

  std::uint32_t first_index() const {
    return first_index_;
  }

  std::uint32_t index_count() const {
    return index_count_;
  }

  Material const& material() const {
    return material_;
  }

  bool has_indices() const {
    return index_count_ > 0;
  }

  BoundingBox bounding_box() const {
    return bb_;
  }

 private:
  std::uint32_t first_index_;
  std::uint32_t index_count_;
  Material const& material_;
  BoundingBox bb_;
};

class Mesh : noncopyable {
 public:
  Mesh(Device& device, glm::mat4 matrix);
  RNDRX_DEFAULT_MOVABLE(Mesh);

  void draw(vk::CommandBuffer command_buffer) const;
  void set_bounding_box(glm::vec3 min, glm::vec3 max);
  void set_world_matrix(glm::mat4 world);
  void set_joint_matrix(std::size_t idx, glm::mat4 matrix);
  void set_num_joints(std::size_t count);
  void add_primitive(MeshPrimitive primitive);

  struct UniformBlock {
    glm::mat4 world_matrix;
    // cglover-todo(2023-01-22): Optimise this out. Every mesh is using way more
    // memory than necessary.
    std::array<glm::mat4, kMaxNumJoints> joints;
    // Float because it aligns with the shader?
    float num_joints = 0;
  };

 private:
  UniformBlock* mapped_memory();
  vma::Buffer uniform_buffer_ = nullptr;
  vk::DescriptorBufferInfo descriptor_info_;
  std::vector<MeshPrimitive> primitives_;
  BoundingBox bb_;
  BoundingBox aabb_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_MESH_HPP_