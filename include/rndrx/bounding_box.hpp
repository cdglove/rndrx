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
#ifndef RNDRX_BOUNDINGBOX_HPP_
#define RNDRX_BOUNDINGBOX_HPP_
#pragma once

#include <glm/glm.hpp>

namespace rndrx {
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
} // namespace rndrx

#endif // RNDRX_BOUNDINGBOX_HPP_