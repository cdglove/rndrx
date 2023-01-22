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
#include "rndrx/log.hpp"
#include "rndrx/throw_exception.hpp"
#include "scene.hpp"

#include "tiny_gltf.h"

namespace rndrx::vulkan {

Scene load_scene(std::string_view path) {
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.data());

  if(!warn.empty()) {
    LOG(Warn) << warn.c_str();
  }

  if(!err.empty()) {
    rndrx::throw_runtime_error(err.c_str());
  }

  if(!ret) {
    rndrx::throw_runtime_error("Failed to parse glTF");
  }

  return {};
}

} // namespace rndrx::vulkan
