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
#ifndef RNDRX_VULKAN_MATERIAL_HPP_
#define RNDRX_VULKAN_MATERIAL_HPP_
#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {
class Texture;

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

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_MATERIAL_HPP_