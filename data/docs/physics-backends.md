# Physics backends

The physics runs on interchangeable backends: a scalar reference, AVX2 and AVX-512 (Zen4) multithreaded, CUDA,
ROCm (AMD GPUs through the HSA runtime, Linux), Vulkan compute, VulkanMulti (one Vulkan backend across several
GPUs), CUDA+AVX2, a fixed split of the balls between those two, and Hybrid, which runs one backend on each device
the player picks and splits the balls between all of them.

## Plugins

Physics backends are DLL plugins loaded from next to the executable, so physics work only
rebuilds its plugin, not the core or a front end:

```powershell
cmake --build build/win32 --config Release --target balls_physics_cuda
```

Every time a backend is selected the game loads a fresh shadow copy of its DLL from
`%TEMP%\balls\physics-plugins`, so a plugin can be rebuilt while the game runs and
picked up by selecting the backend again (or `/backend?kind=cuda` on the control API). Plugins only see
`source/core/sim/PhysicsBackend.h` and `source/core/sim/kernels/BallStepCore.h`. The build uses the shared MSVC
runtime and copies its DLLs next to the executable.

## Devices and Hybrid

Each plugin lists the devices it can run on, and the game merges them into physical devices: `cpu`, then
`gpu0`, `gpu1`, ... ordered by PCI location, with a GPU that several plugins see listed once (matched by UUID, or
by PCI location for ROCm). `balls_bench --devices` prints them. A selection gives each device at most one backend;
VulkanMulti claims several devices, joined with `|`:

```powershell
balls.exe --backend "AVX2@cpu+CUDA@gpu0+Vulkan@gpu1"   # Hybrid over three devices
balls.exe --backend "vulkan@6700"                       # one backend pinned to a device (name substring)
balls --backend "Zen4@cpu+CUDA@gpu1+ROCm@gpu0"          # AVX-512 CPU, CUDA and ROCm
balls --backend "VulkanMulti@gpu0|gpu1"                 # one Vulkan backend over two GPUs
balls_bench.exe --grid --balls 32000000 --backend "cuda,cuda+avx2,cuda@gpu0+vulkan@gpu1"
```

The same text works in `/backend?kind=` and is what Settings saves. `--backend hybrid` composes the best backend
on the CPU and every discrete GPU.

## Measurements

- [hybrid-physics.md](hybrid-physics.md): how CUDA+AVX2 and Hybrid split the balls, and their results.
- [cuda-limits.md](cuda-limits.md): the CUDA step kernel at the GTX 1650's limits: DRAM bandwidth on an empty
  board (93.7% of copy bandwidth), and on the brick level DRAM bandwidth and the 82 W board power limit.
- [gpu-compute-apis.md](gpu-compute-apis.md): the step kernel and cell sort as HLSL through D3D11, D3D12 and
  Vulkan (`source/tools/gpucompute/`); Vulkan comes within 1.5% of CUDA on the brick level and 6% on the empty board.
- [free-flight.md](free-flight.md): why balls that only meet walls are not stepped.
- [physics-accuracy.md](physics-accuracy.md): the optimized backends against a naive reference.
- [hardware-support.md](hardware-support.md): the oldest GPUs, CPUs, drivers and OS versions each backend and
  renderer API supports.
