StructuredBuffer<uint> visibleBalls : register(t0, space0);

cbuffer BallUniforms : register(b0, space1) {
    float2 boardOrigin;
    float2 unpackScale;
    float2 viewportSize;
    float cellPixels;
    float radiusPixels;
    float4 palette[7];
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float3 tint : TEXCOORD1;
};

static const float2 quadCorners[6] = {
    float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
    float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
};

VertexOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    const uint packedBall = visibleBalls[instanceId];
    const float2 cell = float2(packedBall & 16383u, (packedBall >> 14) & 16383u) * unpackScale;
    const float2 corner = quadCorners[vertexId];
    const float2 pixel = boardOrigin + cell * cellPixels + corner * radiusPixels;
    VertexOutput output;
    output.position = float4(pixel.x / viewportSize.x * 2.0 - 1.0, 1.0 - pixel.y / viewportSize.y * 2.0, 0.0, 1.0);
    output.local = corner;
    output.tint = palette[min(packedBall >> 28, 6u)].rgb;
    return output;
}

float4 fragmentMain(VertexOutput input) : SV_Target0 {
    const float distanceSquared = dot(input.local, input.local);
    if (distanceSquared > 1.0) {
        discard;
    }
    const float highlight = saturate(1.0 - length(input.local - float2(-0.35, -0.4)) * 1.6);
    const float3 shaded = input.tint * (0.65 + 0.35 * (1.0 - distanceSquared)) + highlight * highlight * 0.55;
    return float4(shaded, 1.0);
}
