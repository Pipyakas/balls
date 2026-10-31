# WebGPU bindings for SDL front-end shaders

Generated WGSL lives in `source/frontends/sdl/shaders/generated/web/`. Entry points keep their HLSL names (`vertexMain`, `fragmentMain`, `computeMain`).
WebGPU has no push constants: uniform data is written to a bound uniform buffer
(`wgpuQueueWriteBuffer` or a mapped buffer) before the draw/dispatch (replacing SDL_gpu's `SDL_PushGPU*UniformData`).

## Balls (Balls.hlsl)

- `ballsVertex.wgsl` (`vertexMain`), `ballsFragment.wgsl` (`fragmentMain`).
- Bindings:
  - group 0, binding 0: storage buffer `visibleBalls` (read-only, `array<u32>`), from `StructuredBuffer<uint> visibleBalls` (register t0, space0). Holds packed visible ball indices.
  - group 1, binding 0: uniform buffer `BallUniforms` (register b0, space1): `boardOrigin`, `unpackScale`, `viewportSize`, `cellPixels`, `radiusPixels`, `palette[7]`.
- Vertex layout: no vertex buffers. `@builtin(vertex_index)` (0..5 quad corner) and `@builtin(instance_index)` (into visibleBalls). Fragment consumes `local` (vec2) and `tint` (flat vec3).
- Draw: indirect, `wgpuRenderPassEncoderDrawIndirect` over `argumentBuffer` (6 vertices per ball, instance count from compact pass), or direct `wgpuRenderPassEncoderDraw` (6 vertices, direct count) when compact is bypassed. Vertex storage buffer bound to `visibleBuffer`.

## Rects (Rects.hlsl)

- `rectsVertex.wgsl` (`vertexMain`), `rectsFragment.wgsl` (`fragmentMain`).
- Bindings:
  - group 0, binding 0: storage buffer `rects` (read-only, `array<RectInstance>`), register t0, space0.
  - group 1, binding 0: uniform buffer `RectUniforms` (register b0, space1): `viewportSize`, padding.
- Vertex layout: no vertex buffers. `vertex_index` selects a quad corner from a constant array; `instance_index` indexes `rects`. Fragment uses `local`, `size` (flat), `color` (flat), `style` (flat).
- Draw: `wgpuRenderPassEncoderDraw` with 6 vertices and `instanceCount = pendingRects.size()`.

## Imgui (Imgui.hlsl)

- `imguiVertex.wgsl` (`vertexMain`), `imguiFragment.wgsl` (`fragmentMain`).
- Bindings (all group 0):
  - binding 0: uniform buffer `ImguiUniforms` (register b0, space0): `scale`, `translate`.
  - binding 1: sampled texture `atlas` (register t0, space0).
  - binding 2: sampler `atlasSampler` (register s0, space0).
- Vertex layout: vertex buffer with `position: vec2` (location 0), `uv: vec2` (location 1), `color: vec4` (location 2).
- Draw: `wgpuRenderPassEncoderDrawIndexed` per ImGui draw command with scissor clipping via `wgpuRenderPassEncoderSetScissorRect`.

## BallVisibility (BallVisibility.hlsl) - compute

- Single entry point `computeMain`, generated as four separate WGSL modules corresponding to HLSL preprocessor defines: `visibilityClear.wgsl` (`CLEAR_PASS`), `visibilityBid.wgsl` (`BID_PASS`), `visibilityResolve.wgsl` (`RESOLVE_PASS`), `visibilityCompact.wgsl` (`COMPACT_PASS`).
- Uniforms: group 2, binding 0 = `VisibilityUniforms` (register b0, space2): `itemCount`, `gridWidth`, `gridHeight`, `frameSeed`, `coordinateToSlotX`, `coordinateToSlotY`, `bidderMask`, padding.
- Storage (group 1 unless noted):
  - CLEAR: priorities (rw, binding 0), winners (rw, binding 1), drawArguments (rw, binding 2). group 0 has no buffers.
  - BID: group 0 binding 0 = balls (read), group 1 binding 0 = priorities (rw).
  - RESOLVE: group 0 binding 0 = balls (read), group 1 binding 0 = priorities (rw), group 1 binding 1 = winners (rw).
  - COMPACT: group 0 binding 0 = candidates (read), group 1 binding 0 = visibleBalls (rw), group 1 binding 1 = drawArguments (rw).
- Workgroup size: (256, 1, 1). Dispatch `ceil(items / 256)`.

## WebGPU limitations / caveats

- No wave intrinsics are used here, so the WGSL port is direct. If future shaders use wave ops, WebGPU only exposes them via the `subgroups` feature with different intrinsics.
- Workgroup storage is not used; all state is in storage buffers, so the 16 KiB workgroup storage limit does not apply. Storage buffer sizes are governed by device limits (maxStorageBufferBindingSize).
- `InterlockedMax`/`InterlockedAdd` map to WGSL `atomicMax`/`atomicAdd` on `atomic<u32>` storage; the generated WGSL uses `var<storage, read_write>` arrays, which naga/ WebGPU accept for these builtins. The HLSL read-modify-write pattern (`if (priority > priorities[slot]) InterlockedMax(...)`) is preserved.
- Coordinate space: SPIR-V targets Vulkan (Y down); WGSL targets WebGPU (Y up). naga's conversion negates the output Y; the checked-in vertex shaders in `WgslShaders.h` drop that negation (`global_2.y = _e8;`) so the image is not flipped. Native Vulkan path is unaffected.
- `nointerpolation` becomes `@interpolate(flat)`.
