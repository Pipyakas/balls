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
        public int advancedSettings;
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
        public double estimatedBalls;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] result;

        public string Label => Read(label);

        public string Failure => Read(failure);

        /// <summary>The core's result text: balls at 60 FPS and ball-steps, failed: reason, or not available.</summary>
        public string Result => Read(result);

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

    /// <summary>Mirrors BallsEditorState in BallsUnityApi.h.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeEditorState
    {
        public int width;
        public int height;
        public int playableHeight;
        public int launcherColumns;
        public int launcherVisibleRows;
        public int launcherRows;
        public int dirty;
        public int canUndo;
        public int canRedo;
        public int tool;
        public int brushColor;
        public int brushHits;
        public int launcherBalls;
        public int selectedBrick;
        public int mode;
        public int playable;
        public int problemCount;
        public float messageSecondsLeft;
        public int messageIsError;
    }

    /// <summary>Mirrors BallsUiOption in BallsUnityApi.h.</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeUiOption
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] label;
        public int enabled;
        public int selected;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] detail;
        public int kind;

        public string Label => Read(label);
        public string Detail => Read(detail);

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

    /// <summary>Mirrors BallsDevicePickRow in BallsUnityApi.h.</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeDevicePickRow
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] device;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] label;
        public int optionCount;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 12)] public NativeUiOption[] options;

        public string Device => Read(device);
        public string Label => Read(label);

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

    /// <summary>Mirrors BallsDebugRow in BallsUnityApi.h.</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeDebugRow
    {
        public int kind;
        public int tone;
        public int colorIndex;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] label;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 192)] public byte[] value;

        public string Label => Read(label);
        public string Value => Read(value);

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

    /// <summary>Mirrors BallsControlContext: what the core's control API reads for one request.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeControlContext
    {
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string renderer;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string screen;
        public int windowWidth;
        public int windowHeight;
        public int preferredBackend;
        public int vsync;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string toast;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string extraFields;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string screenDetails;
        public IntPtr frames;
        public NativeSettings settings;
        public IntPtr session;
        public int mode;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string levelName;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string loadError;
        public int diskState;
        public int paused;
        public int probeTargetFps;
        public IntPtr benchmark;
        public IntPtr profile;
    }

    /// <summary>Mirrors BallsControlAction: something a control request asks the front end to do.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeControlAction
    {
        public int kind;
        public int mode;
        public int value;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 1024)] public byte[] text;
        public NativeSettings settings;

        public string Text
        {
            get
            {
                if (text == null)
                {
                    return string.Empty;
                }
                int length = Array.IndexOf(text, (byte)0);
                return System.Text.Encoding.UTF8.GetString(text, 0, length < 0 ? text.Length : length);
            }
        }
    }

    /// <summary>Mirrors BallsFlowState: live state of the core's screen flow.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeFlowState
    {
        public int screen;
        public int overlay;
        public int mode;
        public int paused;
        public int diskState;
        public int canRunMachineBenchmark;
        public int benchmarkSettled;
        public int buttonCount;
        public int levelCount;
        public int hasLevel;
        public float toastSecondsLeft;
        public int toastIsError;
    }

    /// <summary>Mirrors BallsFlowButton: one button the screen or overlay offers.</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeFlowButton
    {
        public int action;
        public int enabled;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] label;

        public string Label
        {
            get
            {
                if (label == null)
                {
                    return string.Empty;
                }
                int length = Array.IndexOf(label, (byte)0);
                return System.Text.Encoding.UTF8.GetString(label, 0, length < 0 ? label.Length : length);
            }
        }
    }

    /// <summary>Mirrors BallsFlowLevelRow: one level entry in Level select.</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeFlowLevelRow
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] name;
        public int highlighted;

        public string Name
        {
            get
            {
                if (name == null)
                {
                    return string.Empty;
                }
                int length = Array.IndexOf(name, (byte)0);
                return System.Text.Encoding.UTF8.GetString(name, 0, length < 0 ? name.Length : length);
            }
        }
    }

    /// <summary>Mirrors BallsFlowRequest: work the screen flow asks the front end to do.</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct NativeFlowRequest
    {
        public int kind;
        public int mode;
        public int hasLevel;
        public NativeLevelInfo level;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)] public byte[] folder;

        public string Folder
        {
            get
            {
                if (folder == null)
                {
                    return string.Empty;
                }
                int length = Array.IndexOf(folder, (byte)0);
                return System.Text.Encoding.UTF8.GetString(folder, 0, length < 0 ? folder.Length : length);
            }
        }
    }
}
