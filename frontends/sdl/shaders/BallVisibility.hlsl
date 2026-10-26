static const uint emptySlot = 0xFFFFFFFFu;
static const uint drawVertexCount = 6u;

cbuffer VisibilityUniforms : register(b0, space2) {
    uint itemCount;
    uint gridWidth;
    uint gridHeight;
    uint frameSeed;
    float coordinateToSlotX;
    float coordinateToSlotY;
    uint bidderMask;
    uint unusedPadding;
};

uint slotPriority(uint index) {
    uint hash = index ^ frameSeed;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash | 1u;
}

uint slotForPacked(uint packedBall) {
    const uint column = min(uint(float(packedBall & 16383u) * coordinateToSlotX), gridWidth - 1u);
    const uint row = min(uint(float((packedBall >> 14) & 16383u) * coordinateToSlotY), gridHeight - 1u);
    return row * gridWidth + column;
}

#if defined(CLEAR_PASS)
RWStructuredBuffer<uint> priorities : register(u0, space1);
RWStructuredBuffer<uint> winners : register(u1, space1);
RWStructuredBuffer<uint> drawArguments : register(u2, space1);

[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID) {
    if (thread.x == 0u) {
        drawArguments[0] = drawVertexCount;
        drawArguments[1] = 0u;
        drawArguments[2] = 0u;
        drawArguments[3] = 0u;
    }
    if (thread.x < itemCount) {
        priorities[thread.x] = 0u;
        winners[thread.x] = emptySlot;
    }
}
#endif

#if defined(BID_PASS)
StructuredBuffer<uint> balls : register(t0, space0);
RWStructuredBuffer<uint> priorities : register(u0, space1);

[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID) {
    if (thread.x >= itemCount) {
        return;
    }
    const uint priority = slotPriority(thread.x);
    if (((priority >> 1) & bidderMask) != 0u) {
        return;
    }
    const uint slot = slotForPacked(balls[thread.x]);
    if (priority > priorities[slot]) {
        InterlockedMax(priorities[slot], priority);
    }
}
#endif

#if defined(RESOLVE_PASS)
StructuredBuffer<uint> balls : register(t0, space0);
RWStructuredBuffer<uint> priorities : register(u0, space1);
RWStructuredBuffer<uint> winners : register(u1, space1);

[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID) {
    if (thread.x >= itemCount) {
        return;
    }
    const uint packedBall = balls[thread.x];
    const uint slot = slotForPacked(packedBall);
    if (priorities[slot] == slotPriority(thread.x)) {
        winners[slot] = packedBall;
    }
}
#endif

#if defined(COMPACT_PASS)
StructuredBuffer<uint> candidates : register(t0, space0);
RWStructuredBuffer<uint> visibleBalls : register(u0, space1);
RWStructuredBuffer<uint> drawArguments : register(u1, space1);

[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID) {
    if (thread.x >= itemCount) {
        return;
    }
    const uint packedBall = candidates[thread.x];
    if (packedBall == emptySlot) {
        return;
    }
    uint destination;
    InterlockedAdd(drawArguments[1], 1u, destination);
    visibleBalls[destination] = packedBall;
}
#endif
