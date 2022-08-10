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
#include "pso_caching.h"

#include <Windows.h>
#include <assert.h>
#include <combaseapi.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <filesystem>
#include <sstream>

namespace {

// check_hr will throw an exception if |r| is a failure
void check_hr(HRESULT r, std::string_view message = "unknown failure") {
  if(FAILED(r)) {
    int err = ::GetLastError();
    std::stringstream msg;
    msg << "HRESULT: " << message << " (Error: hr=" << r
        << ", GetLastError=" << err << ")";
    throw std::runtime_error(msg.str());
  }
}

// get_cache_folder will return the folder where PSO caches are to be saved
// to/loaded from.
std::filesystem::path get_cache_folder() {
  PWSTR path;
  auto hr = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &path);
  if(hr != S_OK) {
    return std::filesystem::path();
  }
  std::filesystem::path root(path);
  root = root / "rndrx";
  CoTaskMemFree(path);
  return root;
}

// name_pso will take |prefix| and a hash of |vs| and |fs| to generate a unique
// name for the PSO created from these three parameters. This will be used to
// save/load the cache file on disk.
std::string name_pso(const char* prefix, IDxcBlob* vs, IDxcBlob* fs) {
  const int kHashSize = 16;
  auto fmt = [kHashSize](BYTE* hash) -> std::string {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << std::hex;

    for(int i = 0; i < kHashSize; i++) {
      ss << static_cast<unsigned int>(hash[i]);
    }

    return ss.str();
  };

  BYTE vs_hash[kHashSize];
  HashData((BYTE*)vs->GetBufferPointer(), vs->GetBufferSize(), vs_hash, kHashSize);
  BYTE fs_hash[kHashSize];
  HashData((BYTE*)fs->GetBufferPointer(), fs->GetBufferSize(), fs_hash, kHashSize);

  std::stringstream ss;
  ss << prefix << "-" << fmt(vs_hash) << "-" << fmt(fs_hash) << ".pos";
  return ss.str();
}

// load_cache_blob will load a PSO cache from disk using the |prefix|, |vs|,
// |fs| to identify the file name, returning the data in |cpso|. Returns true if
// the load was successful, otherwise false.
bool load_cache_blob(
    const char* prefix,
    IDxcBlob* vs,
    IDxcBlob* fs,
    D3D12_CACHED_PIPELINE_STATE* cpso) {
  auto root = get_cache_folder();
  auto input = root / name_pso(prefix, vs, fs);

  FILE* f = fopen(input.string().c_str(), "rb");
  if(f == NULL) {
    return false;
  }
  const int kBufferSize = 64 * 1024;
  BYTE* data = static_cast<BYTE*>(malloc(kBufferSize));
  auto nb = fread(data, sizeof(BYTE), kBufferSize, f);
  fclose(f);
  assert(nb < kBufferSize); // TODO(krmoule): allow for arbitrary buffer sizes
  cpso->CachedBlobSizeInBytes = nb;
  cpso->pCachedBlob = data;
  return true;
}

// save_cache_blob will save the PSO data from |pipeline| to a file generated
// from the tuple |prefix|, |vs| and |fs|.
void save_cache_blob(
    ID3D12PipelineState* pipeline,
    const char* prefix,
    IDxcBlob* vs,
    IDxcBlob* fs) {
  auto root = get_cache_folder();
  std::filesystem::create_directories(root);
  auto output = root / name_pso(prefix, vs, fs);

  ID3DBlob* cache_blob;
  auto hr = pipeline->GetCachedBlob(&cache_blob);
  if(hr == S_OK) {
    FILE* f = fopen(output.string().c_str(), "wb");
    if(f != NULL) {
      fwrite(
          cache_blob->GetBufferPointer(),
          sizeof(uint8_t),
          cache_blob->GetBufferSize(),
          f);
      fclose(f);
    }
    cache_blob->Release();
  }
}

} // anonymous namespace

void create_pso_with_caching(
    ID3D12Device* device,
    D3D12_GRAPHICS_PIPELINE_STATE_DESC* pso_desc,
    const char* prefix,
    IDxcBlob* vs,
    IDxcBlob* fs,
    ID3D12PipelineState** pipeline) {
  D3D12_CACHED_PIPELINE_STATE cpso;
  auto res = load_cache_blob(prefix, vs, fs, &cpso);
  if(res) {
    // Use the loaded PSO cache data along side the other state descriptor
    // parameters to hopefully create the pipeline from the cache data.
    pso_desc->CachedPSO.CachedBlobSizeInBytes = cpso.CachedBlobSizeInBytes;
    pso_desc->CachedPSO.pCachedBlob = cpso.pCachedBlob;
    auto hr = device->CreateGraphicsPipelineState(pso_desc, IID_PPV_ARGS(pipeline));
    free(const_cast<void*>(cpso.pCachedBlob));
    if(hr != S_OK) {
      // Fallback to creating without the CachePSO set, assuming the failure
      // was related to a cache mismatch (wrong data, wrong driver, etc.). After
      // creating create the PSO, save a new cache file.
      pso_desc->CachedPSO.CachedBlobSizeInBytes = 0;
      pso_desc->CachedPSO.pCachedBlob = nullptr;
      check_hr(
          device->CreateGraphicsPipelineState(pso_desc, IID_PPV_ARGS(pipeline)));
      save_cache_blob(*pipeline, prefix, vs, fs);
    }
  }
  else {
    // There was some failure to load the blob or it doesn't exist, proceed with
    // creating the pipeline without a cache and generating a new cache from the result.
    check_hr(device->CreateGraphicsPipelineState(pso_desc, IID_PPV_ARGS(pipeline)));
    save_cache_blob(*pipeline, prefix, vs, fs);
  }
}
