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
#include "model.hpp"

#include <vulkan/vulkan.h>
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
#include "rndrx/vulkan/device.hpp"
#include "rndrx/vulkan/texture.hpp"
#include "rndrx/vulkan/vma/buffer.hpp"
#include "tiny_gltf.h"

constexpr int kTinyGltfNotSpecified = -1;

// ERROR is already defined in wingdi.h and collides with a define in the Draco headers
#if defined(_WIN32) && defined(ERROR) && defined(TINYGLTF_ENABLE_DRACO)
#  undef ERROR
#  pragma message("ERROR constant already defined, undefining")
#endif

namespace {

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

rndrx::vulkan::gltf::Node const* find_node_recursive(
    rndrx::vulkan::gltf::Node const& node,
    uint32_t index) {
  if(node.index == index) {
    return &node;
  }

  rndrx::vulkan::gltf::Node* ret = nullptr;
  for(auto const& child : node.children) {
    if(auto found = find_node_recursive(child, index)) {
      return found;
    }
  }
  return nullptr;
}

rndrx::vulkan::gltf::Node const* node_from_index(
    std::span<const rndrx::vulkan::gltf::Node> nodes,
    std::uint32_t index) {
  for(auto const& node : nodes) {
    if(auto found = find_node_recursive(&node, index)) {
      // If this is true then we don't need this loop
      // we can just detch directly from the array.
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

namespace rndrx::vulkan::gltf {

Mesh::Mesh(Device& device, glm::mat4 matrix) {
  buffer_ = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(sizeof(UniformBlock))
          .setUsage(vk::BufferUsageFlagBits::eUniformBuffer));

  set_world_matrix(matrix);

  descriptor_info_ = vk::DescriptorBufferInfo(*buffer_.vk(), 0, sizeof(UniformBlock));
};

void Mesh::set_bounding_box(glm::vec3 min, glm::vec3 max) {
  bb_.min = min;
  bb_.max = max;
  bb_.valid = true;
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

void Mesh::add_primitive(DrawPrimitive p) {
  primitives_.push_back(std::move(p));
  if(p.bounding_box().valid && !bb_.valid) {
    bb_ = p.bounding_box();
  }
  bb_.min = glm::min(bb_.min, p.bounding_box().min);
  bb_.max = glm::max(bb_.max, p.bounding_box().max);
}

Mesh::UniformBlock* Mesh::mapped_memory() {
  return static_cast<UniformBlock*>(buffer_.mapped_data());
}

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
    if(skin) {
      glm::mat4 inverse_transform = glm::inverse(m);
      size_t num_joints = std::min(skin->joints.size(), kMaxNumJoints);
      mesh->set_num_joints(num_joints);
      for(size_t i = 0; i < num_joints; i++) {
        Node const* joint_node = skin->joints[i];
        glm::mat4 joint_mat = joint_node->resolve_transform_hierarchy() *
                              skin->inverse_bind_matrices[i];
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

Model::Model(Device& device, tinygltf::Model const& source) {
  create_texture_samplers(device, source);
  create_textures(device, source);
  create_materials(source);

  LoaderInfo loader_info;
  create_nodes(device, source, loader_info);
  create_animations(source);
  create_skins(source);
  create_device_buffers(device, loader_info);

  extensions = source.extensionsUsed;

  // getSceneDimensions();
}

void Model::create_texture_samplers(Device& device, tinygltf::Model const& source) {
  for(auto&& gltf_sampler : source.samplers) {
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
}

void Model::create_textures(Device& device, tinygltf::Model const& source) {
  for(auto&& gltf_texture : source.textures) {
    tinygltf::Image const& image = source.images[gltf_texture.source];
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
}

void Model::create_materials(tinygltf::Model const& source) {
  for(auto&& gltf_material : source.materials) {
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
    #error "Need to alloc the descriptor set."
    material.double_sided = gltf_material.doubleSided;

    if(auto properties = get_value("baseColorTexture")) {
      material.base_colour_texture = &textures[properties->TextureIndex()];
      material.tex_coord_sets.base_colour = properties->TextureTexCoord();
    }

    if(auto properties = get_value("baseColorFactor")) {
      material.base_colour_factor = glm::make_vec4(
          properties->ColorFactor().data());
    }

    if(auto properties = get_value("metallicRoughnessTexture")) {
      material.metallic_roughness_texture = &textures[properties->TextureIndex()];
      material.tex_coord_sets.metallic_roughness = properties->TextureTexCoord();
    }

    if(auto properties = get_value("metallicFactor")) {
      material.metallic_factor = properties->Factor();
    }

    if(auto properties = get_value("roughnessFactor")) {
      material.roughness_factor = properties->Factor();
    }

    if(auto properties = get_value("normalTexture")) {
      material.normal_texture = &textures[properties->TextureIndex()];
      material.tex_coord_sets.normal = properties->TextureTexCoord();
    }

    if(auto properties = get_value("emissiveTexture")) {
      material.emissive_texture = &textures[properties->TextureIndex()];
      material.tex_coord_sets.emissive = properties->TextureTexCoord();
    }

    if(auto properties = get_value("occlusionTexture")) {
      material.occlusion_texture = &textures[properties->TextureIndex()];
      material.tex_coord_sets.occlusion = properties->TextureTexCoord();
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
        material.tex_coord_sets.specular_glossiness = text_coord_set.Get<int>();
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
}

void Model::create_nodes(
    Device& device,
    tinygltf::Model const& source,
    LoaderInfo& loader_info) {
  tinygltf::Scene const& scene =
      source.scenes[source.defaultScene > kTinyGltfNotSpecified ? source.defaultScene : 0];

  NodeProperties scene_properties = std::accumulate(
      scene.nodes.begin(),
      scene.nodes.end(),
      NodeProperties(),
      [&source](NodeProperties props, int node_idx) {
        return props +
               get_node_properties_recursive(source.nodes[node_idx], source);
      });

  loader_info.vertex_buffer.resize(scene_properties.vertex_count);
  loader_info.index_buffer.resize(scene_properties.index_count);

  RNDRX_ASSERT(scene_properties.node_count == source.nodes.size());
  nodes.reserve(scene_properties.node_count);

  for(auto&& node_idx : scene.nodes) {
    tinygltf::Node const& node = source.nodes[node_idx];
    create_node_recursive(device, source, node, nullptr, node_idx, loader_info);
  }
}

void Model::create_node_recursive(
    Device& device,
    tinygltf::Model const& source,
    tinygltf::Node const& source_node,
    Node* parent,
    std::uint32_t node_index,
    LoaderInfo& loader_info) {
  nodes.emplace_back(parent);
  Node& new_node = nodes.back();
  new_node.index = node_index;
  new_node.parent = parent;
  new_node.name = source_node.name;
  new_node.skin_index = source_node.skin;
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
    create_node_recursive(
        device,
        source,
        source.nodes[child_idx],
        &new_node,
        child_idx,
        loader_info);
  }

  if(source_node.mesh > kTinyGltfNotSpecified) {
    tinygltf::Mesh const& mesh = source.meshes[source_node.mesh];
    new_node.mesh = Mesh(device, new_node.matrix);
    for(auto&& primitive : mesh.primitives) {
      std::uint32_t vertex_start = loader_info.vertex_position;
      std::uint32_t index_start = loader_info.index_position;
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
      assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

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

      auto get_accessor = [&source, &primitive](
                              std::string_view name) -> tinygltf::Accessor const* {
        auto iter = primitive.attributes.find(name.data());
        if(iter == primitive.attributes.end()) {
          return nullptr;
        }

        return &source.accessors[iter->second];
      };

      auto& position_accessor = *get_accessor("POSITION");
      auto& position_view = source.bufferViews[position_accessor.bufferView];
      BufferExtractor extractor(source, position_accessor);
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
        BufferExtractor extractor(source, *accessor);
        buffer_normals = extractor.float_buffer();
        normal_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3));
      }

      if(auto accessor = get_accessor("TEXCOORD_0")) {
        BufferExtractor extractor(source, *accessor);
        buffer_uv0 = extractor.float_buffer();
        uv0_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2));
      }

      if(auto accessor = get_accessor("TEXCOORD_1")) {
        BufferExtractor extractor(source, *accessor);
        buffer_uv1 = extractor.float_buffer();
        uv1_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2));
      }

      if(auto accessor = get_accessor("COLOR_0")) {
        BufferExtractor extractor(source, *accessor);
        buffer_colour = extractor.float_buffer();
        colour_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3));
      }

      if(auto accessor = get_accessor("JOINTS_0")) {
        BufferExtractor extractor(source, *accessor);
        buffer_joints = extractor.float_buffer();
        joints_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4));
      }

      if(auto accessor = get_accessor("WEIGHTS_0")) {
        BufferExtractor extractor(source, *accessor);
        buffer_weights = extractor.float_buffer();
        weights_stride = extractor.stride_or(
            tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4));
      }

