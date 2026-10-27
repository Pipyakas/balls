struct RectInstance {
    float4 bounds;
    float4 color;
    float4 style;
};

StructuredBuffer<RectInstance> rects : register(t0, space0);

cbuffer RectUniforms : register(b0, space1) {
    float2 viewportSize;
    float2 unusedPadding;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float2 size : TEXCOORD1;
    nointerpolation float4 color : TEXCOORD2;
    nointerpolation float4 style : TEXCOORD3;
};

static const float2 quadCorners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

VertexOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    const RectInstance rect = rects[instanceId];
    const float2 local = quadCorners[vertexId] * rect.bounds.zw;
    const float2 pixel = rect.bounds.xy + local;
    VertexOutput output;
    output.position = float4(pixel.x / viewportSize.x * 2.0 - 1.0, 1.0 - pixel.y / viewportSize.y * 2.0, 0.0, 1.0);
    output.local = local;
    output.size = rect.bounds.zw;
    output.color = rect.color;
    output.style = rect.style;
    return output;
}

float roundedBoxDistance(float2 local, float2 size, float radius) {
    const float2 halfSize = size * 0.5;
    const float2 offset = abs(local - halfSize) - (halfSize - radius);
    return length(max(offset, 0.0)) + min(max(offset.x, offset.y), 0.0) - radius;
}

float4 fragmentMain(VertexOutput input) : SV_Target0 {
    const float cornerRadius = min(input.style.x, min(input.size.x, input.size.y) * 0.5);
    const float bevelHeight = input.style.y;
    const float highlightHeight = input.style.z;
    const float distanceToEdge = roundedBoxDistance(input.local, input.size, cornerRadius);
    const float coverage = saturate(0.5 - distanceToEdge);
    if (coverage <= 0.0) {
        discard;
    }
    float3 color = input.color.rgb;
    if (bevelHeight > 0.0 && input.local.y > input.size.y - bevelHeight) {
        color *= 0.7;
    }
    const float highlightTop = highlightHeight * 0.5;
    if (highlightHeight > 0.0 && input.local.y > highlightTop && input.local.y < highlightTop + highlightHeight &&
        input.local.x > highlightHeight && input.local.x < input.size.x - highlightHeight) {
        color = min(color * 1.25, 1.0);
    }
    return float4(color, input.color.a * coverage);
}
