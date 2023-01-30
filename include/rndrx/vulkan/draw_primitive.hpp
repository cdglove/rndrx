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
#ifndef RNDRX_VULKAN_DRAWPRIMITIVE_HPP_
#define RNDRX_VULKAN_DRAWPRIMITIVE_HPP_
#pragma once

#include <cstdint>
#include "rndrx/noncopyable.hpp"
#include "rndrx/bounding_box.hpp"
#include <glm/glm.hpp>

namespace rndrx::vulkan {

struct Material;

class DrawPrimitive : noncopyable {
 public:
  DrawPrimitive(
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
    return index_count_ > 0;
  }

  BoundingBox bounding_box() const {
    return bb_;
  }

 private:
  std::uint32_t first_index_;
  std::uint32_t index_count_;
  std::uint32_t vertex_count_;
  Material const& material_;
  BoundingBox bb_;
};

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_DRAWPRIMITIVE_HPP_