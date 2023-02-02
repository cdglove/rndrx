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
#ifndef RNDRX_VULKAN_GLTF_MODEL_HPP_
#define RNDRX_VULKAN_GLTF_MODEL_HPP_
#pragma once

#include <string_view>
#include "tiny_gltf.h"

namespace rndrx::vulkan::gltf {
tinygltf::Model load_model_from_file(std::string_view path);
} // namespace rndrx::vulkan::gltf

#endif // RNDRX_VULKAN_GLTF_MODEL_HPP_