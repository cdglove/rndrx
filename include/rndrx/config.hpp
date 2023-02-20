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
#ifndef RNDRX_CONFIG_HPP_
#define RNDRX_CONFIG_HPP_

#define RNDRX_BACKEND_DYNAMIC 0
#define RNDRX_BACKEND_VULKAN  1
#define RNDRX_BACKEND_D3D12   2

#ifndef RNDRX_BACKEND
#  define RNDRX_BACKEND RNDRX_BACKEND_DYNAMIC
#endif

namespace rndrx {
enum class Backend {
  Vulkan = 1,
  D3D12,
};

#if RNDRX_BACKEND == RNDRX_BACKEND_DYNAMIC
extern Backend SelectedBackend;
#elif RNDRX_BACKEND == RNDRX_BACKEND_VULKAN
static constexpr Backend SelectedBackend = Backend::Vulkan;
#elif RNDRX_BACKEND == RNDRX_BACKEND_D3D12
static constexpr Backend SelectedBackend = Backend::D3D12;
#else
#  error "RNDRX_BACKEND musrt be defined to a valid value."
#endif

}

#if RNDRX_STATIC_BACKEND
#  define RNDRX_MAYBE_VIRTUAL
#  define RNDRX_MAYBE_OVERRIDE
#else
#  define RNDRX_MAYBE_VIRTUAL  virtual
#  define RNDRX_MAYBE_OVERRIDE override
#endif

#endif // RNDRX_CONFIG_HPP_