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

cbuffer Scene : register(b0) {
  float4x4 g_projection;
  float4x4 g_view;
  float4x4 g_model;
};

// cbuffer Object : register(b1) {
//     float4x4 g_model;
// };

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD) {
  PSInput result;
  float4x4 view_projection = mul(g_projection, g_view);
  float4x4 model_view_projection = mul(view_projection, g_model);
  result.position = mul(model_view_projection, float4(position, 1.0));
  result.uv = uv;
  return result;
}

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(PSInput input)
    : SV_TARGET {
  float4 colour = g_texture.Sample(g_sampler, input.uv);
  return colour;
}