      bool is_skinned = (buffer_joints && buffer_weights);

      for(std::size_t v = 0; v < position_accessor.count; v++) {
        Vertex& vert = loader_info.vertex_buffer[loader_info.vertex_position];
        auto pos_3d = glm::make_vec3(&buffer_positions[v * position_stride]);
        vert.pos = glm::vec4(pos_3d, 1.0f);
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
        loader_info.vertex_position++;
      }

      bool has_indices = primitive.indices > -1;
      if(has_indices) {
        const tinygltf::Accessor& accessor = source.accessors[primitive.indices];
        BufferExtractor extractor(source, accessor);
        index_count = static_cast<std::uint32_t>(accessor.count);

        switch(accessor.componentType) {
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
            std::uint32_t const* buf = extractor.uint32_buffer();
            for(size_t index = 0; index < accessor.count; index++) {
              loader_info.index_buffer[loader_info.index_position] = buf[index] +
                                                                     vertex_start;
              ++loader_info.index_position;
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
            const uint16_t* buf = extractor.uint16_buffer();
            for(size_t index = 0; index < accessor.count; index++) {
              loader_info.index_buffer[loader_info.index_position] = buf[index] +
                                                                     vertex_start;
              ++loader_info.index_position;
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
            const uint8_t* buf = extractor.uint8_buffer();
            for(size_t index = 0; index < accessor.count; index++) {
              loader_info.index_buffer[loader_info.index_position] = buf[index] +
                                                                     vertex_start;
              ++loader_info.index_position;
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
           vertex_count,
           primitive.material > kTinyGltfNotSpecified ? materials[primitive.material]
                                                      : Material()});
    }
  }

  if(parent) {
    parent->children.push_back(&new_node);
  }

  linear_nodes.push_back(&new_node);
}

void Model::create_animations(tinygltf::Model const& source) {
  for(auto&& anim : source.animations) {
    Animation animation;
    animation.name = anim.name;
    if(anim.name.empty()) {
      animation.name = std::to_string(animations.size());
    }

    for(auto&& samp : anim.samplers) {
      AnimationSampler sampler;

      if(samp.interpolation == "LINEAR") {
        sampler.interpolation = AnimationSampler::InterpolationType::Linear;
      }
      if(samp.interpolation == "STEP") {
        sampler.interpolation = AnimationSampler::InterpolationType::Step;
      }
      if(samp.interpolation == "CUBICSPLINE") {
        sampler.interpolation = AnimationSampler::InterpolationType::CubicSpline;
      }

      // Read sampler input time values
      {
        const tinygltf::Accessor& accessor = source.accessors[samp.input];
        const tinygltf::BufferView& bufferView = source.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = source.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        const void* data_ptr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
        const float* buf = static_cast<const float*>(data_ptr);
        for(size_t index = 0; index < accessor.count; index++) {
          sampler.inputs.push_back(buf[index]);
        }

        for(auto input : sampler.inputs) {
          if(input < animation.start) {
            animation.start = input;
          };
          if(input > animation.end) {
            animation.end = input;
          }
        }
      }

      // Read sampler output T/R/S values
      {
        const tinygltf::Accessor& accessor = source.accessors[samp.output];
        const tinygltf::BufferView& bufferView = source.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = source.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        const void* data_ptr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];

        switch(accessor.type) {
          case TINYGLTF_TYPE_VEC3: {
            const glm::vec3* buf = static_cast<const glm::vec3*>(data_ptr);
            for(size_t index = 0; index < accessor.count; index++) {
              sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.f));
            }
            break;
          }
          case TINYGLTF_TYPE_VEC4: {
            const glm::vec4* buf = static_cast<const glm::vec4*>(data_ptr);
            for(size_t index = 0; index < accessor.count; index++) {
              sampler.outputsVec4.push_back(buf[index]);
            }
            break;
          }
          default: {
            LOG(Warn) << "unknown type" << std::endl;
            break;
          }
        }
      }

