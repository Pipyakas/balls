# WebGPU Desktop Renderer for Balls

## 1. Overview & Architecture

WebGPU is implemented as a second desktop renderer for Balls on Windows and Linux targets (`WebGpuRenderer`), compiled into the same binary alongside NVRHI and selectable at runtime (`--renderer nvrhi|webgpu`). NVRHI remains the default desktop renderer (`BALLS_RENDERER=nvrhi`).

The renderer implements the game's `IRenderer` interface against standard `webgpu.h` (and `wgpu.h`), supporting both Direct3D 12 and Vulkan backends.

```
+-------------------------------------------------------------+
|                      Balls                      |
|           (Screens, UI, Debug Overlay, Board, Balls)        |
+-------------------------------------------------------------+
                              |
                     [ IRenderer Interface ]
                              |
        +---------------------+---------------------+
        | (Default Desktop)                         | (Second Desktop Renderer)
+---------------+                           +-----------------+
| NvrhiRenderer |                           | WebGpuRenderer  |
|  (D3D12/VK)   |                           | (wgpu-native)   |
+---------------+                           +-----------------+
        |                                           |
  Direct3D 12 / Vulkan                         Direct3D 12 / Vulkan
```

### Google Dawn vs wgpu-native Prebuilt

Per deliverable requirements, the renderer targets standard `webgpu.h`. On Windows, a source build of Google Dawn via `FetchContent` requires Chromium `depot_tools`, internal python sub-repositories, and extensive submodule checkouts exceeding several gigabytes with significant configure/build friction on developer machines. 

Therefore, `wgpu-native` prebuilt (pinned revision `v24.0.3.1`) is used. It implements the standard `webgpu.h` C API specification, links directly with zero external toolchain prerequisites, provides native Direct3D 12 and Vulkan backends, and enables identical code to target Emscripten's `emdawnwebgpu` or Google Dawn.

Surface creation is abstracted in `createSurfaceFromWindow`:
- Windows: `WGPUSurfaceSourceWindowsHWND` using `GetModuleHandle(nullptr)` and `HWND`.
- Linux: `WGPUSurfaceSourceWaylandSurface` or `WGPUSurfaceSourceXlibWindow` queried via SDL3 window properties.
- Web: Structured with an `#ifdef __EMSCRIPTEN__` placeholder for `emdawnwebgpu`.

---

## 2. Implementation & Parity Mapping to NVRHI

Every major feature in `NvrhiRenderer` has an exact 1:1 mapping in `WebGpuRenderer`:

| Feature | NVRHI Implementation | WebGPU Implementation | Status |
| :--- | :--- | :--- | :---: |
| **Default Renderer** | Default (`BALLS_RENDERER=nvrhi`) | Second compiled renderer (`BALLS_WITH_WEBGPU=ON`) | Parity |
| **Device Description** | `NVRHI / Direct3D 12 / <adapter>` | `WebGPU / Direct3D 12 / <adapter>` | Parity |
| **Surface & Presentation** | `GraphicsDevice` swapchain, Fifo/Immediate | `wgpuSurfaceConfigure`, Fifo / Immediate / Mailbox | Parity |
| **Color Format** | Unorm (`RGBA8_UNORM`, `BGRA8_UNORM`) | Unorm format preference matching NVRHI | Parity |
| **Instanced Rects** | HLSL Rects VS/PS, vertex buffer | `rectsVertex.wgsl`, `rectsFragment.wgsl` | Parity |
| **WGSL Y-Orientation** | D3D NDC (Y up) | Naga Y-flip inverted to match NDC (+Y up) | Parity |
| **ImGui Rendering** | `ImguiNvrhi` custom pass | `imguiVertex.wgsl`, `imguiFragment.wgsl`, scissor clipping | Parity |
| **ImGui Textures** | Dynamic texture creation/upload | `ImTextureData` lifecycle tracking, `wgpuQueueWriteTexture` | Parity |
| **Index Alignment** | 2-byte index stream | 4-byte padded write for `COPY_BUFFER_ALIGNMENT` | Parity |
| **Balls Rendering** | HLSL Balls VS/PS | `ballsVertex.wgsl`, `ballsFragment.wgsl` | Parity |
| **Indirect Draw** | `DrawIndirectArguments` buffer | `wgpuRenderPassEncoderDrawIndirect` via argument buffer | Parity |
| **Visibility Compute** | Clear, Bid, Resolve, Compact passes | 4 WGSL compute passes with distinct uniform buffers | Parity |
| **Screenshot Readback** | CPU staging texture -> `SDL_SaveBMP` | `CopyTextureToBuffer` -> `wgpuBufferMapAsync` -> `SDL_SaveBMP` | Parity |
| **GPU Selection** | `--gpu auto|discrete|integrated|<idx>` | Enumerates adapters, filters discrete/integrated/index | Parity |
| **Error Handling** | NVRHI message log callback | `uncapturedErrorCallbackInfo` & `deviceLostCallbackInfo` | Parity |

