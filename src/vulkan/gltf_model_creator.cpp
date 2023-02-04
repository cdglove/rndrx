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
#include "rndrx/vulkan/gltf_model_creator.hpp"

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
#include "rndrx/vulkan/model.hpp"
#include "rndrx/vulkan/texture.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"
#include "tiny_gltf.h"

namespace rndrx::vulkan {
namespace {

constexpr int kTinyGltfNotSpecified = -1;
vk::SamplerAddressMode to_vk_sampler_address(std::int32_t gltf_wrap_mode) {
  switch(gltf_wrap_mode) {
    case -1:
    case TINYGLTF_TEXTURE_WRAP_REPEAT:
      return vk::SamplerAddressMode::eRepeat;
    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
      return vk::SamplerAddressMode::eClampToEdge;
    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
      return vk::SamplerAddressMode::eMirroredRepeat;
  }

  LOG(Error) << "Unknown wrap mode for to_vk_sampler_address: " << gltf_wrap_mode;
  return vk::SamplerAddressMode::eRepeat;
}

vk::Filter to_vk_filter_mode(std::int32_t gltf_filter_mode) {
  switch(gltf_filter_mode) {
    case -1:
    case TINYGLTF_TEXTURE_FILTER_NEAREST:
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
      return vk::Filter::eNearest;
    case TINYGLTF_TEXTURE_FILTER_LINEAR:
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
      return vk::Filter::eLinear;
  }

  LOG(Error) << "Unknown filter mode for to_vk_filter_mode: " << gltf_filter_mode;
  return vk::Filter::eLinear;
}

Node const* find_node_recursive(Node const& node, uint32_t index) {
  if(node.index == index) {
    return &node;
  }

  Node* ret = nullptr;
  for(auto const& child : node.children) {
    if(auto found = find_node_recursive(child, index)) {
      return found;
    }
  }
  return nullptr;
}

Node const* node_from_index(std::span<const Node> nodes, std::uint32_t index) {
  for(auto const& node : nodes) {
    if(auto found = find_node_recursive(&node, index)) {
      // If this is true then we don't need this loop
      // we can just fetch directly from the array.
      RNDRX_ASSERT(std::distance(nodes.data(), found) == index);
      return found;
    }
  }
  return nullptr;
}

struct NodeProperties {
  std::size_t vertex_count = 0;
  std::size_t index_count = 0;
  std::size_t node_count = 0;
  friend NodeProperties operator+(NodeProperties const& a, NodeProperties const& b) {
    return {
        a.vertex_count + b.vertex_count,
        a.index_count + b.index_count,
        a.node_count + b.node_count};
  }
};

NodeProperties get_node_properties_recursive(
    tinygltf::Node const& node,
    tinygltf::Model const& model) {
  NodeProperties ret;
  ret.node_count = 1;

  if(node.mesh > kTinyGltfNotSpecified) {
    tinygltf::Mesh const& mesh = model.meshes[node.mesh];
    for(auto&& primitive : mesh.primitives) {
      auto position_idx = primitive.attributes.find("POSITION")->second;
      ret.vertex_count += model.accessors[position_idx].count;
      if(primitive.indices > -1) {
        ret.index_count += model.accessors[primitive.indices].count;
      }
    }
  }

  for(auto&& child : node.children) {
    ret = ret + get_node_properties_recursive(model.nodes[child], model);
  }

  return ret;
}
} // namespace

std::vector<vk::raii::Sampler> GltfModelCreator::create_texture_samplers(Device& device) {
  std::vector<vk::raii::Sampler> texture_samplers;
  for(auto&& gltf_sampler : source_.samplers) {
    auto min_filter = to_vk_filter_mode(gltf_sampler.minFilter);
    auto mag_filter = to_vk_filter_mode(gltf_sampler.magFilter);
    auto address_mode_u = to_vk_sampler_address(gltf_sampler.wrapS);
    auto address_mode_v = to_vk_sampler_address(gltf_sampler.wrapT);
    auto address_mode_w = address_mode_v;

    vk::raii::Sampler sampler = device.vk().createSampler(
        vk::SamplerCreateInfo()
            .setMagFilter(mag_filter)
            .setMinFilter(min_filter)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setAddressModeU(address_mode_u)
            .setAddressModeV(address_mode_v)
            .setAddressModeW(address_mode_w)
            .setCompareOp(vk::CompareOp::eNever)
            .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
            .setMinLod(0.f)
            .setMaxLod(VK_LOD_CLAMP_NONE)
            .setMaxAnisotropy(8.0f)
            .setAnisotropyEnable(VK_TRUE));

    texture_samplers.push_back(std::move(sampler));
  }

  // Create one more for non-specificed samplers.
  auto min_filter = to_vk_filter_mode(kTinyGltfNotSpecified);
  auto mag_filter = to_vk_filter_mode(kTinyGltfNotSpecified);
  auto address_mode_u = to_vk_sampler_address(kTinyGltfNotSpecified);
  auto address_mode_v = to_vk_sampler_address(kTinyGltfNotSpecified);
  auto address_mode_w = address_mode_v;

  vk::raii::Sampler sampler = device.vk().createSampler(
      vk::SamplerCreateInfo()
          .setMagFilter(mag_filter)
          .setMinFilter(min_filter)
          .setMipmapMode(vk::SamplerMipmapMode::eLinear)
          .setAddressModeU(address_mode_u)
          .setAddressModeV(address_mode_v)
          .setAddressModeW(address_mode_w)
          .setCompareOp(vk::CompareOp::eNever)
          .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
          .setMinLod(0.f)
          .setMaxLod(VK_LOD_CLAMP_NONE)
          .setMaxAnisotropy(8.0f)
          .setAnisotropyEnable(VK_TRUE));

  texture_samplers.push_back(std::move(sampler));
  return texture_samplers;
}

std::vector<Texture> GltfModelCreator::create_textures(
    Device& device,
    std::vector<vk::raii::Sampler> const& texture_samplers) {
  std::vector<Texture> textures;
  for(auto&& gltf_texture : source_.textures) {
    tinygltf::Image const& image = source_.images[gltf_texture.source];
    vk::Sampler sampler = nullptr;
    if(gltf_texture.sampler == kTinyGltfNotSpecified) {
      sampler = *texture_samplers.back();
    }
    else {
      sampler = *texture_samplers[gltf_texture.sampler];
    }

    TextureCreateInfo create_info;
    create_info.width = image.width;
    create_info.height = image.height;
    create_info.component_count = image.component;
    create_info.sampler = sampler;
    create_info.image_data = image.image;
    textures.emplace_back(device, create_info);
  }

  return textures;
}

std::vector<Material> GltfModelCreator::create_materials(
    std::vector<Texture> const& textures) {
  std::vector<Material> materials;
  for(auto&& gltf_material : source_.materials) {
    auto get_value =
        [&gltf_material](std::string_view name) -> tinygltf::Parameter const* {
      auto iter = gltf_material.values.find(name.data());
      if(iter == gltf_material.values.end()) {
        return nullptr;
      }

      return std::addressof(iter->second);
    };

    auto get_additional_value =
        [&gltf_material](std::string_view name) -> tinygltf::Parameter const* {
      auto iter = gltf_material.additionalValues.find(name.data());
      if(iter == gltf_material.additionalValues.end()) {
        return nullptr;
      }

      return std::addressof(iter->second);
    };

    auto get_extension =
        [&gltf_material](std::string_view name) -> tinygltf::Value const* {
      auto iter = gltf_material.extensions.find(name.data());
      if(iter == gltf_material.extensions.end()) {
        return nullptr;
      }

      return std::addressof(iter->second);
    };

    Material& material = materials.emplace_back();
    material.double_sided = gltf_material.doubleSided;

    if(auto properties = get_value("baseColorTexture")) {
      material.base_colour_texture = &textures[properties->TextureIndex()];
      material.uv_sets.base_colour = properties->TextureTexCoord();
    }

    if(auto properties = get_value("baseColorFactor")) {
      material.base_colour_factor = glm::make_vec4(
          properties->ColorFactor().data());
    }

    if(auto properties = get_value("metallicRoughnessTexture")) {
      material.metallic_roughness_texture = &textures[properties->TextureIndex()];
      material.uv_sets.metallic_roughness = properties->TextureTexCoord();
    }

    if(auto properties = get_value("metallicFactor")) {
      material.metallic_factor = properties->Factor();
    }

    if(auto properties = get_value("roughnessFactor")) {
      material.roughness_factor = properties->Factor();
    }

    if(auto properties = get_value("normalTexture")) {
      material.normal_texture = &textures[properties->TextureIndex()];
      material.uv_sets.normal = properties->TextureTexCoord();
    }

    if(auto properties = get_value("emissiveTexture")) {
      material.emissive_texture = &textures[properties->TextureIndex()];
      material.uv_sets.emissive = properties->TextureTexCoord();
    }

    if(auto properties = get_value("occlusionTexture")) {
      material.occlusion_texture = &textures[properties->TextureIndex()];
      material.uv_sets.occlusion = properties->TextureTexCoord();
    }

    if(auto property = get_additional_value("alphaMode")) {
      if(property->string_value == "BLEND") {
        material.alpha_mode = Material::AlphaMode::Blend;
      }
      if(property->string_value == "MASK") {
        material.alpha_cutoff = 0.5f;
        material.alpha_mode = Material::AlphaMode::Mask;
      }
    }

    if(auto property = get_additional_value("alphaCutoff")) {
      material.alpha_cutoff = property->Factor();
    }

    if(auto property = get_additional_value("emissiveFactor")) {
      material.emissive_factor = glm::vec4(
          glm::make_vec3(property->ColorFactor().data()),
          1.0);
    }

    if(auto ext = get_extension("KHR_materials_pbrSpecularGlossiness")) {
      auto&& spec_gloss_value = ext->Get("specularGlossinessTexture");
      if(spec_gloss_value != tinygltf::Value()) {
        auto index = spec_gloss_value.Get("index");
        material.extension.specular_glossiness_texture = &textures[index.Get<int>()];
        auto text_coord_set = spec_gloss_value.Get("texCoord");
        material.uv_sets.specular_glossiness = text_coord_set.Get<int>();
        material.pbr_workflows.specular_glossiness = true;
      }

      auto&& diffuse_texture = ext->Get("diffuseTexture");
      if(diffuse_texture != tinygltf::Value()) {
        auto index = diffuse_texture.Get("index");
        material.extension.diffuse_texture = &textures[index.Get<int>()];
      }

      auto&& diffuse_factor_value = ext->Get("diffuseFactor");
      if(diffuse_factor_value != tinygltf::Value()) {
        for(uint32_t i = 0; i < diffuse_factor_value.ArrayLen(); i++) {
          auto val = diffuse_factor_value.Get(i);
          if(val.IsReal()) {
            material.extension.diffuse_factor[i] = static_cast<float>(
                val.Get<double>());
          }
          else {
            RNDRX_ASSERT(val.IsInt());
            material.extension.diffuse_factor[i] = static_cast<float>(
                val.Get<int>());
          }
        }

        auto&& specular_factor_value = ext->Get("specularFactor");
        if(specular_factor_value != tinygltf::Value()) {
          for(uint32_t i = 0; i < specular_factor_value.ArrayLen(); i++) {
            auto val = specular_factor_value.Get(i);
            if(val.IsReal()) {
              material.extension.specular_factor[i] = static_cast<float>(
                  val.Get<double>());
            }
            else {
              RNDRX_ASSERT(val.IsInt());
              material.extension.specular_factor[i] = static_cast<float>(
                  val.Get<int>());
            }
          }
        }
      }
    }
  }

  return materials;
}

std::vector<Animation> GltfModelCreator::create_animations(
    std::vector<Node> const& nodes) {
  std::vector<Animation> animations;
  for(auto&& anim : source_.animations) {
    Animation animation;
    animation.name = anim.name;
    if(anim.name.empty()) {
      animation.name = std::to_string(animations.size());
    }

    for(auto&& samp : anim.samplers) {
      AnimationSampler& sampler = animation.samplers.emplace_back();

      if(samp.interpolation == "LINEAR") {
        sampler.interpolation = AnimationSampler::InterpolationType::Linear;
      }
      if(samp.interpolation == "STEP") {
        sampler.interpolation = AnimationSampler::InterpolationType::Step;
      }
      if(samp.interpolation == "CUBICSPLINE") {
        sampler.interpolation = AnimationSampler::InterpolationType::CubicSpline;
      }

      {
        tinygltf::Accessor const& accessor = source_.accessors[samp.input];
        tinygltf::BufferView const& buffer_view =
            source_.bufferViews[accessor.bufferView];
        tinygltf::Buffer const& buffer = source_.buffers[buffer_view.buffer];

        RNDRX_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        void const* data_ptr = &buffer.data[accessor.byteOffset + buffer_view.byteOffset];
        float const* buf = static_cast<const float*>(data_ptr);
        sampler.inputs.assign(buf, buf + accessor.count);
        auto start_end = std::ranges::minmax_element(sampler.inputs);
        animation.start = *start_end.min;
        animation.end = *start_end.max;
      }

      {
        tinygltf::Accessor const& accessor = source_.accessors[samp.output];
        tinygltf::BufferView const& buffer_view =
            source_.bufferViews[accessor.bufferView];
        tinygltf::Buffer const& buffer = source_.buffers[buffer_view.buffer];

        RNDRX_ASSERT(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        void const* data_ptr = &buffer.data[accessor.byteOffset + buffer_view.byteOffset];

        switch(accessor.type) {
          case TINYGLTF_TYPE_VEC3: {
            glm::vec3 const* buf = static_cast<glm::vec3 const*>(data_ptr);
            sampler.inputs.resize(accessor.count);
            std::transform(
                buf,
                buf + accessor.count,
                sampler.outputs.begin(),
                [](glm::vec3 const& v) { return glm::vec4(v, 0.f); });
            break;
          }
          case TINYGLTF_TYPE_VEC4: {
            glm::vec4 const* buf = static_cast<glm::vec4 const*>(data_ptr);
            sampler.outputs.assign(buf, buf + accessor.count);
            break;
          }
          default: {
            LOG(Warn) << "unknown type" << std::endl;
            break;
          }
        }
      }
    }

    for(auto& src_channel : anim.channels) {
      AnimationChannel channel{};

      if(src_channel.target_path == "rotation") {
        channel.path = AnimationChannel::PathType::Rotation;
      }
      if(src_channel.target_path == "translation") {
        channel.path = AnimationChannel::PathType::Translation;
      }
      if(src_channel.target_path == "scale") {
        channel.path = AnimationChannel::PathType::Scale;
      }
      if(src_channel.target_path == "weights") {
        LOG(Info) << "weights not yet supported, skipping channel" << std::endl;
        continue;
      }
      channel.samplerIndex = src_channel.sampler;
      channel.node = node_from_index(nodes, src_channel.target_node);
      if(!channel.node) {
        continue;
      }

      animation.channels.push_back(channel);
    }

    animations.push_back(animation);
  }

  return animations;
}

void GltfModelCreator::create_nodes_recursive(
    Device& device,
    tinygltf::Node const& source_node,
    Node* parent,
    std::uint32_t node_index,
    std::vector<Material> const& materials,
    std::vector<Node>& nodes) {
  nodes.emplace_back(parent);
  Node& new_node = nodes.back();
  new_node.index = node_index;
  new_node.parent = parent;
  new_node.name = source_node.name;
  new_node.skeleton_index = source_node.skin;
  new_node.matrix = glm::mat4(1.f);

  glm::vec3 translation = glm::vec3(0.f);
  if(source_node.translation.size() == 3) {
    translation = glm::make_vec3(source_node.translation.data());
    new_node.translation = translation;
  }

  glm::mat4 rotation = glm::mat4(1.f);
  if(source_node.rotation.size() == 4) {
    glm::quat q = glm::make_quat(source_node.rotation.data());
    new_node.rotation = glm::mat4(q);
  }

  glm::vec3 scale = glm::vec3(1.f);
  if(source_node.scale.size() == 3) {
    scale = glm::make_vec3(source_node.scale.data());
    new_node.scale = scale;
  }

  if(source_node.matrix.size() == 16) {
    new_node.matrix = glm::make_mat4x4(source_node.matrix.data());
  };

  for(auto&& child_idx : source_node.children) {
    create_nodes_recursive(
        device,
        source_.nodes[child_idx],
        &new_node,
        child_idx,
        materials,
        nodes);
  }

  if(source_node.mesh > kTinyGltfNotSpecified) {
    tinygltf::Mesh const& mesh = source_.meshes[source_node.mesh];
    new_node.mesh = Mesh(device, new_node.matrix);
    for(auto&& primitive : mesh.primitives) {
      std::uint32_t vertex_start = vertex_position_;
      std::uint32_t index_start = index_position_;
      std::uint32_t index_count = 0;
      std::uint32_t vertex_count = 0;
      glm::vec3 pos_min{};
      glm::vec3 pos_max{};

      float const* buffer_positions = nullptr;
      float const* buffer_normals = nullptr;
      float const* buffer_uv0 = nullptr;
      float const* buffer_uv1 = nullptr;
      float const* buffer_colour = nullptr;
      void const* buffer_joints = nullptr;
      float const* buffer_weights = nullptr;

      std::uint32_t position_stride = 0;
      std::uint32_t normal_stride = 0;
      std::uint32_t uv0_stride = 0;
      std::uint32_t uv1_stride = 0;
      std::uint32_t colour_stride = 0;
      std::uint32_t joints_stride = 0;
      std::uint32_t weights_stride = 0;

      int joint_component_type = 0;

      // Position attribute is required
      RNDRX_ASSERT(
          primitive.attributes.find("POSITION") != primitive.attributes.end());

      struct BufferExtractor {
        BufferExtractor(tinygltf::Model const& source, tinygltf::Accessor const& accessor)
            : source_(source)
            , accessor_(accessor) {
        }

        void const* void_buffer() const {
          return &(source_.buffers[view().buffer]
                       .data[accessor_.byteOffset + accessor_.byteOffset]);
        }

        float const* float_buffer() const {
          return reinterpret_cast<float const*>(void_buffer());
        }

        std::uint32_t const* uint32_buffer() const {
          return reinterpret_cast<std::uint32_t const*>(void_buffer());
        }

        std::uint16_t const* uint16_buffer() const {
          return reinterpret_cast<std::uint16_t const*>(void_buffer());
        }

        std::uint8_t const* uint8_buffer() const {
          return reinterpret_cast<std::uint8_t const*>(void_buffer());
        }

        std::uint32_t stride_or(std::uint32_t alt) const {
          auto stride = accessor_.ByteStride(view());
          if(stride > 0) {
            return accessor_.ByteStride(view()) / sizeof(float);
          }
          return alt;
        };

        tinygltf::BufferView const& view() const {
          return source_.bufferViews[accessor_.bufferView];
        }

        tinygltf::Model const& source_;
        tinygltf::Accessor const& accessor_;
      };

      auto get_accessor = [this, &primitive](
                              std::string_view name) -> tinygltf::Accessor const* {
        auto iter = primitive.attributes.find(name.data());
        if(iter == primitive.attributes.end()) {
          return nullptr;
        }

        return &source_.accessors[iter->second];
      };

      auto& position_accessor = *get_accessor("POSITION");
      auto& position_view = source_.bufferViews[position_accessor.bufferView];
      BufferExtractor extractor(source_, position_accessor);
      buffer_positions = extractor.float_buffer();
      position_stride = extractor.stride_or(
          tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3));
      pos_min = glm::vec3(
          position_accessor.minValues[0],
          position_accessor.minValues[1],
          position_accessor.minValues[2]);
      pos_max = glm::vec3(
          position_accessor.maxValues[0],
          position_accessor.maxValues[1],
          position_accessor.maxValues[2]);
      vertex_count = static_cast<std::uint32_t>(position_accessor.count);

      if(auto accessor = get_accessor("NORMAL")) {
        BufferExtractor extractor(source_, *accessor);
        buffer_normals = extractor.float_buffer();
        normal_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3));
      }

      if(auto accessor = get_accessor("TEXCOORD_0")) {
        BufferExtractor extractor(source_, *accessor);
        buffer_uv0 = extractor.float_buffer();
        uv0_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2));
      }

      if(auto accessor = get_accessor("TEXCOORD_1")) {
        BufferExtractor extractor(source_, *accessor);
        buffer_uv1 = extractor.float_buffer();
        uv1_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2));
      }

      if(auto accessor = get_accessor("COLOR_0")) {
        BufferExtractor extractor(source_, *accessor);
        buffer_colour = extractor.float_buffer();
        colour_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3));
      }

      if(auto accessor = get_accessor("JOINTS_0")) {
        BufferExtractor extractor(source_, *accessor);
        buffer_joints = extractor.float_buffer();
        joints_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4));
      }

      if(auto accessor = get_accessor("WEIGHTS_0")) {
        BufferExtractor extractor(source_, *accessor);
        buffer_weights = extractor.float_buffer();
        weights_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4));
      }

      bool is_skinned = (buffer_joints && buffer_weights);

      for(std::size_t v = 0; v < position_accessor.count; v++) {
        Model::Vertex& vert = vertex_buffer_[vertex_position_];
        auto pos_3d = glm::make_vec3(&buffer_positions[v * position_stride]);
        vert.position = glm::vec4(pos_3d, 1.0f);
        if(buffer_normals) {
          vert.normal = glm::make_vec3(&buffer_normals[v * normal_stride]);
          vert.normal = glm::normalize(vert.normal);
        }

        if(buffer_uv0) {
          vert.uv0 = glm::make_vec2(&buffer_uv0[v * uv0_stride]);
        }

        if(buffer_uv1) {
          vert.uv1 = glm::make_vec2(&buffer_uv1[v * uv1_stride]);
        }

        if(buffer_colour) {
          vert.colour = glm::make_vec4(&buffer_colour[v * colour_stride]);
        }

        if(is_skinned) {
          switch(joint_component_type) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
              const uint16_t* buf = static_cast<const uint16_t*>(buffer_joints);
              vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * joints_stride]));
              break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
              const uint8_t* buf = static_cast<const uint8_t*>(buffer_joints);
              vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * joints_stride]));
              break;
            }
            default:
              // Not supported by spec
              std::cerr << "Joint component type " << joint_component_type
                        << " not supported!" << std::endl;
              break;
          }
        }
        else {
          vert.joint0 = glm::vec4(0.0f);
        }
        vert.weight0 = is_skinned
                           ? glm::make_vec4(&buffer_weights[v * weights_stride])
                           : glm::vec4(0.0f);
        // Fix for all zero weights
        if(glm::length(vert.weight0) == 0.0f) {
          vert.weight0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
        vertex_position_++;
      }

      bool has_indices = primitive.indices > -1;
      if(has_indices) {
        const tinygltf::Accessor& accessor = source_.accessors[primitive.indices];
        BufferExtractor extractor(source_, accessor);
        index_count = static_cast<std::uint32_t>(accessor.count);

        switch(accessor.componentType) {
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
            std::uint32_t const* buf = extractor.uint32_buffer();
            for(size_t index = 0; index < accessor.count; index++) {
              index_buffer_[index_position_] = buf[index] + vertex_start;
              ++index_position_;
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
            const uint16_t* buf = extractor.uint16_buffer();
            for(size_t index = 0; index < accessor.count; index++) {
              index_buffer_[index_position_] = buf[index] + vertex_start;
              ++index_position_;
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
            const uint8_t* buf = extractor.uint8_buffer();
            for(size_t index = 0; index < accessor.count; index++) {
              index_buffer_[index_position_] = buf[index] + vertex_start;
              ++index_position_;
            }
            break;
          }
          default:
            LOG(Error) << "Index component type " << accessor.componentType
                       << " not supported!";
            return;
        }
      }

      new_node.mesh->add_primitive(
          {index_start,
           index_count,
           primitive.material > kTinyGltfNotSpecified ? materials[primitive.material]
                                                      : Material()});
    }
  }

  if(parent) {
    parent->children.push_back(&new_node);
  }
}

