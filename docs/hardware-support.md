# Hardware support

The GTX 1650 path is unchanged by everything below: it runs native sm_75 SASS with warp-match ranking, two
balls per thread and the adaptive sort schedule. Older and other hardware gets separate images or backends
that are only picked when the fast one cannot run. Only the 1650 and the i5-12400F were available for testing;
the rest follows from the vendors' published feature and driver support.

## Physics

| Backend | Oldest hardware | Driver / OS | Notes |
|---|---|---|---|
| CUDA, SASS | Maxwell: GTX 750 / 900 series (2014) through Ada (RTX 40) | NVIDIA R527+ | cubins for sm_50/60/70/75/80/86/89; pre-Volta ranks with shared atomics (2.3% slower) |
| CUDA, PTX JIT | Hopper, Blackwell and later | a driver that supports CUDA 12.9 (R575+) | `compute_75` PTX |
| AVX2 | Intel Haswell (2013), AMD Excavator (2015) / Zen (2017) | any | about 1.1-1.25G ball-steps/s on a 6-core |
| Scalar | any x86-64 or ARM64 CPU | any | used automatically without AVX2 |

Kepler (GTX 600/700) cannot run CUDA: CUDA 12 dropped it, and its last driver (R470) cannot load CUDA 12 code.
It still renders and runs the CPU backends.

## Rendering

| API | Requirement | Oldest hardware |
|---|---|---|
| D3D12 (Windows default) | feature level 11.0, shader model 6.0 (DXIL) | NVIDIA Kepler (2012), AMD GCN 1 (2012), Intel Skylake iGPU (2015) |
| Vulkan (Linux default, Windows fallback) | Vulkan 1.3 with dynamic rendering | anything with a 2022+ Vulkan 1.3 driver: NVIDIA Maxwell+, AMD GCN+ (Mesa RADV), Intel Broadwell+ (Mesa) |

If the requested API fails on Windows, the game retries with the other one. Intel Haswell/Broadwell iGPUs
have D3D12 drivers but no shader model 6 and no Windows Vulkan driver, so they are not supported on Windows.
DXIL shaders need Windows 10 1703 or later.