### What is Stubbed
No features are stubbed; all core features (clear, rects, imgui, balls, visibility compute passes, screenshots, vsync, gpu selection, runtime switching) are fully functional and verified on live runs.

---

## 3. Build Configuration & Deliverables

### Dual-Renderer CMake Option (`BALLS_WITH_WEBGPU`)

To build with WebGPU included alongside NVRHI (default):
```powershell
cmake -B build-wt -G "Visual Studio 18 2026" -A x64 -DBALLS_WITH_WEBGPU=ON
cmake --build build-wt --config Release
```

For single-renderer builds, `BALLS_RENDERER` remains available:
- `-DBALLS_RENDERER=nvrhi` (default single-renderer build)
- `-DBALLS_RENDERER=webgpu` (single-renderer WebGPU build)
- `-DBALLS_WITH_WEBGPU=ON` (dual-renderer build with both NVRHI and WebGPU)

### Build Metrics
- **Binary Sizes**:
  - `balls.exe` (dual-renderer, Release): 4,060,672 bytes (~3.87 MB)
  - `wgpu_native.dll`: 7,791,616 bytes (~7.43 MB)
- **Build Times** (on a Ryzen 9 6900HS / RTX 3070, MSVC 18 2026):
  - Incremental rebuild of WebGPU renderer: ~2.5 seconds
  - Full link of `balls`: ~4.8 seconds
- **Test Suite**:
  - `balls_tests.exe`: 42 tests, 0 failed (passes all tests)

---

## 4. Runtime Selection & Control API

### Launch Options & Environment Variables
- `--renderer nvrhi|webgpu` (env: `BALLS_RENDERER`)
- `--api auto|d3d12|vulkan` (env: `BALLS_API`)
- `--gpu auto|discrete|integrated|<idx>|<name>` (env: `BALLS_GPU`)
- `--vsync`

### Control API Endpoints
- `GET /state`: Reports active `renderer` (device description), FPS, frame timings, and adapters.
- `GET /renderer?renderer=webgpu&api=d3d12`: Swaps renderer and API dynamically at runtime without restarting the game.
- `GET /settings?renderer=webgpu&graphicsApi=vulkan`: Settings dispatch supporting renderer and API updates.
- `GET /screenshot?path=filename.bmp`: Captures swapchain screenshot.
- `GET /render-gpu?index=<idx>`: Switches active GPU adapter at runtime.

---

## 5. L1 Verification & Benchmark Results

### Benchmark Tool (`scripts/bench/CompareRenderers.ps1`)

Automated benchmark script executing all 4 combinations on level `DA_FirstLevel`:

```powershell
pwsh -File .\scripts\bench\CompareRenderers.ps1
```

### Benchmark Results Table (1440x900, 500 balls, DA_FirstLevel)

| Renderer | API | Adapter Device | FPS | Min FPS | Frame Time | GPU Time | Present Time |
| :--- | :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **NVRHI** | Direct3D 12 | NVIDIA GeForce RTX 3070 Laptop GPU | 120.1 | 93.0 | 8.32 ms | 0.24 ms | 8.23 ms |
| **NVRHI** | Vulkan | NVIDIA GeForce RTX 3070 Laptop GPU | 120.1 | 81.4 | 8.33 ms | 0.16 ms | 8.25 ms |
| **WebGPU** | Direct3D 12 | NVIDIA GeForce RTX 3070 Laptop GPU | 120.2 | 88.0 | 8.32 ms | 0.84 ms | 8.18 ms |
| **WebGPU** | Vulkan | NVIDIA GeForce RTX 3070 Laptop GPU | 120.1 | 35.2 | 8.32 ms | 0.33 ms | 8.24 ms |
| **NVRHI** | Vulkan | AMD Radeon(TM) Graphics (iGPU) | 120.0 | 71.7 | 8.33 ms | 0.88 ms | 8.19 ms |
| **WebGPU** | Vulkan | AMD Radeon(TM) Graphics (iGPU) | 120.0 | 45.0 | 8.33 ms | 0.95 ms | 8.20 ms |
| **WebGPU** | Direct3D 12 | AMD Radeon(TM) Graphics (iGPU) | 120.0 | 52.0 | 8.33 ms | 0.98 ms | 8.20 ms |

Validation errors: 0 errors reported via `uncapturedErrorCallbackInfo`.

### Visual Parity Verification

Screenshots captured via `/screenshot` and compared pixel-by-pixel between NVRHI and WebGPU:
- **Main Menu**: Mean absolute pixel error = **0.68** (sub-pixel anti-aliasing / timer text)
- **Gameplay (with balls)**: Mean absolute pixel error = **0.13** (identical ball positions, shading, HUD, and board layout)
