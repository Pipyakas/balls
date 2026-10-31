# UE5 high-end rendering features for Balls

Research note for the Unreal front end at `source/frontends/unreal`.  
Engine reference: stock Unreal Engine 5.8.4 (e.g. `D:\Unreal\UE_5.8`, fallback `D:\Unreal\UnrealEngine`).  
Scope: read-only analysis and implemented high-end configuration.

## Baseline (current project)

| Item | Current value | Source |
|---|---|---|
| Map | `/Engine/Maps/Entry`, code-spawned scene | `Config/DefaultEngine.ini` |
| Desktop shader format | `PCD3D_SM6` and `PCD3D_SM5` fallback | `Config/DefaultEngine.ini`, `Config/Windows/WindowsEngine.ini` |
| Android shader platform | `VULKAN_ES3_1_ANDROID` (`bBuildForES31=True`) | `Config/DefaultEngine.ini` |
| Renderer | AutoExposure off, MotionBlur off, Bloom on, TAA (`r.AntiAliasingMethod=2`) | `Config/DefaultEngine.ini` |
| GI | None (`r.DynamicGlobalIlluminationMethod=0`) default; Lumen selectable at runtime | `Config/DefaultEngine.ini`, `Source/Balls/Private/BallsGraphics.cpp` |
| Reflections | None (`r.ReflectionMethod=0`) default; Screen space or Lumen selectable at runtime | `Config/DefaultEngine.ini`, `Source/Balls/Private/BallsGraphics.cpp` |
| Shadows | VSM off (`r.Shadow.Virtual.Enable=0`) default; key light casts shadows when enabled (Shadow maps or Virtual) | `Config/DefaultEngine.ini`, `Source/Balls/Private/BallsBoardView.cpp`, `Source/Balls/Private/BallsGraphics.cpp` |
| Nanite | Project enabled (`r.Nanite.ProjectEnabled=True`); runtime toggleable | `Config/DefaultEngine.ini`, `Source/Balls/Private/BallsGraphics.cpp` |
| Scene | Runtime-spawned `ACameraActor`, two `ADirectionalLight`s, ~200-cell instanced-cube backdrop, one ISM per brick color using `/Game/Balls/NaniteCube` (falling back to `/Engine/BasicShapes/Cube`) + `BasicShapeMaterial` MIDs, custom RDG ball pass after motion blur | `Source/Balls/Private/BallsBoardView.cpp`, `Source/BallsRendering/...` |

Important constraint: the project defaults to stock Unreal Engine 5.8.4 (linking prebuilt engine modules and cleanly building SM6). On desktop, `Config/DefaultEngine.ini` targets `PCD3D_SM6` alongside `PCD3D_SM5` as fallback. For the engine fork, `Config/Windows/WindowsEngine.ini` disables SM6 bindless and `r.AreShaderErrorsFatal=0` bypasses unused global shader compile errors. Lumen, Nanite, and VSM on desktop require SM6 or a Vulkan-SM5 path; see the platform gates below.

## 1. Lumen (GI + reflections)

### Requirements

- Project/renderer settings:
  - `r.DynamicGlobalIlluminationMethod=1` (Lumen)
  - `r.ReflectionMethod=1` (Lumen)
  - `r.GenerateMeshDistanceFields=1` for Software Ray Tracing; otherwise Lumen can still run hardware ray tracing if enabled, but the scene has no RT requirement.
  - `r.Lumen.Supported=1` (default; read-only, so it must be in config before startup).
- Assets/meshes:
  - Lumen software tracing uses mesh distance fields. The bricks are runtime-spawned ISMs over `/Engine/BasicShapes/Cube`; distance fields must exist for that mesh in the cooked build.
  - Lumen also builds a surface cache from visible meshes. Runtime-spawned movable ISMs can contribute, but expect more CPU/GPU overhead than static meshes.
