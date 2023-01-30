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
#include "rndrx/vulkan/draw_primitive.hpp"

namespace rndrx::vulkan {

DrawPrimitive::DrawPrimitive(
    std::uint32_t first_index,
    std::uint32_t index_count,
    std::uint32_t vertex_count,
    Material const& material)
    : first_index_(first_index)
    , index_count_(index_count)
    , vertex_count_(vertex_count)
    , material_(material) {
}

void DrawPrimitive::set_bounding_box(glm::vec3 min, glm::vec3 max) {
  bb_.min = min;
  bb_.max = max;
  bb_.valid = true;
}

} // namespace rndrx::vulkan