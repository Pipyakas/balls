// The board's bricks as one rectangle: each pixel looks up the cell under it in the tile-major cell image
// (BrickCellImage), so the cost follows the pixels drawn, not the number of bricks.

StructuredBuffer<uint> shadeWords : register(t0, space0);

cbuffer CellImageUniforms : register(b0, space1) {
    float4 drawRect;
    float2 boardOrigin;
    float2 viewportSize;
    float cellPixels;
    uint columns;
    uint rows;
    uint tilesAcross;
    float4 palette[14];
};

static const uint tileSize = 64;
static const uint cellsPerTile = tileSize * tileSize;
static const uint flashBit = 0x80;
static const uint colorCount = 7;

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
};

static const float2 quadCorners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

VertexOutput vertexMain(uint vertexId : SV_VertexID) {
    const float2 pixel = drawRect.xy + quadCorners[vertexId] * drawRect.zw;
    VertexOutput output;
    output.position = float4(pixel.x / viewportSize.x * 2.0 - 1.0, 1.0 - pixel.y / viewportSize.y * 2.0, 0.0, 1.0);
    output.pixel = pixel;
    return output;
}

float4 fragmentMain(VertexOutput input) : SV_Target0 {
    const float2 cell = floor((input.pixel - boardOrigin) / cellPixels);
    if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= columns || cell.y >= rows) {
        discard;
    }
    const uint column = (uint)cell.x;
    const uint row = (uint)cell.y;
    const uint tile = (row / tileSize) * tilesAcross + column / tileSize;
    const uint index = tile * cellsPerTile + (row % tileSize) * tileSize + column % tileSize;
    const uint shade = (shadeWords[index >> 2] >> ((index & 3) * 8)) & 0xFF;
    if (shade == 0) {
        discard;
    }
    const uint color = (shade & ~flashBit) - 1;
    return palette[(shade & flashBit) != 0 ? color + colorCount : color];
}