- Actors:
  - Works with runtime-spawned actors and movable lights; no baked lighting or level-placed actors are required.
  - The scene has no sky light; Lumen GI will come from the two movable directional lights and emissive/colored materials.
- ISM support:
  - ISMs are supported by Lumen mesh processing, but all instances share one mesh/material. Colored MIDs are fine for Lumen diffuse, but mirror-like reflections depend on roughness/metalness in `BasicShapeMaterial`.

### Android (Vulkan ES3.1)

Not supported. Evidence: `Engine/Config/Android/DataDrivenPlatformInfo.ini` has no `bSupportsLumenGI=true` for `VULKAN_ES3_1_ANDROID`, while `Engine/Source/Runtime/RHI/Private/DataDrivenShaderPlatformInfo.cpp` defaults unset booleans to false. `Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp::DoesPlatformSupportLumenGI()` also requires SM6, or the Vulkan-SM5/Metal-SM5 exceptions; ES3.1 is neither.

Degradation: if enabled in config on ES3.1, the renderer falls back to no Lumen GI/reflections; the project setting is ignored at runtime. It can run on `VULKAN_SM5_ANDROID`, but that is a different target and is much too heavy for Adreno 630.

### Config keys / code changes

In `source/frontends/unreal/Config/DefaultEngine.ini`:

```ini
[/Script/Engine.RendererSettings]
r.DynamicGlobalIlluminationMethod=1
r.ReflectionMethod=1
r.GenerateMeshDistanceFields=True
```

The project configures `PCD3D_SM6` on desktop with `PCD3D_SM5` as fallback. For the fork, `Config/Windows/WindowsEngine.ini` disables bindless:

```ini
[ShaderPlatformConfig PCD3D_SM6]
BindlessConfiguration=Disabled
bEnableNaniteBindlessShading=False
bEnableNaniteBindlessRasterization=False
```

The project defaults to stock Unreal Engine 5.8.4, which builds SM6 cleanly. Runtime selection is implemented in the game's Graphics settings page and via `-BallsGraphics` or `-BallsHighEnd` (Epic preset).

Optional runtime command for an already-SM6 build:

```text
-ExecCmds="r.DynamicGlobalIlluminationMethod 1; r.ReflectionMethod 1"
```

### Cost

- RTX 3070 Laptop, 1600x900: expect roughly +5-8 ms/frame for Lumen software GI + reflections in this simple scene, plus distance-field build/cook overhead. Likely still 60 fps if the base frame is under ~8 ms, but not free.
- POCO F1 (Adreno 630, Vulkan ES3.1): unavailable. Do not attempt on ES3.1.

### Verdict

Worth a desktop-only opt-in if the goal is to showcase UE5 GI/reflections. Not useful on the current mobile target.

## 2. Nanite

### Requirements