      animation.samplers.push_back(sampler);
    }

    // Channels
    for(auto& source : anim.channels) {
      AnimationChannel channel{};

      if(source.target_path == "rotation") {
        channel.path = AnimationChannel::PathType::Rotation;
      }
      if(source.target_path == "translation") {
        channel.path = AnimationChannel::PathType::Translation;
      }
      if(source.target_path == "scale") {
        channel.path = AnimationChannel::PathType::Scale;
      }
      if(source.target_path == "weights") {
        std::cout << "weights not yet supported, skipping channel" << std::endl;
        continue;
      }
      channel.samplerIndex = source.sampler;
      channel.node = node_from_index(nodes, source.target_node);
      if(!channel.node) {
        continue;
      }

      animation.channels.push_back(channel);
    }

    animations.push_back(animation);
  }
}

void Model::create_skins(tinygltf::Model const& source) {
  for(auto&& skin : source.skins) {
    skins.emplace_back();
    auto& new_skin = skins.back();
    new_skin.name = skin.name;

    // Find skeleton root node
    if(skin.skeleton > kTinyGltfNotSpecified) {
      new_skin.skeleton_root = node_from_index(nodes, skin.skeleton);
    }

    // Find joint nodes
    for(int joint_index : skin.joints) {
      if(auto joint_node = node_from_index(nodes, joint_index)) {
        new_skin.joints.push_back(joint_node);
      }
    }

    // Get inverse bind matrices from buffer
    if(skin.inverseBindMatrices > -1) {
      auto const& accessor = source.accessors[skin.inverseBindMatrices];
      auto const& bufferView = source.bufferViews[accessor.bufferView];
      const tinygltf::Buffer& buffer = source.buffers[bufferView.buffer];
      new_skin.inverse_bind_matrices.resize(accessor.count);
      std::memcpy(
          new_skin.inverse_bind_matrices.data(),
          &buffer.data[accessor.byteOffset + bufferView.byteOffset],
          accessor.count * sizeof(glm::mat4));
    }
  }
}

