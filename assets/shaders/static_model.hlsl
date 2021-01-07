// Copyright (c) 2021 Google Inc.
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

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD;
};

struct PSInput {
  float4 position : SV_POSITION;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD;
};

cbuffer Scene : register(b0) {
  float4x4 g_projection;
  float4x4 g_view;
  float4x4 g_model;
};

PSInput VSMain(VSInput input) {
  PSInput result;
  float4x4 view_projection = mul(g_projection, g_view);
  float4x4 model_view_projection = mul(view_projection, g_model);
  result.position = mul(model_view_projection, float4(input.position, 1.0));
  float4x4 model_view = mul(g_view, g_model);
  result.normal = mul(model_view, input.normal).xyz;
  result.uv = input.uv;
  return result;
}


Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 Albedo(PSInput input)
    : SV_TARGET {
  float4 colour = g_texture.Sample(g_sampler, input.uv);
  return colour;
}

float4 Phong(PSInput input)
    : SV_TARGET {
  float3 point_light = float3(10, -10, 0);
  float3 light_dir = point_light - input.position.xyz;
  float light_distance = length(light_dir);
  float n_dot_l = dot(input.normal, light_dir);
  n_dot_l = max(0.f, n_dot_l);

  float phong = 0.f;
  if(n_dot_l > 0.f) {
    float3 view = g_view[3].xyz;
    float3 ref = reflect(-light_dir, input.normal);
    float phong = dot(view, ref);
    phong = clamp(phong, 0.f, 1.f);
    phong = pow(phong, 16.f);
  }

  float3 albedo = g_texture.Sample(g_sampler, input.uv).xyz;
  float attenuation = 1.f / light_distance;
  float3 spec_colour = float3(0.f, 0.f, 1.f);
  float ambient = 0.2f;
  float3 lighted = (albedo * attenuation * n_dot_l) +
                   (spec_colour * attenuation * phong) + (albedo * ambient);
  return float4(lighted, 1.f);
}
