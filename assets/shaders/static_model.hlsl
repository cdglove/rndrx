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
  float3 position_world : POSITIONT;
  float2 uv : TEXCOORD;
};

cbuffer Scene : register(b0) {
  float4x4 g_projection;
  float4x4 g_view;
  float4x4 g_model;
};

struct Light {
  float3 position;
  float3 colour;
};

static const int kNumLights = 3;
cbuffer Lights : register(b1) {
  Light g_lights[kNumLights];
};

PSInput VSMain(VSInput input) {
  PSInput result;
  float4x4 view_projection = mul(g_projection, g_view);
  float4x4 model_view_projection = mul(view_projection, g_model);
  result.position = mul(model_view_projection, float4(input.position, 1.0));
  float4x4 model_view = mul(g_view, g_model);
  result.normal = mul((float3x3)model_view, input.normal).xyz;
  result.position_world = mul((float3x3)g_model, input.position).xyz;
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
  float3 final_colour = float3(0, 0, 0);
  float3 albedo = g_texture.Sample(g_sampler, input.uv).xyz;
  float3 view = g_view[3].xyz;
  float ambient = 0.2f;
  float light_power = 50;
  float4x4 model_view = mul(g_view, g_model);
  for(int i = 0; i < 1; ++i) {
    float3 light_dir_ws = (g_lights[i].position - input.position_world).xyz;
    float light_distance = length(light_dir_ws);
    float3 light_dir_vs = normalize(mul(g_view, float4(g_lights[i].position, 0)).xyz);
    float n_dot_l = dot(normalize(input.normal), light_dir_vs);
    n_dot_l = max(0.f, n_dot_l);

    float phong = 0.f;
    if(n_dot_l > 0.f) {
      float3 ref = reflect(-light_dir_vs, normalize(input.normal));
      float phong = dot(view, ref);
      phong = clamp(phong, 0.f, 1.f);
      phong = pow(phong, 5.f);
    }

    float attenuation = 1.f / (light_distance * light_distance);
    float3 spec_colour = float3(0.3f, 0.3f, 0.3f);
    float3 colour = albedo * n_dot_l;
    float3 specular = spec_colour * phong;
    float3 lighted = colour + specular;
    lighted *= light_power * g_lights[i].colour * attenuation;

    final_colour = lighted;
  }
  float3 ambient_colour = albedo * ambient;

  final_colour += ambient_colour;
  return float4(final_colour, 1.f);
}
