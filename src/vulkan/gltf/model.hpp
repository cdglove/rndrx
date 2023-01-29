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
#include "rndrx/noncopyable.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"
#include "rndrx/vulkan/vma/image.hpp"
#include "rndrx/vulkan/texture.hpp"

// Changing this value also requires updating the skinning shaders.
constexpr std::size_t kMaxNumJoints = 128;

namespace rndrx::vulkan {
class Device;
class Texture;
}

namespace tinygltf {
class Image;
class Model;
class Node;
} // namespace tinygltf

namespace rndrx::vulkan::gltf {

struct BoundingBox {
  BoundingBox() = default;
  BoundingBox(glm::vec3 min, glm::vec3 max)
      : min(min)
      , max(max) {
  }

  BoundingBox get_aabb(glm::mat4 const& aligned_to) const;

  glm::vec3 min;
  glm::vec3 max;
  bool valid = false;
};

struct Material {
  enum class AlphaMode { Opaque, Mask, Blend };
  AlphaMode alpha_mode = AlphaMode::Opaque;
  float alpha_cutoff = 1.f;
  float metallic_factor = 1.f;
  float roughness_factor = 1.f;
  glm::vec4 base_colour_factor = glm::vec4(1.f);
  glm::vec4 emissive_factor = glm::vec4(1.f);
  Texture const* base_colour_texture = nullptr;
  Texture const* metallic_roughness_texture = nullptr;
  Texture const* normal_texture = nullptr;
  Texture const* occlusion_texture = nullptr;
  Texture const* emissive_texture = nullptr;
  bool double_sided = false;
  struct TexCoordSets {
    std::uint8_t base_colour = 0;
    std::uint8_t metallic_roughness = 0;
    std::uint8_t specular_glossiness = 0;
    std::uint8_t normal = 0;
    std::uint8_t occlusion = 0;
    std::uint8_t emissive = 0;
  } tex_coord_sets;
  struct Extension {
    Texture const* specular_glossiness_texture = nullptr;
    Texture const* diffuse_texture = nullptr;
    glm::vec4 diffuse_factor = glm::vec4(1.f);
    glm::vec3 specular_factor = glm::vec3(0.f);
  } extension;
  struct PbrWorkflows {
    bool metallic_roughness = true;
    bool specular_glossiness = false;
  } pbr_workflows;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

class Primitive : noncopyable {
 public:
  Primitive(
      std::uint32_t first_index,
      std::uint32_t index_count,
      std::uint32_t vertex_count,
      Material const& material);

  void set_bounding_box(glm::vec3 min, glm::vec3 max);

  std::uint32_t first_index() const {
    return first_index_;
  }
  std::uint32_t index_count() const {
    return index_count_;
  }
  std::uint32_t vertex_count() const {
    return vertex_count_;
  }
  Material const& material() const {
    return material_;
  }
  bool has_indices() const {
    return has_indices_;
  }
  BoundingBox bounding_box() const {
    return bb_;
  }

 private:
  std::uint32_t first_index_;
  std::uint32_t index_count_;
  std::uint32_t vertex_count_;
  Material const& material_;
  bool has_indices_;
  BoundingBox bb_;
};

struct Mesh : noncopyable {
  Mesh(Device& device, glm::mat4 matrix);
  RNDRX_DEFAULT_MOVABLE(Mesh);

  void set_bounding_box(glm::vec3 min, glm::vec3 max);
  void set_world_matrix(glm::mat4 world);
  void set_joint_matrix(std::size_t idx, glm::mat4 matrix);
  void set_num_joints(std::size_t count);
  void add_primitive(Primitive primitive);

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
  vma::Buffer buffer_ = nullptr;
  vk::DescriptorBufferInfo descriptor_info_;
  // UniformBlock uniform_block_;
  std::vector<Primitive> primitives_;
  BoundingBox bb_;
  BoundingBox aabb_;
  // vk::DescriptorSet descriptor_set_;
};

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
  std::vector<glm::vec4> outputsVec4;
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
  std::vector<TextureSampler> texture_samplers;
  std::vector<Material> materials;
  std::vector<Animation> animations;
  std::vector<std::string> extensions;
  glm::mat4 aabb;

 private:
  void create_texture_samplers(tinygltf::Model const& source);
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