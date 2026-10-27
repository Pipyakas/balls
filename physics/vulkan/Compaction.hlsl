cbuffer CompactionConstants : register(b0) {
    uint killedCount;
    uint survivorCount;
    uint oldCount;
    uint packedCapacity;
};

StructuredBuffer<uint> killedIndices : register(t0);
RWStructuredBuffer<uint> holes : register(u0);
RWStructuredBuffer<uint> counters : register(u1);
RWStructuredBuffer<uint> positions : register(u2);
RWStructuredBuffer<uint> states : register(u3);
RWStructuredBuffer<uint> packedBalls : register(u4);

static const uint DEAD_COLOR = 7u;
static const uint COLOR_SHIFT = 26u;

[numthreads(256, 1, 1)]
void collectHoles(uint3 thread : SV_DispatchThreadID) {
    const uint index = thread.x;
    if (index >= killedCount) {
        return;
    }
    const uint killed = killedIndices[index];
    if (killed < survivorCount) {
        uint slot = 0u;
        InterlockedAdd(counters[1], 1u, slot);
        holes[slot] = killed;
    }
}

[numthreads(256, 1, 1)]
void fillHoles(uint3 thread : SV_DispatchThreadID) {
    const uint source = survivorCount + thread.x;
    if (thread.x >= killedCount || source >= oldCount || ((states[source] >> COLOR_SHIFT) & 7u) == DEAD_COLOR) {
        return;
    }
    uint slot = 0u;
    InterlockedAdd(counters[2], 1u, slot);
    const uint target = holes[slot];
    positions[target] = positions[source];
    states[target] = states[source];
    if (source < packedCapacity) {
        packedBalls[target] = packedBalls[source];
    }
}