- Project setting: `r.Nanite.ProjectEnabled=1` (`bNanite=True` in `URendererSettings`). This is read-only at runtime and requires a restart/cook (enabled in `Config/DefaultEngine.ini`).
- Platform support: `DoesPlatformSupportNanite()` must be true (`bSupportsNanite` in the platform's data-driven ini).
- Assets: meshes must have Nanite enabled. The project includes `Content/Balls/NaniteCube.uasset` (the engine basic cube with Nanite enabled; recreated via `Scripts/MakeNaniteCube.py`).
- ISMs: Nanite works through instanced/static mesh components; `BallsBoardView.cpp` loads `NaniteCube.uasset` for the brick ISMs (falling back to `/Engine/BasicShapes/Cube` if absent).
- Scene fit: Nanite's value is high triangle counts and automatic LODs. This scene is ~200 low-poly cubes; Nanite adds fixed overhead with no visual benefit.

### Android (Vulkan ES3.1)

Not supported. `VULKAN_ES3_1_ANDROID` has no `bSupportsNanite=true`. It is supported on `VULKAN_SM5_ANDROID`, but that target is impractical for Adreno 630 and would increase shader/cook size.

### Config keys / code changes

```ini
[/Script/Engine.RendererSettings]
r.Nanite.ProjectEnabled=True
```

`Config/DefaultEngine.ini` targets `PCD3D_SM6` and sets `r.Nanite.ProjectEnabled=True`. `BallsBoardView.cpp` uses `NaniteCube.uasset`, and `BallsGraphics.cpp` controls `r.Nanite` at runtime based on user presets.

### Cost

- RTX 3070 Laptop: negligible to +1-2 ms for this triangle count, but no visible improvement.
- POCO F1: unavailable on ES3.1.

### Verdict

Not worth enabling in this project unless the goal is specifically to test Nanite on imported high-poly meshes. Keep disabled.

## 3. Virtual Shadow Maps

### Requirements

- Project setting: `r.Shadow.Virtual.ProjectEnabled=1` (read-only).
- Platform support: `DoesPlatformSupportVirtualShadowMaps()` requires both `DoesPlatformSupportNanite()` and, for non-Nanite objects, `r.Shadow.Virtual.NonNanite.ProjectEnabled=1`.
- Rendering path: deferred shading. VSM is not supported with forward shading.
- Lighting: a shadow-casting light. `BallsBoardView.cpp` dynamically sets `KeyLight->GetLightComponent()->SetCastShadows(Settings.Shadows != EBallsShadows::Off)`.
- Asset/mesh fit: VSM operates with `NaniteCube.uasset` when Nanite is enabled, or falls back to the non-Nanite VSM path.

### Android (Vulkan ES3.1)

Not supported. VSM support depends on Nanite platform support, and `VULKAN_ES3_1_ANDROID` does not support Nanite. `VULKAN_SM5_ANDROID` supports the Nanite/VSM path but is not a realistic target for POCO F1.

### Config keys / code changes

```ini
[/Script/Engine.RendererSettings]
r.Shadow.Virtual.Enable=1
r.Shadow.Virtual.ProjectEnabled=1
r.Shadow.Virtual.NonNanite.ProjectEnabled=1
```

Implemented in `Source/Balls/Private/BallsBoardView.cpp` (`ApplyGraphics`):

```cpp
KeyLight->GetLightComponent()->SetCastShadows(Settings.Shadows != EBallsShadows::Off);
```

The backdrop is directly behind the bricks, so shadows will add streaks; this is a visual tradeoff, not just a performance one.

### Cost

- RTX 3070 Laptop: +2-5 ms/frame if shadows are enabled and VSM is active; more if many shadow-casting instances update.
- POCO F1: unavailable on ES3.1.

### Verdict

Not recommended for this flat 2.5D scene. The current shadowless lighting is intentional and cheaper. Keep VSM off unless testing shadow quality.

## 4. TSR (Temporal Super-Resolution)

### Requirements

- Renderer setting: `r.AntiAliasingMethod=4` (`AAM_TSR`).
- Platform support: `SupportsTSR()` maps to `bSupportsGen5TemporalAA` in the data-driven platform ini.
- Inputs: TSR needs depth and velocity/motion vectors. Standard engine meshes generate these. The custom ball pass in `Source/BallsRendering/Private/BallsSceneViewExtension.cpp` draws after motion blur with `CF_Always` and no velocity output, so the balls themselves will not be temporally reconstructed or anti-aliased by TSR; brick/backdrop edges will.
- Interaction with DRS: TSR handles spatial upsampling, so it pairs well with dynamic resolution.

### Android (Vulkan ES3.1)

Not supported. `VULKAN_ES3_1_ANDROID` does not set `bSupportsGen5TemporalAA`, so it defaults to false. Android SM5 also does not set it in the provided ini. Mobile should remain on TAA/FXAA as configured by the mobile renderer.

### Config keys / code changes

```ini
[/Script/Engine.RendererSettings]
r.AntiAliasingMethod=4
```

Runtime toggle:

```text
-ExecCmds="r.AntiAliasingMethod 4"
```

Implemented: `-BallsHighEnd` and `-BallsGraphics` are parsed in `BallsApp.cpp`, and `BallsGraphics.cpp` applies `r.AntiAliasingMethod 4` for TSR under the High, Epic, and Cinematic presets.

### Cost

- RTX 3070 Laptop: roughly similar to TAA, about +1-2 ms/frame versus no temporal AA; better edge stability at 1600x900.
- POCO F1: unavailable on ES3.1.

### Verdict

Worth a desktop quality preset. It is the cheapest high-end visual upgrade and improves the look of brick edges and backdrop cubes.

## 5. Dynamic Resolution (DRS / screen percentage)

### Requirements

- Desktop:
  - RHI support: D3D12 sets `GRHISupportsDynamicResolution=true`.
  - `r.DynamicRes.OperationMode=2` (enable regardless of user settings) or `1` (respect user settings).
  - `r.DynamicRes.MinScreenPercentage`, `r.DynamicRes.MaxScreenPercentage`, and `r.DynamicRes.FrameTimeBudget` (ms).
  - Optional: `r.ScreenPercentage.Default` for the base scale.
- Android:
  - `FVulkanAndroidPlatform::SupportsDynamicResolution()` requires Swappy enabled at RHI init and `r.Android.SupportsDynamicResolution=1` in the device profile.
  - Timestamp queries are a separate capability (`r.Android.SupportsTimestampQueries`); on builds with Swappy, DRS depends on the dynamic-resolution cvar.
  - The POCO F1 matches `Android_Adreno6xx_Vulkan` in `BaseDeviceProfiles.ini`; that profile does not currently set either cvar.

### Android (Vulkan ES3.1)

Conditionally supported. The platform/RHI path exists, but the device profile must opt in. On Adreno 630, timestamp-query/swapchain timing support is not guaranteed, so DRS may be unavailable even when requested. If it works, it is one of the few high-end-ish features that can help mobile performance.

### Config keys / code changes

Desktop, in `Config/DefaultEngine.ini`:

```ini
[SystemSettings]
r.DynamicRes.OperationMode=2
r.DynamicRes.MinScreenPercentage=50
r.DynamicRes.MaxScreenPercentage=100
r.DynamicRes.FrameTimeBudget=16.67
```

Android, in a new or existing `Config/Android/AndroidEngine.ini` or a device-profile override:

```ini
[Android_Adreno6xx_Vulkan DeviceProfile]
+CVars=r.Android.SupportsDynamicResolution=1
+CVars=r.Android.SupportsTimestampQueries=1
```

Test timestamp queries first with `r.Android.SupportsTimestampQueries` before relying on DRS.

### Cost

- RTX 3070 Laptop: DRS overhead is small (<1 ms), but it can drop render resolution to maintain frame time. Use a 16.67 ms budget for 60 fps.
- POCO F1: if supported, DRS can protect frame rate during heavy frames; if timestamp queries are unsupported, it will not engage.

### Verdict

Recommended as an optional desktop and (carefully tested) mobile feature. It is the best tool for protecting frame rate without permanently lowering quality.

## Cross-cutting constraints

1. Custom RDG ball pass:
   - `Source/BallsRendering/Private/BallsSceneViewExtension.cpp` subscribes to `EPostProcessingPass::MotionBlur` and draws balls into scene color after motion blur.
   - TSR and Lumen do not see these balls as normal geometry; Lumen will not bounce light from them, and TSR will not temporally reconstruct them.
   - This is acceptable for the current impostor pass, but it means high-end features improve bricks/backdrop more than balls.

2. Runtime-spawned ISMs:
   - Lumen can use them, but distance-field generation and surface-cache updates are less predictable than for cooked static meshes.
   - Nanite and VSM use `Content/Balls/NaniteCube.uasset` for the brick mesh.

3. Fork SM6 configuration:
   - Stock UE 5.8.4 is the default engine and compiles SM6 shaders cleanly.
   - For the engine fork, `PCD3D_SM6` is enabled with bindless disabled in `Config/Windows/WindowsEngine.ini` and `r.AreShaderErrorsFatal=0` in `Config/DefaultEngine.ini` to bypass unused global shader errors.

4. Shadows:
   - Key light cast shadows is dynamically controlled by `BallsBoardView.cpp` based on graphics settings (`Settings.Shadows != EBallsShadows::Off`).
   - The backdrop sits directly behind the bricks, so enabling shadows adds streaks on higher presets.

## Recommended design

Use a two-tier runtime preset, not five independent toggles:

1. Safe default (all platforms):
   - Keep current settings: SM5 on desktop, Vulkan ES3.1 on Android, TAA, no Lumen, no Nanite, no VSM, DRS off.
   - This preserves the fork workaround and mobile compatibility.

2. Desktop quality preset (`-BallsHighEnd` or `-ExecCmds`):
   - Build/run with SM6 (stock engine recommended).
   - Enable TSR + DRS first.
   - Optionally enable Lumen GI + reflections.
   - Keep Nanite and VSM disabled unless specifically testing those features.

3. Mobile:
   - Keep all high-end features off.
   - Optionally test DRS only on Adreno 6xx/7xx/8xx Vulkan profiles after verifying timestamp-query support.

Recommended exploration priority for this scene:

1. TSR (cheapest, visible improvement).
2. DRS (performance safety net).
3. Lumen (visible GI/reflections, requires SM6 and distance fields).
4. VSM (only if shadows are desired; requires deferred + Nanite platform support).
5. Nanite (not useful until high-poly Nanite meshes are added).

## Implemented graphics system notes

- Parsed in `Source/Balls/Private/BallsApp.cpp` (`-BallsHighEnd` and `-BallsGraphics=<preset>`) and managed via `BallsGraphics.cpp` / `SBallsGraphicsPage.cpp`:
  - `r.AntiAliasingMethod` (TAA, TSR, etc.)
  - `r.DynamicRes.OperationMode` and `r.DynamicRes.FrameTimeBudget`
  - `r.DynamicGlobalIlluminationMethod` and `r.ReflectionMethod` (Lumen)
  - `r.Shadow.Virtual.Enable` and key light shadow casting
  - `r.Nanite`
- Project defaults in `Config/DefaultEngine.ini` enable `r.Nanite.ProjectEnabled=True` and `r.GenerateMeshDistanceFields=True` so runtime switches take effect without restarting.

## Source references

- `source/frontends/unreal/Config/DefaultEngine.ini`
- `source/frontends/unreal/Config/Windows/WindowsEngine.ini`
- `source/frontends/unreal/Source/Balls/Private/BallsBoardView.cpp`
- `source/frontends/unreal/Source/Balls/Private/BallsGraphics.cpp`
- `source/frontends/unreal/Source/Balls/Private/UI/SBallsGraphicsPage.cpp`
- `source/frontends/unreal/Source/BallsRendering/Private/BallsSceneViewExtension.cpp`
- `Engine/Config/Android/DataDrivenPlatformInfo.ini`
- `Engine/Config/Windows/DataDrivenPlatformInfo.ini`
- `Engine/Source/Runtime/RHI/Private/DataDrivenShaderPlatformInfo.cpp`
- `Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp`
- `Engine/Source/Runtime/Renderer/Private/Lumen/Lumen.cpp`
- `Engine/Source/Runtime/Engine/Classes/Engine/RendererSettings.h`
- `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h`
- `Engine/Source/Runtime/Engine/Private/DynamicResolution.cpp`
- `Engine/Source/Runtime/ApplicationCore/Private/Android/AndroidApplication.cpp`
- `Engine/Source/Runtime/VulkanRHI/Private/Android/VulkanAndroidPlatform.cpp`
- `Engine/Config/BaseDeviceProfiles.ini`