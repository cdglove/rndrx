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
#ifndef RNDRX_VULKAN_GLTF_MODEL_HPP_
#define RNDRX_VULKAN_GLTF_MODEL_HPP_
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
#include "rndrx/vulkan/mesh.hpp"
#include "rndrx/vulkan/material.hpp"
#include "rndrx/vulkan/texture.hpp"

#include "rndrx/vulkan/vma/image.hpp"

namespace rndrx::vulkan {
class Device;
class Texture;
} // namespace rndrx::vulkan

namespace tinygltf {
class Model;
class Node;
} // namespace tinygltf

namespace rndrx::vulkan::gltf {

class Node;
class Skin : noncopyable {
 public:
  std::string name;
  Node const* skeleton_root = nullptr;
  std::vector<glm::mat4> inverse_bind_matrices;
  std::vector<Node const*> joints;
};

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
  std::optional<Skin> skin;
  std::int32_t skin_index = -1;
  glm::vec3 translation{};
  glm::vec3 scale{1.f};
  glm::quat rotation{};
  BoundingBox bvh;
  BoundingBox aabb;
};

struct AnimationChannel {
  enum class PathType { Translation, Rotation, Scale };
  PathType path;
  Node const* node;
  uint32_t samplerIndex;
};

struct AnimationSampler {
  enum class InterpolationType { Linear, Step, CubicSpline };
  InterpolationType interpolation;
  std::vector<float> inputs;
  std::vector<glm::vec4> outputs;
};

struct Animation {
  std::string name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  float start = std::numeric_limits<float>::max();
  float end = std::numeric_limits<float>::min();
};

class Model {
 public:
  Model(Device& device, tinygltf::Model const& source);
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec4 joint0;
    glm::vec4 weight0;
    glm::vec4 colour;
  };

  struct Dimensions {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::min());
  };

  struct LoaderInfo {
    std::vector<std::uint32_t> index_buffer;
    std::vector<Vertex> vertex_buffer;
    std::uint32_t index_position = 0;
    std::uint32_t vertex_position = 0;
  };

  void draw_node(Node* node, vk::CommandBuffer command_buffer);
  void draw(vk::CommandBuffer command_buffer);
  void calculate_bounding_box(Node* node, Node* parent);
  void get_scene_dimensions();
  void update_animation(std::uint32_t index, float time);

  vma::Buffer vertices = nullptr;
  vma::Buffer indices = nullptr;
  std::vector<Node> nodes;
  std::vector<Node*> linear_nodes;
  std::vector<Skin> skins;
  std::vector<Texture> textures;
  std::vector<vk::raii::Sampler> texture_samplers;
  std::vector<Material> materials;
  std::vector<Animation> animations;
  std::vector<std::string> extensions;
  glm::mat4 aabb;

 private:
  void create_texture_samplers(Device& device, tinygltf::Model const& source);
  void create_textures(Device& device, tinygltf::Model const& source);
  void create_materials(tinygltf::Model const& source);
  void create_animations(tinygltf::Model const& source);
  void create_skins(tinygltf::Model const& source);
  void create_nodes(Device& device, tinygltf::Model const& source, LoaderInfo& loader_info);
  void create_node_recursive(
      Device& device,
      tinygltf::Model const& source,
      tinygltf::Node const& node,
      Node* parent,
      std::uint32_t node_index,
      LoaderInfo& loader_info);
  void create_device_buffers(Device& device, LoaderInfo const& loader_info);
};

Model load_model_from_file(Device& device, std::string_view path);

} // namespace rndrx::vulkan::gltf

#endif // RNDRX_VULKAN_GLTF_MODEL_HPP_