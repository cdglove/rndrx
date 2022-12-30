// Copyright (c) 2020 Google Inc.
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
struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD;
};

static const float4 positions[] = {
    float4(-1.f, 3.f, 0.f, 1.f),
    float4(3.f, -1.f, 0.f, 1.f),
    float4(-1.f, -1.f, 0.f, 1.f)};

#ifdef __spirv__
// NDC in vulkan puts -1, -1 at the top left
// whereas dx12 puts -1, 1 in the top left. 
static const float2 uvs[] = {
    float2(0, 2.f),
    float2(2, 0),
    float2(0, 0),
  };
#else 
static const float2 uvs[] = {
    float2(0, -1.f),
    float2(2, 1),
    float2(0, 1)};
#endif

PSInput VSMain(uint id : SV_VertexID) {
  PSInput result;
  result.position = positions[id];
  result.uv = uvs[id]; 
  return result;
}

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 CopyImageOpaque(PSInput input)
    : SV_TARGET {
    float4 colour = g_texture.Sample(g_sampler, input.uv);
    return float4(colour.xyz, 1.f);
}

float4 BlendImage(PSInput input)
    : SV_TARGET {
    float4 colour = g_texture.Sample(g_sampler, input.uv);
    return colour;
}

float4 BlendImageInv(PSInput input)
    : SV_TARGET {
    float4 colour = g_texture.Sample(g_sampler, input.uv);
    return float4(colour.xyz, 1 - colour.a);
}