std::vector<Node> GltfModelCreator::create_nodes(
    Device& device,
    std::vector<Material> const& materials) {
  std::vector<Node> ret_nodes;
  tinygltf::Scene const& scene =
      source_.scenes[source_.defaultScene > kTinyGltfNotSpecified ? source_.defaultScene : 0];

  NodeProperties scene_properties = std::accumulate(
      scene.nodes.begin(),
      scene.nodes.end(),
      NodeProperties(),
      [this](NodeProperties props, int node_idx) {
        return props +
               get_node_properties_recursive(source_.nodes[node_idx], source_);
      });

  vertex_buffer_.resize(scene_properties.vertex_count);
  index_buffer_.resize(scene_properties.index_count);

  RNDRX_ASSERT(scene_properties.node_count == source_.nodes.size());
  ret_nodes.reserve(scene_properties.node_count);

  for(auto&& node_idx : scene.nodes) {
    tinygltf::Node const& node = source_.nodes[node_idx];
    create_nodes_recursive(device, node, nullptr, node_idx, materials, ret_nodes);
  }

  return ret_nodes;
}

std::vector<Skeleton> GltfModelCreator::create_skeletons(std::vector<Node> const& nodes) {
  std::vector<Skeleton> skeletons;
  for(auto&& skin : source_.skins) {
    skeletons.emplace_back();
    auto& new_skeleton = skeletons.back();
    new_skeleton.name = skin.name;

    if(skin.skeleton > kTinyGltfNotSpecified) {
      new_skeleton.skeleton_root = node_from_index(nodes, skin.skeleton);
    }

    for(int joint_index : skin.joints) {
      if(auto joint_node = node_from_index(nodes, joint_index)) {
        new_skeleton.joints.push_back(joint_node);
      }
    }

    if(skin.inverseBindMatrices > -1) {
      auto const& accessor = source_.accessors[skin.inverseBindMatrices];
      auto const& bufferView = source_.bufferViews[accessor.bufferView];
      const tinygltf::Buffer& buffer = source_.buffers[bufferView.buffer];
      new_skeleton.inverse_bind_matrices.resize(accessor.count);
      std::memcpy(
          new_skeleton.inverse_bind_matrices.data(),
          &buffer.data[accessor.byteOffset + bufferView.byteOffset],
          accessor.count * sizeof(glm::mat4));
    }
  }

  return skeletons;
}

namespace gltf {
tinygltf::Model load_model_from_file(std::string_view path) {
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  std::filesystem::path fs_path(path);
  bool file_loaded = false;
  if(fs_path.extension() == "glb") {
    file_loaded =
        loader.LoadBinaryFromFile(&model, &err, &warn, fs_path.generic_string());
  }
  else {
    file_loaded =
        loader.LoadASCIIFromFile(&model, &err, &warn, fs_path.generic_string());
  }

  if(!warn.empty()) {
    LOG(Warn) << warn.c_str();
  }

  if(!err.empty()) {
    throw_runtime_error(err.c_str());
  }

  if(!file_loaded) {
    throw_runtime_error("Failed to parse glTF");
  }

  return model;
}
} // namespace gltf
} // namespace rndrx::vulkan