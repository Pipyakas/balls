# Unreal Engine front end

A UE 5.8 front end for the same core as the SDL build. It has one asset (`Content/Balls/NaniteCube.uasset` for Nanite
bricks); the game runs on the engine's empty `/Engine/Maps/Entry` map, and `ABallsGameMode` spawns everything else.

- **Board.** Bricks are instanced cubes (one instanced mesh per color) lit by a key light and a fill light. The
  camera looks straight at the board and is framed so the board fills the middle column at full height.
- **Balls.** A scene view extension (`Source/BallsRendering`) uploads the core's packed 32-bit ball snapshot each
  frame. It draws the balls as instanced sphere impostors after motion blur, so bloom and tonemapping apply to them
  (`Shaders/BallsDraw.usf`).
- **UI.** Slate, laid out in three columns: header and controls (launcher grid or performance controls) on the
  left, the board in the middle, and stats on the right. On a portrait viewport it switches to the original
  mobile layout: stats strip on top, board in the middle, controls along the bottom. There are also menus,
  pause and win overlays, and toasts.
- **Map editor.** Paint/extend/erase/pick tools, brush color and hit counts, launcher balls, undo/redo, and
  save/save-and-play through the same `LevelEditor` core the other front ends use.
- **Same rules as the design.** The Settings page switches the physics backend, the Start button starts
  performance mode, and saving a level's `.txt` files hot-reloads it.

## Build and run

Build with a stock Unreal Engine 5.8 install (5.8.4 from the Epic launcher, at `D:\Unreal\UE_5.8` on the dev
PCs). The engine fork at `D:\Unreal\UnrealEngine` still works but is no longer the default: as a source engine it
compiles the whole engine into every worktree's game target (about 1,100 actions and 14-21 GB each), where a stock
install links it prebuilt. The Target.cs files set `WindowsPlatform.CompilerVersion = "Latest"` so the game builds with
the newest MSVC installed to match `balls_core.lib`. The `.uproject` pre-build step runs `BuildCore.bat`, which
builds `balls_core.lib` and the physics plugin DLLs (CPU, CUDA) into `build/win32-ue/` with `/MD` and no LTO. For
the CUDA backend, run `pwsh scripts/FetchCudaSdk.ps1` once first. The core loads the plugin DLLs at run time:
packaged builds stage them beside the executable, and the editor finds them in `build/win32-ue/bin/Release`.

```powershell
D:\Unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat BallsEditor Win64 Development `
    -Project="$PWD\source\frontends\unreal\Balls.uproject" -WaitMutex
D:\Unreal\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "$PWD\source\frontends\unreal\Balls.uproject" `
    -game -windowed -ResX=1600 -ResY=900 -BallsPlay=normal:DA_FirstLevel -BallsBackend=avx2
```

- `-BallsPlay=<normal|performance>:<level>` opens a level directly.
- `-BallsBackend=<scalar|avx2|cuda|hybrid>` picks the starting physics backend.
- `-BallsEditor` opens the map editor instead of the main menu.
- `-BallsScreen=<settings|levels-normal|levels-performance|pause|pause-settings>` opens a screen directly (the pause
  ones need `-BallsPlay` too), for screenshots and conformance checks.
- `-BallsGraphics=<low|medium|high|epic|cinematic>` starts at that graphics preset without saving it; the same
  switch takes the saved `key=value;...` form (`-BallsGraphics="preset=epic;aa=taau;screen=67"`). `-BallsHighEnd`
  is the Epic preset.

## Graphics page

The Unreal port's own settings page (the design has none), a popup opened from Settings > Graphics settings or from
the Graphics button in the game's Advanced Settings, so every change shows on the running board:

- **Resolution**: screen resolution 50-100%; dynamic resolution (on by default, drops to 50% to hold the target
  FPS); target FPS 1 to the display's highest refresh rate (default 60). These settings are kept across presets.
