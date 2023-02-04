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
#ifndef RNDRX_VULKAN_MODEL_HPP_
#define RNDRX_VULKAN_MODEL_HPP_
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <optional>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/bounding_box.hpp"
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/animation.hpp"
#include "rndrx/vulkan/material.hpp"
#include "rndrx/vulkan/mesh.hpp"
#include "rndrx/vulkan/texture.hpp"
#include "rndrx/vulkan/vma/image.hpp"

namespace tinygltf {
class Model;
} // namespace tinygltf

namespace rndrx::vulkan {

class Device;
class Texture;

class Node : noncopyable {
 public:
  Node(Node const* parent);
  RNDRX_DEFAULT_MOVABLE(Node);

  glm::mat4 local_matrix() const;
  glm::mat4 resolve_transform_hierarchy() const;
  void update();

  Node const* parent;
  std::uint32_t index;
  std::vector<Node> children;
  glm::mat4 matrix;
  std::string name;
  std::optional<Mesh> mesh;
  std::optional<Skeleton> skeleton;
  std::int32_t skeleton_index = -1;
  glm::vec3 translation{};
  glm::vec3 scale{1.f};
  glm::quat rotation{};
  BoundingBox bvh;
  BoundingBox aabb;
};

class Model : noncopyable {
 public:
  // Defined in rndrx/vulkan/gltf/model.cpp
  Model(Device& device, tinygltf::Model const& source);

  RNDRX_DEFAULT_MOVABLE(Model);

  struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec4 joint0;
    glm::vec4 weight0;
    glm::vec4 colour;
  };

  void draw(vk::CommandBuffer command_buffer);
  void calculate_bounding_box(Node const* node, Node const* parent);
  void get_scene_dimensions();
  void update_animation(std::uint32_t index, float time);

 private:
  void create_device_buffers(
      Device& device,
      std::vector<std::uint32_t> const& index_buffer,
      std::vector<Model::Vertex> const& vertex_buffer);
  
  void draw_node(Node const* node, vk::CommandBuffer command_buffer);

  vma::Buffer vertices_ = nullptr;
  vma::Buffer indices_ = nullptr;
  std::vector<Node> nodes_;
  std::vector<Skeleton> skeletons_;
  std::vector<Texture> textures_;
  std::vector<vk::raii::Sampler> texture_samplers_;
  std::vector<Material> materials_;
  std::vector<Animation> animations_;
  glm::mat4 aabb_;
};

Model load_model_from_file(Device& device, std::string_view path);

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_MODEL_HPP_