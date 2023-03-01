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
#ifndef RNDRX_VULKAN_ANIMATION_HPP_
#define RNDRX_VULKAN_ANIMATION_HPP_
#pragma once

#include <glm/glm.hpp>
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class Node;

struct AnimationChannel {
  enum class PathType { Translation, Rotation, Scale };
  PathType path;
  Node const* node;
  uint32_t samplerIndex;
};

struct AnimationSampler {
  enum class InterpolationType { Linear, Step, CubicSpline };
  InterpolationType interpolation;
  std::vector<float> inputs;
  std::vector<glm::vec4> outputs;
};

struct Animation {
  std::string name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  float start = std::numeric_limits<float>::max();
  float end = std::numeric_limits<float>::min();
};

class Skeleton : noncopyable {
 public:
  std::string name;
  Node const* skeleton_root = nullptr;
  std::vector<glm::mat4> inverse_bind_matrices;
  std::vector<Node const*> joints;
};
 
} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_ANIMATION_HPP_