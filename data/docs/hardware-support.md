# Hardware support

The GTX 1650 path is unchanged by everything below: it runs native sm_75 SASS with warp-match ranking, two
balls per thread and the adaptive sort schedule. Older and other hardware gets separate images or backends
that are only picked when the fast one cannot run. Only the 1650 and the i5-12400F were available for testing;
the rest follows from the vendors' published feature and driver support.

## Physics

| Backend | Oldest hardware | Driver / OS | Notes |
|---|---|---|---|
| CUDA, SASS | Maxwell: GTX 750 / 900 series (2014) through Ada (RTX 40) | NVIDIA R527+ | cubins for sm_50/60/70/75/80/86/89; pre-Volta ranks with shared atomics (2.3% slower) |
| CUDA, PTX JIT | Hopper, Blackwell and later | a driver that supports CUDA 12.9 (R575+) | `compute_75` PTX (sm_90+) and `compute_50` fallback PTX |
| AVX2 | Intel Haswell (2013), AMD Excavator (2015) / Zen (2017) | any | about 1.1-1.25G ball-steps/s on a 6-core |
| Zen4 | AMD Zen 4 (Ryzen 7000, 2022) / Zen 5, or Intel client with AVX-512 (2017-2021) or Xeon | any OS with AVX-512 support | 16-lane AVX-512 vectors; about 1.1x AVX2 on Ryzen 7 7700; requires F/BW/DQ/VL/VBMI2/VPOPCNTDQ |
| Scalar | any x86-64 or ARM64 CPU | any | used automatically without AVX2 |
| ROCm | AMD RDNA 2 discrete GPUs (2020: RX 6800/6900/6700 XT) | Linux, ROCm HSA runtime (`libhsa-runtime64.so.1`) | OpenCL C kernels via HSA AQL dispatch without HIP; embeds gfx1030/gfx1031 code objects |
| Vulkan | GPU with Vulkan 1.3 compute: NVIDIA Maxwell+, AMD GCN+, Intel Skylake+ | Windows (Vulkan 1.3 driver) or Linux (Mesa RADV/ANV or proprietary) | NVRHI headless compute device; within 1.5% of CUDA on brick level |
| VulkanMulti | Two or more GPUs supporting Vulkan 1.3 compute | same as Vulkan | one Vulkan backend spanning several GPUs, driven from a single thread |
| CUDA+AVX2 | NVIDIA Maxwell+ GPU (2014) and Haswell+ CPU (2013, AVX2) | NVIDIA R527+ on Windows or Linux | fixed CUDA+AVX2 pair (`HybridBackend`); preallocated startup backend in scoring build (`BALLS_PROFILE=scoring`) |
| Hybrid | Any supported CPU and GPU combination | depends on composed backends | device composer (`DeviceComposer`), one backend per picked physical device; default in extended build |

Kepler (GTX 600/700) cannot run CUDA: CUDA 12 dropped it, and its last driver (R470) cannot load CUDA 12 code.
It still renders (via D3D12) and runs the CPU backends.

## Rendering

| API | Requirement | Oldest hardware |
|---|---|---|
| Vulkan (default on Windows and Linux) | Vulkan 1.3 with dynamic rendering | anything with a 2022+ Vulkan 1.3 driver: NVIDIA Maxwell+, AMD GCN+ (Mesa RADV), Intel Broadwell+ (Mesa) |
| D3D12 (Windows fallback or opt-in) | feature level 11.0, shader model 6.0 (DXIL) | NVIDIA Kepler (2012), AMD GCN 1 (2012), Intel Skylake iGPU (2015) |

If the requested API fails on Windows, the game retries with the other one. Intel Haswell/Broadwell iGPUs
have D3D12 drivers but no shader model 6 and no Windows Vulkan driver, so they are not supported on Windows.
DXIL shaders need Windows 10 1703 or later.
