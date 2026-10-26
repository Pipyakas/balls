# Unreal Engine front end

A UE 5.8 front end for the same core as the raylib build. It is code-only: there are no `.uasset` files. The
game runs on the engine's empty `/Engine/Maps/Entry` map, and `ABallsGameMode` spawns everything else.

- **Board.** Bricks are instanced cubes (one instanced mesh per color) lit by a key light and a fill light. The
  camera looks straight at the board and is framed so the board fills the middle column at full height.
- **Balls.** A scene view extension (`Source/BallsRendering`) uploads the core's packed 32-bit ball snapshot each
  frame. It draws the balls as instanced sphere impostors after motion blur, so bloom and tonemapping apply to them
  (`Shaders/BallsDraw.usf`).
- **UI.** Slate, laid out in three columns: header and controls (launcher grid or performance controls) on the
  left, the board in the middle, and stats on the right. There are also menus, pause and win overlays, and toasts.
- **Same rules as raylib.** Esc/P pause, R restarts, F1/F2/F3 switch the physics backend, Space starts
  performance mode, and saving a level's `.txt` files hot-reloads it.

The map editor is not ported yet; use the raylib build for editing. Hot reload picks up the saved files.

## Build and run

Build with the engine fork at `D:\Unreal\UnrealEngine`. The `.uproject` pre-build step runs `BuildCore.bat`, which
builds `balls_core.lib` into `build-unreal/` with `/MD` and no LTO. For the CUDA backend, run
`pwsh tools/FetchCudaSdk.ps1` once first.

```powershell
D:\Unreal\UnrealEngine\Engine\Build\BatchFiles\Build.bat ColorBlastBallsEditor Win64 Development `
    -Project="$PWD\frontends\unreal\ColorBlastBalls.uproject" -WaitMutex
D:\Unreal\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe "$PWD\frontends\unreal\ColorBlastBalls.uproject" `
    -game -windowed -ResX=1600 -ResY=900 -BallsPlay=normal:DA_FirstLevel -BallsBackend=avx2
```

- `-BallsPlay=<normal|performance>:<level>` opens a level directly.
- `-BallsBackend=<scalar|avx2|cuda>` picks the starting physics backend.

## Engine fork workarounds

The fork's current DXC/SM6 path fails to compile many engine global shaders (bindless-by-default plus the WIP UEX
merge in `ShaderFormatD3D`). This project therefore:

- targets `PCD3D_SM5` (`Config/DefaultEngine.ini`);
- disables SM6 bindless (`Config/Windows/WindowsEngine.ini`).

Remove both once the fork compiles SM6 global shaders again.
