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

## Tuning that is not tied to the 1650

Constants hand-picked on one GPU do not carry to others, so the follow-up work either changes the work itself
or measures on the running GPU:

- **Shared sort schedule.** CUDA already timed sorting every 0, 8 and 16 ticks and kept the cheapest, retrying
  every 1200 ticks. That logic now lives in `core/sim/SortSchedule.h` and drives both CUDA and the compute
  shaders (`--sort-interval auto`, the default). It picks no sorting on the empty board and sorting on
  the brick level on any GPU, instead of a fixed 8.
- **Four balls per thread, processed in a real loop.** Loading four balls before stepping any of them keeps
  more memory requests in flight. The first try unrolled the four calls into the step body, and the brick
  level got 30-80% slower, because four copies of the step body spill registers and overflow the
  instruction cache. With `[loop]` on the processing loop it wins on both boards and every API. CUDA's nvcc
  build was already at its best with two, so CUDA keeps two.
- **CUDA on pre-Turing GPUs.** The PTX was `compute_75` only, because `__match_any_sync` needs sm_70. The
  build now also emits `compute_60` PTX that ranks balls inside a cell with a shared-memory `atomicAdd`, and
  the driver picks a variant by compute capability. Forced onto the 1650, the fallback passes the tests
  and is 2.3% slower (9.67 vs 9.45 ms per tick).
- **SASS for every shipped architecture.** PTX from NVRTC 12.9 is ISA 8.8, which only a CUDA 12.9 driver can
  JIT. The plugin now also embeds cubins for sm_50, 60, 70, 75, 80, 86 and 89, which load on any R527+ driver
  through CUDA minor-version compatibility, and the fallback PTX moved down to `compute_50` for Maxwell. The
  loader tries the newest same-major cubin, then the newest PTX at or below the device. On the 1650 the sm_75
  cubin and the JIT-compiled PTX run within noise (7.09-7.13 vs 7.07-7.38 ms per tick, 60M scattered balls).

Same three-round interleaving, 60M balls:

| ms per tick | Brick level, sort every 8: before | after | Empty board: before (sort every 8) | after (adaptive) |
|---|---|---|---|---|
| Vulkan | 9.65-9.67 | 9.59-9.61 | 7.13 | 6.84-7.05 |
| D3D12 | 10.42-10.46 | 10.18-10.20 | 7.61-7.63 | 7.15 |
| D3D11 | 10.47-10.48 | 9.97-9.98 | 8.29 | 7.34-7.35 |

## Running it

```
python tools/gpucompute/CompileBallStep.py
cmake --build build --config Release --target balls_gpucompute
build/bin/Release/balls_gpucompute --dir assets/levels/performance --level DA_1000Bricks --balls 60000000 --ticks 400 --api all
```

## Per-profile variants and sweeping

Compute profiles allow tuning kernel launch dimensions and memory parameters (such as `threadsPerGroup`, `ballsPerThread`, `countThreads`, `countItems`, `sortThreads`, and `sortItems`) for different GPU architectures (e.g. wave32 vs wave64). Profiles are defined in `core/gpu/BallStepProfiles.def` and compiled by `CompileBallStep.py`.

The compiled shader arrays follow the blob naming scheme:
`ballStep_<profile>_<Pass><Api>`
where:
- `<profile>` is the profile variant identifier (such as `wave32`, `wave64`, or a dynamic sweep profile `sweepNN`),
- `<Pass>` is one of `BallStep`, `CountCells`, `ScanCells`, or `StepAndSort`,
- `<Api>` is the target shader API format (`Spirv`, `Dxil`, or `Dxbc`).

The benchmark harness `balls_gpucompute` supports adapter querying and profile selection:
- `--list-gpus`: queries and prints available graphics adapters with their device information (index, summary, vendor/device hex IDs, `subgroup=<min>-<max>`, shared memory bytes, max workgroup invocations, and device-local MB).
- `--gpu <auto|integrated|discrete|N|substring>`: selects the target GPU adapter by index, name substring, or auto-selection heuristic (prefers the discrete GPU with the largest device-local memory, falling back to integrated).
- `--profile <name>`: selects a precompiled profile variant from `BallStepProfiles.def` by name.
- `--profile-numbers a,b,c,d,e,f`: sets the profile parameters `(threadsPerGroup, ballsPerThread, countThreads, countItems, sortThreads, sortItems)` explicitly at runtime.

`tools/gpucompute/Sweep.py` automates exploring candidate configurations, compiling individual shader variants with `CompileBallStep.py`, benchmarking each with `balls_gpucompute`, and ranking results by kernel execution time:

```
python -I tools/gpucompute/Sweep.py --bench build/bin/Release/balls_gpucompute --api vulkan --gpu auto --balls 1000000 --ticks 100
```