- **Preset**: Low, Medium, High, Epic, Cinematic. Low is the flat look with nothing optional (the port's default);
  Medium and High add TAA/TSR, screen-space reflections and shadow maps; Epic and Cinematic turn on Lumen GI and
  reflections, virtual shadow maps and Nanite. Changing any row below makes the settings Custom.
- **Anti-aliasing**: Off, FXAA, TAA, TAAU, TSR or FSR (only with AMD's FidelityFX FSR plugin installed in the
  engine).
- **UE5 features**: global illumination (Lumen), reflections (screen space or Lumen), shadows (shadow maps or
  virtual), Nanite. A feature the GPU or shader model cannot run is disabled with the reason.
- **Scalability**: the engine's ten `sg.*` groups, each Low to Cinematic.

The settings are saved per user (`GameUserSettings.ini`, `[Balls] GraphicsSettings`), and the control API's
`/graphics` takes the same keys: `preset`, `sg.<group>`, `gi`, `reflectionmethod`, `shadowmethod`, `nanite`, `aa`,
`screen`, `dynamic`, `fps` (for example `/graphics?preset=epic&aa=taau&screen=67&fps=120`).

Windows builds cook shaders for SM6, which Lumen, Nanite and virtual shadow maps need, and SM5 as the fallback for
older GPUs and the engine fork. `Content/Balls/NaniteCube.uasset`, the project's only asset, is the engine cube with
Nanite on; `Scripts/MakeNaniteCube.py` recreates it. On the dev PC (i5-12400F, GTX 1650, 50M balls) the presets cost
0.8, 1.8, 3.8, 9.0 and 23.7 ms of GPU time from Low to Cinematic.

## Packaged build

`source\frontends\unreal\build.bat` cooks, stages and archives the game; the root `build.bat` reaches it as
`build.bat unreal win64` (or `build.bat --menu`). Variants:

- `win64` stages `dist\win32-ue\Balls.exe`.
- `linux` stage `dist\linux-ue\` for x86_64 Linux, cross-compiled on Windows with Unreal's own toolchain
  (`LINUX_MULTIARCH_ROOT`, default `D:\UnrealToolchains\v26_clang-20.1.8-rockylinux8`). UnrealBuildTool skips the
  `.uproject` pre-build steps for a cross-compiled target, so the script builds the core and the plugins through
  `BuildCoreLinux.bat` itself.

Every build starts with `setup.ps1` (`build.bat --setup [variant]` runs only that). Without `UE_ROOT` it takes a
stock 5.8 install first (`FindUnrealEngine.ps1`: the launcher's own install record, its registry entry, or
`UE_5.8`, `Unreal\UE_5.8` or `Program Files\Epic Games\UE_5.8` on any drive), then a source build (the launcher's
registered builds, then the fork at `D:\Unreal\UnrealEngine`), then any `UE5*`/`UnrealEngine*` folder up to two
levels below a drive's root. When it still finds none it asks: give the engine's folder (kept as the user variable
`UE_ROOT` if you agree), or install it, which installs the Epic Games Launcher with winget if needed and opens it on
the engine library. The `linux` variant also needs the launcher's Linux target platform option on a stock engine
(offered the same way) and the cross toolchain, which it finds from `LINUX_MULTIARCH_ROOT` or the usual
`UnrealToolchains` folders, or downloads Epic's installer for. The script also copies `data/assets` to the packaged game's
`Balls\assets`, where it looks for levels, and the physics plugins stage next to the executable.

The fork lists two prebuilt shaders as staged runtime dependencies that its tree does not contain, and staging
aborts without them. Create empty placeholders (nothing here uses them):

```powershell
ni D:\Unreal\UnrealEngine\Engine\Shaders\Private\WindField\WindFieldWorker.dxil
ni D:\Unreal\UnrealEngine\Engine\Plugins\FX\Niagara\Shaders\Private\MultiAdapter\NiagaraMultiAdapterParticlesWorker.dxil
```

## Android

Parked: the SDL front end is the Android path (see the repository README). The notes below describe what was built.

The Android build uses Vulkan ES3.1 and landscape/portrait adapts at runtime. The `Android` pre-build step runs
`BuildCoreAndroid.bat`, which builds `libballs_core.a` with the NDK (arm64) and copies the level files into
`source/frontends/unreal/Content/assets` (project-level non-UFS staging paths are Content-relative). Because balls_core
reads levels with `std::filesystem`, the game copies the packaged `assets` to the app's saved directory on first
run.

```powershell
D:\Unreal\UnrealEngine\Engine\Build\BatchFiles\RunUAT.bat BuildCookRun `
    -project="$PWD\source\frontends\unreal\Balls.uproject" -noP4 -platform=Android `
    -clientconfig=Development -build -cook -package -stage -deploy -device=<serial> `
    -architecture=arm64 -cookflavor=ASTC -map=/Engine/Maps/Entry -utf8output -unattended
```

Requires JDK 17, Android SDK 36 with NDK 27.2.12479018, and the SDK's Ninja (the SDK CMake 3.22 is too old for
the core, so `BuildCoreAndroid.bat` uses the system CMake with the SDK Ninja).

