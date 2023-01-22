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
#include <vulkan/vulkan_enums.hpp>
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include "../vma/image.hpp"
#include "rndrx/noncopyable.hpp"

// Changing this value also requires updating the skinning shaders.
constexpr std::uint32_t kMaxJoints = 128;

namespace rndrx::vulkan {
class Device;
}

namespace tinygltf {
class Image;
}

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

struct TextureSampler {
  vk::Filter mag_filter;
  vk::Filter min_filter;
  vk::SamplerAddressMode address_mode_u;
  vk::SamplerAddressMode address_mode_v;
  vk::SamplerAddressMode address_mode_w;
};

class Texture : noncopyable {
 public:
  Texture() = default;
  Texture(Device& device, tinygltf::Image const& image_data, Texture const& sampler);
  void update_descriptor();

 private:
  void generate_mip_maps(vk::raii::CommandBuffer& cmd_buf);

  Device* device_ = nullptr;
  vma::Image image_ = nullptr;
  vk::raii::ImageView image_view_ = nullptr;
  vk::raii::ImageView view_ = nullptr;
  vk::raii::Sampler sampler_ = nullptr;
  vk::ImageLayout image_layout_;
  vk::Format format_;
  vk::DescriptorImageInfo descriptor_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint32_t mip_count_ = 0;
  std::uint32_t layer_count_ = 0;
};

struct Material {
  enum class AlphaMode { Opaque, Mask, Blend };
  AlphaMode alpha_mode = AlphaMode::Opaque;
  float alpha_cutoff = 1.f;
  float metallic_factor = 1.f;
  float roughness_factor = 1.f;
  glm::vec4 base_color_factor = glm::vec4(1.f);
  glm::vec4 emissive_factor = glm::vec4(1.f);
  Texture const* base_color_texture = nullptr;
  Texture const* metallic_roughness_texture = nullptr;
  Texture const* normal_texture = nullptr;
  Texture const* occlusion_texture = nullptr;
  Texture const* emissive_texture = nullptr;
  bool double_sided = false;
  struct TexCoordSets {
    uint8_t base_color = 0;
    uint8_t metallic_roughness = 0;
    uint8_t specular_glossiness = 0;
    uint8_t normal = 0;
    uint8_t occlusion = 0;
    uint8_t emissive = 0;
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

struct Primitive {
  Primitive(
      std::uint32_t first_index,
      std::uint32_t index_count,
      std::uint32_t vertex_count,
      Material& material);

  void set_bounding_box(glm::vec3 min, glm::vec3 max);

  std::uint32_t first_index;
  std::uint32_t index_count;
  std::uint32_t vertex_count;
  Material& material;
  bool has_indices;
  BoundingBox bb;
};

struct Mesh {
  Mesh(Device& device, glm::mat4 matrix);
  ~Mesh();
  void set_bounding_box(glm::vec3 min, glm::vec3 max);

  Device* device;
  std::vector<Primitive*> primitives;
  BoundingBox bb;
  BoundingBox aabb;

  struct UniformBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDescriptorBufferInfo descriptor;
    VkDescriptorSet descriptor_set;
    void* mapped;
  } uniform_buffer;

  struct UniformBlock {
    glm::mat4 matrix;
    // cglover-todo(2023-01-22): Optimise this out. Every mush is using way more memory than
    // necessary.
    std::array<glm::mat4, kMaxJoints> joints;
    float joint_count{0};
  } uniform_block;
};

} // namespace rndrx::vulkan::gltf

#endif // RNDRX_VULKAN_GLTF_MODEL_HPP_