struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD;
};

PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD) {
  PSInput result;
  result.position = position;
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

float4 PSMainInv(PSInput input)
    : SV_TARGET {
    float4 colour = g_texture.Sample(g_sampler, input.uv);
    return float4(colour.xyz, 1 - colour.a);
}