void Model::create_device_buffers(Device& device, LoaderInfo const& loader_info) {
  std::size_t vertex_buffer_size = loader_info.vertex_buffer.size() *
                                   sizeof(Vertex);
  std::size_t index_buffer_size = loader_info.index_buffer.size() *
                                  sizeof(std::uint32_t);

  RNDRX_ASSERT(vertex_buffer_size > 0);

  vma::Buffer vertex_staging = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(vertex_buffer_size)
          .setUsage(vk::BufferUsageFlagBits::eTransferSrc));

  vma::Buffer index_staging = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(index_buffer_size)
          .setUsage(vk::BufferUsageFlagBits::eTransferSrc));

  vertices = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(vertex_buffer_size)
          .setUsage(
              vk::BufferUsageFlagBits::eTransferDst |
              vk::BufferUsageFlagBits::eVertexBuffer));

  indices = device.allocator().create_buffer(
      vk::BufferCreateInfo()
          .setSize(index_buffer_size)
          .setUsage(
              vk::BufferUsageFlagBits::eTransferDst |
              vk::BufferUsageFlagBits::eIndexBuffer));

  struct StagingBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
  } vertexStaging, indexStaging;

  auto transfer_cmd = device.alloc_transfer_command_buffer();
  transfer_cmd.begin( //
      vk::CommandBufferBeginInfo().setFlags(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  transfer_cmd.copyBuffer(
      *vertex_staging.vk(),
      *vertices.vk(),
      vk::BufferCopy(0, 0, vertex_buffer_size));

  transfer_cmd.copyBuffer(
      *index_staging.vk(),
      *indices.vk(),
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

Model load_model_from_file(Device& device, std::string_view path) {
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
    rndrx::throw_runtime_error(err.c_str());
  }

  if(!file_loaded) {
    rndrx::throw_runtime_error("Failed to parse glTF");
  }

  return Model(device, model);
}
} // namespace rndrx::vulkan::gltf