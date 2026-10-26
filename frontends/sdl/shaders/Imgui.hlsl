struct ImguiUniforms {
    float2 scale;
    float2 translate;
};

[[vk::binding(0, 0)]] ConstantBuffer<ImguiUniforms> uniforms : register(b0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> atlas : register(t0, space0);
[[vk::binding(2, 0)]] SamplerState atlasSampler : register(s0, space0);

struct VertexInput {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position * uniforms.scale + uniforms.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 fragmentMain(VertexOutput input) : SV_Target0 {
    return input.color * atlas.Sample(atlasSampler, input.uv);
}
