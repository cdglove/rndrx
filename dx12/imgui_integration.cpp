#include <Windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <array>
#include <cstring>
#include <string_view>

extern "C" HRESULT WINAPI DxcCompile(
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
      shader_model.data());

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

#define D3DCompile DxcCompile

#include "imgui_impl_dx12.cpp"