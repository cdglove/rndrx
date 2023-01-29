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
#ifndef RNDRX_VULKAN_SHADERCHACHE_HPP_
#define RNDRX_VULKAN_SHADERCHACHE_HPP_
#pragma once

#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {
class Device;
class ShaderCache : noncopyable {
 public:
  struct CachedShader {
    vk::raii::ShaderModule module;
    vk::raii::DescriptorSetLayout descriptor_set_layout;
  };

  CachedShader const& get(std::string_view name) const {
    return shader_cache_.at(hasher_(name));
  }

  CachedShader const&
  add(Device const& device, std::string_view name, std::span<std::uint32_t> code);

 private:
  std::hash<std::string_view> hasher_;
  std::unordered_map<std::uint64_t, CachedShader> shader_cache_;
};

class ShaderLoader : rndrx::noncopyable {
 public:
  ShaderLoader(Device& device, ShaderCache& target_cache)
      : device_(device)
      , cache_(target_cache) {
  }

  void load(std::string_view shader);

 private:
  std::vector<std::uint32_t> buffer_;
  Device& device_;
  ShaderCache& cache_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_SHADERCHACHE_HPP_