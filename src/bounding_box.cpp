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
#include "rndrx/bounding_box.hpp"

namespace rndrx {

BoundingBox BoundingBox::get_aabb(glm::mat4 const& aligned_to) const {
  glm::vec3 aabb_min = glm::vec3(aligned_to[3]);
  glm::vec3 aabb_max = aabb_min;
  glm::vec3 v0, v1;

  glm::vec3 right = glm::vec3(aligned_to[0]);
  v0 = right * min_.x;
  v1 = right * max_.x;
  aabb_min += glm::min(v0, v1);
  aabb_max += glm::max(v0, v1);

  glm::vec3 up = glm::vec3(aligned_to[1]);
  v0 = up * min_.y;
  v1 = up * max_.y;
  aabb_min += glm::min(v0, v1);
  aabb_max += glm::max(v0, v1);

  glm::vec3 back = glm::vec3(aligned_to[2]);
  v0 = back * min_.z;
  v1 = back * max_.z;
  aabb_min += glm::min(v0, v1);
  aabb_max += glm::max(v0, v1);

  return {aabb_min, aabb_max};
}

BoundingBox merge(BoundingBox const& a, BoundingBox const& b) {
  return {glm::min(a.min(), b.min()), glm::max(a.max(), b.max())};
}

} // namespace rndrx