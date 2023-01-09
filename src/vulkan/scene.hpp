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
#ifndef RNDRX_VULKAN_SCENE_HPP_
#define RNDRX_VULKAN_SCENE_HPP_
#pragma once

#include <string_view>
#include "rndrx/noncopyable.hpp"

namespace rndrx::vulkan {

class Scene : noncopyable {
 public:
  Scene() = default;
  RNDRX_DEFAULT_MOVABLE(Scene);
};

Scene load_scene(std::string_view path);

} // namespace rndrx::vulkan

#endif // RNDRX_VULKAN_SCENE_HPP_