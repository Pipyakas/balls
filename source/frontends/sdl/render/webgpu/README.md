# WebGPU Desktop Renderer for Balls

## 1. Overview & Architecture

The WebGPU renderer (`WebGpuRenderer`) provides a modern, cross-platform graphics backend implementing the application's `IRenderer` interface alongside the default NVRHI renderer.

On desktop targets (Windows and Linux), the renderer is powered by **`wgpu-native`** (revision `v24.0.3.1`), providing standard `webgpu.h` and `wgpu.h` interfaces with backend support for Direct3D 12 and Vulkan.

```
+-------------------------------------------------------------+
|                      Balls                      |
|           (Screens, UI, Debug Overlay, Board, Balls)         |
+-------------------------------------------------------------+
                              |
                     [ IRenderer Interface ]
                              |
        +---------------------+---------------------+
        |                                           |
+---------------+                           +-----------------+
| NvrhiRenderer |                           | WebGpuRenderer  |
|  (D3D12/VK)   |                           |  (wgpu-native)  |
+---------------+                           +-----------------+
        |                                           |
  Native D3D12/VK                              D3D12 / Vulkan
```

### Key Components

- **Instance & Surface Management**:
  - `wgpuCreateInstance(nullptr)` creates the WebGPU instance.
  - Surface creation binds to the native window handle (`HWND` via `WGPUSType_SurfaceSourceWindowsHWND` on Windows; Wayland/X11 on Linux).
  - Swapchain configuration uses standard formats (`Bgra8Unorm` / `Rgba8Unorm`) with `WGPUPresentMode_Fifo` (vsync enabled) or `WGPUPresentMode_Immediate` (vsync disabled).
- **Shaders & Pipeline**:
  - Pure WGSL shaders (`rectsVertex.wgsl` and `rectsFragment.wgsl`) imported into `WgslShaders.h`.
  - Shader coordinates preserve WebGPU NDC conventions ($[-1, 1]$ with Y-up) to render UI panels and board cells right-side up.
  - Instanced rounded rect pipeline with uniform buffer (`Group 0`: screen dimensions) and storage buffer (`Group 1`: instance rect attributes including position, size, radii, colors, and border widths).

---

## 2. Build Configuration

The WebGPU renderer can be compiled into the binary alongside NVRHI using the CMake option `BALLS_WITH_WEBGPU`:

```powershell
cmake -B build/webgpu -G "Visual Studio 18 2026" -A x64 -DBALLS_WITH_WEBGPU=ON
cmake --build build/webgpu --config Release
```

- When `BALLS_WITH_WEBGPU=ON`, CMake automatically pulls `wgpu-native` v24.0.3.1 prebuilts via `FetchContent` and links `wgpu_native.dll.lib` (copying `wgpu_native.dll` post-build into the output directory).
- Single-renderer builds can also be selected directly with `-DBALLS_RENDERER=webgpu`.

---

## 3. Runtime Selection & Controls

### Command Line Flags
- `--renderer nvrhi|webgpu`: Selects the active rendering engine at startup.
- `--api auto|d3d12|vulkan`: Selects the graphics API backend.
- `--gpu auto|discrete|integrated|<name>`: Selects the preferred physical adapter.
- `--vsync`: Toggles vertical synchronization.

### In-Game Settings UI
Users can switch renderers directly in the game's **Settings Screen**:
- **Renderer**: Toggle between `NVRHI` (default) and `WebGPU`.
- **Graphics API**: Toggle between `Auto`, `Direct3D 12`, and `Vulkan`.
- Settings are persisted per-machine to the local preferences directory (`settings.json`).

---

## 4. Feature Parity & Status

| Feature | NVRHI (Default) | WebGPU (wgpu-native) | Status |
| :--- | :---: | :---: | :---: |
| Native Window Surface | Supported | Supported | Parity |
| Clear & Present | Supported | Supported | Parity |
| Rounded Rects / UI / Board | Supported | Supported (WGSL instanced) | Parity |
| Settings UI Selection | Supported | Supported | Parity |
| Control Protocol / State | Supported | Supported | Parity |
| ImGui Integration | Supported | Stubbed in v1 | Future |
| Instanced Circles / Balls | Supported | Fallback to rects / Stubbed | Future |
| Screenshot Capture | Supported | Stubbed in v1 | Future |
