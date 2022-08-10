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
#pragma once

#define RNDRX_USE_PSO_CACHING 1

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>

// create_pso_with_caching will attempt to use an on disk PSO cache file (defined
// by the tuple |prefix|, |vs|, |fs|) to build a graphics pipeline state object
// using the parameters present in |pso_desc| and the cache contents. If there is
// no cache file present, or if there is an error generating the pipeline state
// object with the cache file, then the pipeline state object will be created using
// only the contents of |pso_desc|. In this case, a new cache file is generated 
// using the data present in the newly created |pipeline|.
void create_pso_with_caching(
    ID3D12Device* device,
    D3D12_GRAPHICS_PIPELINE_STATE_DESC* pso_desc,
    const char* prefix,
    IDxcBlob* vs,
    IDxcBlob* fs,
    ID3D12PipelineState** pipeline);
