# Unreal Engine front end

A UE 5.8 front end for the same core as the raylib build. It is code-only: there are no `.uasset` files. The
game runs on the engine's empty `/Engine/Maps/Entry` map, and `ABallsGameMode` spawns everything else.

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
- **Same rules as raylib.** the Settings page switches the physics backend, the Start button starts
  performance mode, and saving a level's `.txt` files hot-reloads it.

## Build and run

Build with the engine fork at `D:\Unreal\UnrealEngine`. The `.uproject` pre-build step runs `BuildCore.bat`, which
builds `balls_core.lib` and the physics plugin DLLs (CPU, CUDA) into `build/win32-ue/` with `/MD` and no LTO. For
the CUDA backend, run `pwsh scripts/FetchCudaSdk.ps1` once first. The core loads the plugin DLLs at run time:
packaged builds stage them beside the executable, and the editor finds them in `build/win32-ue/bin/Release`.

```powershell
D:\Unreal\UnrealEngine\Engine\Build\BatchFiles\Build.bat ColorBlastBallsEditor Win64 Development `
    -Project="$PWD\source\frontends\unreal\ColorBlastBalls.uproject" -WaitMutex
D:\Unreal\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe "$PWD\source\frontends\unreal\ColorBlastBalls.uproject" `
    -game -windowed -ResX=1600 -ResY=900 -BallsPlay=normal:DA_FirstLevel -BallsBackend=avx2
```

- `-BallsPlay=<normal|performance>:<level>` opens a level directly.
- `-BallsBackend=<scalar|avx2|cuda|hybrid>` picks the starting physics backend.
- `-BallsEditor` opens the map editor instead of the main menu.
- `-BallsScreen=<settings|levels-normal|levels-performance|pause|pause-settings>` opens a screen directly (the pause
  ones need `-BallsPlay` too), for screenshots and conformance checks.
- `-BallsHighEnd` turns on the desktop quality preset: TSR anti-aliasing plus dynamic resolution
  (50-100% screen percentage, 60 fps budget). Lumen/Nanite/VSM need an SM6 build; see
  `data/docs/ue5-high-end-features.md`.

## Packaged build

`build-win32-ue.bat` at the repository root cooks and stages the Win64 game into `dist\win32-ue\` (set `UE_ROOT`
if the engine fork is not at `D:\Unreal\UnrealEngine`); `run-win32-ue.bat [game args]` builds and launches it.
The first run compiles the game target and takes about an hour; later runs are incremental. The script also copies
`data/assets` to `dist\win32-ue\ColorBlastBalls\assets`, where the packaged game looks for levels.

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
    -project="$PWD\source\frontends\unreal\ColorBlastBalls.uproject" -noP4 -platform=Android `
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

- targets `PCD3D_SM5` (`Config/DefaultEngine.ini`);
- disables SM6 bindless (`Config/Windows/WindowsEngine.ini`);
- passes `-Wno-macro-redefined` on Android (`Source/ColorBlastBalls.Target.cs`), because the fork's
  `Misc/Build.h` defaults its UEX/XFSDK macros to 0 while `Renderer`/`RenderCore`/`Engine` define them to 1;
  MSVC only warns, clang under `-Werror` fails;
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
