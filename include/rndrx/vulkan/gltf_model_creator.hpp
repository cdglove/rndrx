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
#ifndef RNDRX_VULKAN_GLTFMODELCREATOR_HPP_
#define RNDRX_VULKAN_GLTFMODELCREATOR_HPP_
#pragma once

#include <span>
#include "rndrx/vulkan/model.hpp"

namespace tinygltf {
class Model;
class Node;
} // namespace tinygltf

namespace rndrx::vulkan {

class GltfModelCreator : public ModelCreator {
 public:
  explicit GltfModelCreator(tinygltf::Model const& source)
      : source_(source) {
  }

 private:
  std::vector<vk::raii::Sampler> create_texture_samplers( //
      Device& device) override;
  
  std::vector<Texture> create_textures(
      Device& device,
      std::vector<vk::raii::Sampler> const& samplers) override;
  
  std::vector<Material> create_materials( //
      std::vector<Texture> const& textures) override;
  
  std::vector<Node> create_nodes(
      Device& device,
      std::vector<Material> const& materials) override;
  
  std::vector<Animation> create_animations( //
      std::vector<Node> const& nodes) override;
  
  std::vector<Skeleton> create_skeletons( //
      std::vector<Node> const& nodes) override;

  std::span<const std::uint32_t> index_buffer() const override {
    return index_buffer_;
  }

  std::span<const Model::Vertex> vertex_buffer() const override {
    return vertex_buffer_;
  }

  void create_nodes_recursive(
      Device& device,
      tinygltf::Node const& source_node,
      Node* parent,
      std::uint32_t node_index,
      std::vector<Material> const& materials,
      std::vector<Node>& nodes);

  tinygltf::Model const& source_;
  std::vector<std::uint32_t> index_buffer_;
  std::vector<Model::Vertex> vertex_buffer_;
  std::uint32_t index_position_ = 0;
  std::uint32_t vertex_position_ = 0;
};

namespace gltf {
tinygltf::Model load_model_from_file(std::string_view path);
}

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_GLTFMODELCREATOR_HPP_