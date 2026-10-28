using System;
using System.Runtime.InteropServices;

namespace Balls.Native
{
    // Layouts mirror source/plugin/BallsUnityApi.h one to one. Change both files together.

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeSnapshot
    {
        public IntPtr packed;
        public ulong count;
        public float fieldWidth;
        public float fieldHeight;
        public int residentInRenderBuffer;
        public int visibilityGrid;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeBoardInfo
    {
        public int columns;
        public int rows;
        public int playableHeight;
        public int paddedWidth;
        public int paddedCellCount;
        public int brickCount;
        public int aliveBrickCount;
        public ulong revision;
        public int launcherColumns;
        public int launcherVisibleRows;
        public int launcherRemainingBalls;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeBoardView
    {
        public IntPtr paddedCells;
        public IntPtr brickColors;
        public IntPtr brickHitsLeft;
        public IntPtr brickRiseOffset;
        public IntPtr brickHitFlash;
        public IntPtr aliveBricks;
        public IntPtr changedCells;
        public int changedCellCount;
        public int brickCount;
        public int aliveBrickCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeStats
    {
        public ulong ballCount;
        public ulong liveBallCount;
        public ulong ballsByColor0;
        public ulong ballsByColor1;
        public ulong ballsByColor2;
        public ulong ballsByColor3;
        public ulong ballsByColor4;
        public ulong ballsByColor5;
        public ulong ballsByColor6;
        public ulong totalBallsSpawned;
        public ulong totalHits;
        public double lastFramePhysicsMs;
        public int ticksLastFrame;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeLauncher
    {
        public int present;
        public int color;
        public int ballCount;
        public float riseOffset;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeProbeInfo
    {
        public int active;
        public int phase;
        public double lastFps;
        public double targetFps;
        public ulong bestPassingCount;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeLevelInfo
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] name;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)] public byte[] bricksPath;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)] public byte[] launchersPath;

        public string Name => Read(name);

        public string BricksPath => Read(bricksPath);

        public string LaunchersPath => Read(launchersPath);

        private static string Read(byte[] bytes)
        {
            if (bytes == null)
            {
                return string.Empty;
            }
            int length = Array.IndexOf(bytes, (byte)0);
            return System.Text.Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
        }
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeSettings
    {
        public int ballCollisions;
        public int orientation;
        public int vsync;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] defaultBackend;

        public string DefaultBackend
        {
            get
            {
                if (defaultBackend == null)
                {
                    return string.Empty;
                }
                int length = Array.IndexOf(defaultBackend, (byte)0);
                return System.Text.Encoding.UTF8.GetString(defaultBackend, 0, length < 0 ? defaultBackend.Length : length);
            }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeValidation
    {
        public ulong brickHits0, brickHits1, brickHits2, brickHits3, brickHits4, brickHits5, brickHits6;
        public ulong balls0, balls1, balls2, balls3, balls4, balls5, balls6;
        public int problemCount;
        public int playable;
    }


    /// <summary>The game's gameplay layout in layout units; see balls_compute_layout.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeLayout
    {
        public float headerX, headerY, headerW, headerH;
        public float mainX, mainY, mainW, mainH;
        public float interactionX, interactionY, interactionW, interactionH;
        public float debugX, debugY, debugW, debugH;
        public float boardOriginX, boardOriginY;
        public float boardX, boardY, boardW, boardH;
        public float cellSize;
        public int portrait;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeBenchmarkRow
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] label;
        public int kind;
        public int status;
        public double ballStepsPerSecond;
        public double progress;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] failure;

        public string Label => Read(label);

        public string Failure => Read(failure);

        private static string Read(byte[] bytes)
        {
            if (bytes == null)
            {
                return string.Empty;
            }
            int length = Array.IndexOf(bytes, (byte)0);
            return System.Text.Encoding.UTF8.GetString(bytes, 0, length < 0 ? bytes.Length : length);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeEditorInfo
    {
        public int width;
        public int height;
        public int playableHeight;
        public int launcherColumns;
        public int launcherVisibleRows;
        public int dirty;
        public int canUndo;
        public int canRedo;
        public int brickCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeFrameStats
    {
        public double framesPerSecond;
        public double worstFpsLastSecond;
        public double workFramesPerSecond;
        public double worstWorkFpsLastSecond;
        public double frameMs;
        public double workMs;
        public double gpuMs;
    }
}