The non-UFS level files are packaged into an OBB. `BuildCookRun -deploy` installs the APK but does not push the
OBB, so after deploying push it once (and again after a recook that changes it):

```powershell
adb push Binaries/Android/main.1.com.colorblastballs.game.obb /sdcard/Android/obb/com.colorblastballs.game/
```

## Engine fork workarounds

The fork's current DXC/SM6 path fails to compile many engine global shaders (bindless-by-default plus the WIP UEX
merge in `ShaderFormatD3D`). This project therefore:

- targets `PCD3D_SM6` with `PCD3D_SM5` fallback and `r.AreShaderErrorsFatal=0` (`Config/DefaultEngine.ini`);
- disables SM6 bindless (`Config/Windows/WindowsEngine.ini`);
- passes `-Wno-macro-redefined -Wno-logical-op-parentheses` on Android and Linux (`Source/Balls.Target.cs`), because the fork's
  `Misc/Build.h` defaults its UEX/XFSDK macros to 0 while `Renderer`/`RenderCore`/`Engine` define them to 1;
  MSVC only warns, clang under `-Werror` fails;
- sets `WindowsPlatform.CompilerVersion = "Latest"` (`Source/Balls.Target.cs`, `Source/BallsEditor.Target.cs`) so Win64 builds match the core's STL;
- drops the XFSDK "Callisto" skin-shading includes from `Engine/Shaders/Private/ShadingModels.ush` and
  `PostProcessSubsurfaceCommon.ush` (and the matching define in `Engine.Build.cs`), because this engine tree
  carries no `XFSDK_Shader_Works` plugin and the cook otherwise dies with "Can't map virtual shader source
  path".

Remove both once the fork compiles SM6 global shaders again.

## Stock UE 5.8.4

The front end does not require the fork. The fork-only `XThirdParty` plugins (`TBImGui`, `XDash`,
`UE5Coro`, `Box3DUnreal`, `FlatNodes`, `Flow`, `WcwMemoryMonitorModule`) are listed as disabled and
optional in the `.uproject`, so a stock 5.8.4 engine ignores the missing optional plugins. The
fork-specific `ShaderPlatformConfig` keys in `Config/Windows/WindowsEngine.ini` and the `TBImGui`
settings in `Config/DefaultGame.ini` are ignored by a stock engine, and the workarounds above are
harmless there. The source only uses stock 5.8 APIs (scene view extension, instanced-mesh bounds,
camera aspect, Slate), and the core build scripts are engine-independent.

Build against a stock 5.8.4 engine with the same `Build.bat`/UBT commands and `BuildCore.bat`; the
fork's SM5/DXC workarounds can be removed once the stock engine compiles the globals.
