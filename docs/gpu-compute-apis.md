# Ball physics as D3D11, D3D12 and Vulkan compute shaders

Can plain graphics-API compute replace CUDA? `tools/gpucompute/` ports the CUDA step kernel and the cell sort
to HLSL and runs them headless through NVRHI on all three APIs. CUDA is NVIDIA-only; these shaders run on
any D3D11/D3D12/Vulkan GPU.

## The port

- `BallStep.hlsl` mirrors `physics/cuda/BallKernels.cu`: the same 8-byte ball encoding, the near-brick cell
  words, two balls per thread, device-side hit consumption with compare-exchange, and the three sort passes
  (count, scan, fused step-and-scatter into a second buffer).
- One source compiles to DXBC with FXC (`cs_5_0`, for D3D11) and to DXIL and SPIR-V with DXC
  (`CompileBallStep.py` writes `generated/BallStepBlobs.h`).
- CUDA ranks balls inside a cell with `__match_any_sync`. D3D11 has no wave operations, so every API ranks
  with a group-shared `InterlockedAdd` instead.
- `balls_gpucompute` checks itself: after the run it reads the balls back and verifies that every color's
  count went down only by the balls killed on bricks. All three APIs report the same kills.

## Results

GTX 1650, 60M balls, 2 substeps, GPU timer queries around the dispatches, three interleaved rounds (the board
power limit makes back-to-back runs drift). CUDA is the sum of its kernels per tick in steady state, after the
adaptive sort has settled (`balls_insncount --mode timeline`).

| ms per tick | Brick level, unsorted | Brick level, sort every 8 ticks | Empty board |
|---|---|---|---|
| CUDA | 12.3-13.1 | 9.39-9.44 (adaptive) | 6.18 |
| Vulkan | 13.35 | 9.51-9.57 | 6.57-6.59 |
| D3D12 | 15.1 | 10.21-10.32 | 6.84-6.87 |
| D3D11 | 14.0 | 10.31-10.39 | 7.22-7.27 |

- Vulkan is within 1.5% of CUDA on the sorted brick level and 6% behind on the memory-bound empty board.
- D3D12 runs the same HLSL 8-11% slower than Vulkan. D3D11 is 10-17% slower than CUDA, and NVRHI's D3D11
  wait-for-idle sleeps on the 15.6 ms Windows timer, so a frame that waits on the GPU loses a tick there.
- Sorting every 16 ticks is slightly slower than every 8 on all three APIs.

So a Vulkan compute backend would carry nearly all of CUDA's throughput to any vendor's GPU. The remaining
gap on the empty board is the price of bounds-checked structured buffers and compiler differences, not
the algorithm.

## Running it

```
python tools/gpucompute/CompileBallStep.py
cmake --build build --config Release --target balls_gpucompute
build/bin/Release/balls_gpucompute --dir assets/levels/performance --level DA_1000Bricks --balls 60000000 --ticks 160 --sort-interval 8 --api all
```
