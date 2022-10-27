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
#include <windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>

#include <array>
#include <cstring>
#include <string_view>

// D3DCompile exists only because imgui calls it to create shaders, but we don't
// want a dependency on d3dcompiler.dll in order to make sure built executables
// can easily run everywhere, so we supply our own implemented in terms of dxc.
extern "C" HRESULT WINAPI D3DCompile(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pFileName,
    CONST D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob** ppCode,
    ID3DBlob** ppErrorMsgs) {
  CComPtr<IDxcCompiler3> compiler;
  DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

  std::wstring_view shader_model = L"vs_6_0";
  std::wstring wentry(pEntrypoint, pEntrypoint + std::strlen(pEntrypoint));
  if(!std::strcmp(pTarget, "ps_5_0")) {
    shader_model = L"ps_6_0";
  }

  std::array<LPCWSTR, 4> args = {
      L"-E",
      wentry.c_str(),
      L"-T",
      shader_model.data()};

  CComPtr<IDxcBlobEncoding> source;
  DxcBuffer source_buffer;
  source_buffer.Ptr = pSrcData;
  source_buffer.Size = SrcDataSize;
  source_buffer.Encoding = DXC_CP_ACP;

  CComPtr<IDxcResult> result;
  if(FAILED(compiler->Compile(
         &source_buffer,
         args.data(),
         args.size(),
         nullptr,
         IID_PPV_ARGS(&result)))) {
    return -1;
  }

  CComPtr<IDxcBlobUtf8> errors;
  if(FAILED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))) {
    return -1;
  }

  if(errors && errors->GetStringLength() != 0) {
    OutputDebugStringA(errors->GetStringPointer());
  }

  HRESULT hr_status;
  result->GetStatus(&hr_status);
  if(FAILED(hr_status)) {
    return -1;
  }

  CComPtr<IDxcBlob> code;
  CComPtr<IDxcBlobUtf16> shader_name;
  if(FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), &shader_name))) {
    return -1;
  }
  
  code.p->AddRef();
  *ppCode = reinterpret_cast<ID3DBlob*>(code.p);
  return S_OK;
}
