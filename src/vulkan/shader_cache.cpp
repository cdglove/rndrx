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
#include "rndrx/vulkan/shader_cache.hpp"

#include <spirv_reflect.h>
#include <filesystem>
#include <fstream>
#include "rndrx/vulkan/device.hpp"

namespace rndrx::vulkan {

ShaderCache::CachedShader const& ShaderCache::add(
    Device const& device,
    std::string_view name,
    std::span<std::uint32_t> code) {
  auto key = hasher_(name);
  SpvReflectShaderModule reflected_module;
  spvReflectCreateShaderModule(code.size_bytes(), code.data(), &reflected_module);

  std::vector<vk::DescriptorSetLayoutBinding> layout_bindings;
  for(std::uint32_t i = 0; i < reflected_module.descriptor_binding_count; ++i) {
    SpvReflectDescriptorBinding& reflected_binding =
        reflected_module.descriptor_bindings[i];
    layout_bindings.push_back(vk::DescriptorSetLayoutBinding(
        i,
        static_cast<vk::DescriptorType>(reflected_binding.descriptor_type),
        reflected_binding.count,
        static_cast<vk::ShaderStageFlags>(
            reflected_module.entry_points[0].shader_stage),
        nullptr));
  }
  spvReflectDestroyShaderModule(&reflected_module);

  vk::DescriptorSetLayoutCreateInfo descriptor_set_create_info(
      {},
      (uint32_t)layout_bindings.size(),
      layout_bindings.data());
  auto layout = vk::raii::DescriptorSetLayout(device.vk(), descriptor_set_create_info);

  vk::ShaderModuleCreateInfo module_create_info({}, code.size_bytes(), code.data());
  auto module = device.vk().createShaderModule(module_create_info);
  auto node = shader_cache_.insert(
      std::make_pair(key, CachedShader{std::move(module), std::move(layout)}));
  return node.first->second;
}

void ShaderLoader::load(std::string_view shader) {
  std::filesystem::path p("assets/shaders");
  p /= shader;
  p.concat(".spv");
  auto filesize = std::filesystem::file_size(p);
  std::ifstream instream(p, std::ios::binary);
  buffer_.resize(filesize / sizeof(std::uint32_t));
  instream.read(reinterpret_cast<char*>(buffer_.data()), filesize);
  cache_.add(device_, shader, buffer_);
}

} // namespace rndrx::vulkan
