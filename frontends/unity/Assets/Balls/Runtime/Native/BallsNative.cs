using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Balls.Native
{
    /// <summary>
    /// P/Invoke surface of balls_unity.dll, the flat C facade over the C++ core. Every pointer returned here is
    /// core-owned and stays valid until the next call that mutates the same session; callers copy what they need
    /// inside the frame.
    /// </summary>
    public static class BallsNative
    {
        public const string Library = "balls_unity";

        public const int AbiVersion = 2;

        public const int ModeNormal = 0;
        public const int ModePerformance = 1;

        public const int BackendScalar = 0;
        public const int BackendAvx2 = 1;
        public const int BackendCuda = 2;
        public const int BackendHybrid = 3;
        public const int BackendVulkan = 4;
        public const int BackendCount = 5;

        public const int StateReady = 0;
        public const int StateRunning = 1;
        public const int StateWon = 2;

        public const int ColorCount = 7;

        public const int DiskUnchanged = 0;
        public const int DiskChanged = 1;
        public const int DiskRemoved = 2;

        public const uint RenderCoordinateMask = 0x3FFFu;
        public const int RenderCoordinateBits = 14;
        public const int RenderColorShift = 28;

        [DllImport(Library)] public static extern int balls_abi_version();

        [DllImport(Library)] public static extern void balls_set_plugin_directory([MarshalAs(UnmanagedType.LPUTF8Str)] string directory);

        [DllImport(Library)] public static extern int balls_last_error(byte[] buffer, int size);

        [DllImport(Library)] public static extern float balls_render_pack_scale(float fieldExtent);

        [DllImport(Library)] public static extern void balls_compute_layout(float frameWidth, float frameHeight, int boardColumns, int boardRows, int orientation, int launcherRows, out NativeLayout layout);

        [DllImport(Library)] public static extern int balls_backend_available(int kind);

        [DllImport(Library)] public static extern int balls_backend_name(int kind, byte[] buffer, int size);

        [DllImport(Library)] public static extern int balls_backend_reason(int kind, byte[] buffer, int size);

        [DllImport(Library)] public static extern int balls_backend_detail(int kind, byte[] buffer, int size);

        [DllImport(Library)] public static extern int balls_fastest_cpu_backend();

        [StructLayout(LayoutKind.Sequential)]
        internal struct DeviceRecord
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string id;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string name;
            public int type;
            public ulong memoryBytes;
            public int vulkanDevice;
            public int cudaDevice;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ImportRecord
        {
            public int status;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string name;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string message;
            public int hasWarning;
        }

        [DllImport(Library)] private static extern int balls_devices_find(IntPtr devices, int capacity);

        [DllImport(Library)] private static extern int balls_levels_import(int mode, string destination,
            IntPtr[] picked, int pickedCount, [Out] ImportRecord[] results, int capacity, byte[] summary, int summarySize);

        /// <summary>What happened to one level pair the importer was given.</summary>
        public readonly struct LevelImport
        {
            public readonly int status;
            public readonly string name;
            public readonly string message;
            public readonly bool hasWarning;

            internal LevelImport(ImportRecord record)
            {
                status = record.status;
                name = record.name;
                message = record.message;
                hasWarning = record.hasWarning != 0;
            }

            public bool Failed => status == 2;
        }

        /// <summary>
        /// Copies level pairs into <paramref name="destination"/>; each pick may be either file of a pair or a folder of
        /// pairs. Returns the toast-ready summary and every pair's outcome, plus the level to highlight in the list.
        /// </summary>
        public static bool ImportLevels(int mode, string destination, IReadOnlyList<string> picked, out List<LevelImport> results,
            out string summary, out string highlight)
        {
            results = new List<LevelImport>();
            summary = string.Empty;
            highlight = null;
            if (picked.Count == 0)
            {
                summary = "Import failed: no files";
                return false;
            }
            var pointers = new IntPtr[picked.Count];
            int capacity = Math.Max(8, picked.Count * 8);
            var records = new ImportRecord[capacity];
            var text = new byte[512];
            try
            {
                for (int index = 0; index < picked.Count; ++index)
                {
                    pointers[index] = Marshal.StringToCoTaskMemUTF8(picked[index]);
                }
                int count = balls_levels_import(mode, destination, pointers, picked.Count, records, capacity, text, text.Length);
                for (int index = 0; index < Math.Min(count, capacity); ++index)
                {
                    results.Add(new LevelImport(records[index]));
                }
                int end = Array.IndexOf(text, (byte)0);
                summary = System.Text.Encoding.UTF8.GetString(text, 0, end < 0 ? text.Length : end);
            }
            finally
            {
                foreach (IntPtr pointer in pointers)
                {
                    if (pointer != IntPtr.Zero)
                    {
                        Marshal.FreeCoTaskMem(pointer);
                    }
                }
            }
            for (int index = results.Count - 1; index >= 0; --index)
            {
                if (!results[index].Failed)
                {
                    highlight = results[index].name;
                    break;
                }
            }
            return results.Count > 0 && results.Exists(result => !result.Failed);
        }

        [DllImport(Library)] private static extern int balls_compute_gpu(byte[] buffer, int size);
        [DllImport(Library)] private static extern int balls_set_compute_gpu(string preference);

        /// <summary>One device the physics can run on: the CPU, or a GPU the CUDA or Vulkan plugin listed.</summary>
        public readonly struct PhysicsDevice
        {
            public readonly string id;
            public readonly string name;
            public readonly int type;
            public readonly ulong memoryBytes;
            public readonly bool vulkan;

            internal PhysicsDevice(DeviceRecord record)
            {
                id = record.id;
                name = record.name;
                type = record.type;
                memoryBytes = record.memoryBytes;
                vulkan = record.vulkanDevice >= 0;
            }
        }

        private static List<PhysicsDevice> devices;

        /// <summary>
        /// Every device the plugins report, the CPU first. Enumerated once: the first call opens the GPU drivers.
        /// </summary>
        public static IReadOnlyList<PhysicsDevice> Devices => devices ??= ReadDevices();

        private static List<PhysicsDevice> ReadDevices()
        {
            var found = new List<PhysicsDevice>();
            int count = balls_devices_find(IntPtr.Zero, 0);
            if (count <= 0)
            {
                return found;
            }
            int stride = Marshal.SizeOf<DeviceRecord>();
            IntPtr buffer = Marshal.AllocHGlobal(stride * count);
            try
            {
                int written = balls_devices_find(buffer, count);
                for (int index = 0; index < written; ++index)
                {
                    found.Add(new PhysicsDevice(Marshal.PtrToStructure<DeviceRecord>(buffer + index * stride)));
                }
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
            return found;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct PickRecord
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string device;
            public int kind;
        }

        [DllImport(Library)] private static extern int balls_picks_display(int runningKind, [Out] PickRecord[] picks,
            int capacity);

        [DllImport(Library)] private static extern int balls_picks_set(PickRecord[] picks, int count, byte[] problem,
            int problemSize);

        [DllImport(Library)] private static extern int balls_pickable_kinds(string device, [Out] int[] kinds, int capacity);

        /// <summary>One device and the backend that runs on it; two or more picks run the composer.</summary>
        public readonly struct Pick
        {
            public readonly string device;
            public readonly int kind;

            internal Pick(PickRecord record)
            {
                device = record.device;
                kind = record.kind;
            }

            public Pick(string deviceId, int backendKind)
            {
                this.device = deviceId;
                kind = backendKind;
            }
        }

        /// <summary>What the device rows show for the backend that is about to run: its picks, or the default ones.</summary>
        public static IReadOnlyList<Pick> DisplayPicks(int runningKind)
        {
            var found = new List<Pick>();
            var records = new PickRecord[16];
            int count = balls_picks_display(runningKind, records, records.Length);
            for (int index = 0; index < Math.Min(count, records.Length); ++index)
            {
                found.Add(new Pick(records[index]));
            }
            return found;
        }

        /// <summary>Replaces the picks; false leaves the old ones and gives the core's reason.</summary>
        public static bool SetPicks(IReadOnlyList<Pick> picks, out string problem)
        {
            var records = new PickRecord[picks.Count];
            for (int index = 0; index < picks.Count; ++index)
            {
                records[index] = new PickRecord { device = picks[index].device, kind = picks[index].kind };
            }
            var text = new byte[256];
            int ok = balls_picks_set(records, records.Length, text, text.Length);
            int end = Array.IndexOf(text, (byte)0);
            problem = ok != 0 ? string.Empty : System.Text.Encoding.UTF8.GetString(text, 0, end < 0 ? text.Length : end);
            return ok != 0;
        }

        /// <summary>The backends a device row offers, fastest first.</summary>
        public static int[] PickableKinds(string device)
        {
            var kinds = new int[BallsNative.BackendCount];
            int count = balls_pickable_kinds(device, kinds, kinds.Length);
            var result = new int[Math.Max(count, 0)];
            Array.Copy(kinds, result, result.Length);
            return result;
        }

        /// <summary>The adapter the Vulkan physics plugin runs on: "auto", an index or a name substring.</summary>
        public static string ComputeGpu
        {
            get
            {
                var buffer = new byte[128];
                int length = balls_compute_gpu(buffer, buffer.Length);
                return length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : "auto";
            }
        }

        /// <summary>Returns false when the core did not match the preference, which leaves the old one in place.</summary>
        public static bool SetComputeGpu(string preference) => balls_set_compute_gpu(preference) != 0;

        [DllImport(Library)] public static extern int balls_levels_find([MarshalAs(UnmanagedType.LPUTF8Str)] string directory, [Out] NativeLevelInfo[] levels, int capacity);

        [DllImport(Library)] public static extern int balls_settings_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path, out NativeSettings settings);

        [DllImport(Library)] public static extern int balls_settings_save([MarshalAs(UnmanagedType.LPUTF8Str)] string path, ref NativeSettings settings);

        [DllImport(Library)] public static extern IntPtr balls_session_create(int mode, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelDirectory, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelName, int backend);

        [DllImport(Library)] public static extern void balls_session_destroy(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_load_level(IntPtr session, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelDirectory, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelName);

        [DllImport(Library)] public static extern int balls_session_restart(IntPtr session);

        [DllImport(Library)] public static extern void balls_session_update(IntPtr session, float deltaSeconds);

        [DllImport(Library)] public static extern void balls_session_report_frame(IntPtr session, double frameSeconds, double workSeconds);

        [DllImport(Library)] public static extern int balls_session_mode(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_state(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_backend(IntPtr session);

        [DllImport(Library)] public static extern double balls_session_elapsed_seconds(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_choose_launcher(IntPtr session, int column, int row);

        [DllImport(Library)] public static extern void balls_session_start_auto_release(IntPtr session);

        [DllImport(Library)] public static extern void balls_session_select_spawn_rate(IntPtr session, int optionIndex);

        [DllImport(Library)] public static extern void balls_session_select_ball_multiplier(IntPtr session, int optionIndex);

        [DllImport(Library)] public static extern void balls_session_select_ball_limit(IntPtr session, int optionIndex);

        [DllImport(Library)] public static extern int balls_session_spawn_rate_index(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_ball_multiplier_index(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_ball_limit_index(IntPtr session);

        [DllImport(Library)] public static extern ulong balls_session_ball_limit(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_auto_releasing(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_switch_backend(IntPtr session, int kind);
        [DllImport(Library)] public static extern int balls_session_pending_backend(IntPtr session);
        [DllImport(Library)] public static extern int balls_session_take_switch_outcome(IntPtr session, out int kind);

        [DllImport(Library)] public static extern void balls_session_start_capacity_probe(IntPtr session, double targetFps);

        [DllImport(Library)] public static extern void balls_session_cancel_capacity_probe(IntPtr session);

        [DllImport(Library)] public static extern void balls_session_probe(IntPtr session, out NativeProbeInfo probe);

        [DllImport(Library)] public static extern int balls_session_has_machine_ball_limit(IntPtr session);

        [DllImport(Library)] public static extern ulong balls_session_machine_ball_limit(IntPtr session);

        [DllImport(Library)] public static extern void balls_session_use_machine_ball_limit(IntPtr session, ulong limit);

        [DllImport(Library)] public static extern void balls_session_fill_with_balls(IntPtr session, ulong count);

        [DllImport(Library)] public static extern void balls_session_set_visibility_grid(IntPtr session, int width, int height);

        [DllImport(Library)] public static extern void balls_set_compute_gpu_preference([MarshalAs(UnmanagedType.LPUTF8Str)] string preference);

        [DllImport(Library)] public static extern int balls_compute_gpu_preference(byte[] buffer, int size);

        [DllImport(Library)] public static extern int balls_editor_brick_at(IntPtr editor, int column, int row);

        [DllImport(Library)] public static extern int balls_editor_brick_color(IntPtr editor, int column, int row);

        [DllImport(Library)] public static extern int balls_editor_brick_hits(IntPtr editor, int column, int row);

        [DllImport(Library)] public static extern int balls_session_set_ball_collisions(IntPtr session, int enabled);

        [DllImport(Library)] public static extern int balls_ball_collisions_message(int change, byte[] buffer, int size);

        [DllImport(Library)] public static extern int balls_session_ball_collisions(IntPtr session);

        [DllImport(Library)] public static extern int balls_session_launch_balls(IntPtr session, int color, ulong count);

        [DllImport(Library)] public static extern int balls_session_spawn_ball(IntPtr session, float x, float y, float vx, float vy, int color);

        [DllImport(Library)] public static extern void balls_session_tick(IntPtr session, int ticks);

        [DllImport(Library)] public static extern int balls_session_last_backend_fault(IntPtr session, byte[] buffer, int size);

        [DllImport(Library)] public static extern void balls_session_board_info(IntPtr session, out NativeBoardInfo info);

        [DllImport(Library)] public static extern void balls_session_board_view(IntPtr session, out NativeBoardView view);

        [DllImport(Library)] public static extern void balls_session_clear_changed_cells(IntPtr session);

        [DllImport(Library)] public static extern void balls_session_stats(IntPtr session, out NativeStats stats);

        [DllImport(Library)] public static extern int balls_session_snapshot(IntPtr session, out NativeSnapshot snapshot);

        [DllImport(Library)] public static extern int balls_session_launcher(IntPtr session, int column, int row, out NativeLauncher launcher);

        [DllImport(Library)] public static extern ulong balls_session_remaining_balls_by_color(IntPtr session, int color);

        [DllImport(Library)] public static extern IntPtr balls_benchmark_create([MarshalAs(UnmanagedType.LPUTF8Str)] string levelDirectory, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelName);

        [DllImport(Library)] public static extern void balls_benchmark_destroy(IntPtr benchmark);

        [DllImport(Library)] public static extern void balls_benchmark_start(IntPtr benchmark);

        [DllImport(Library)] public static extern void balls_benchmark_cancel(IntPtr benchmark);

        [DllImport(Library)] public static extern int balls_benchmark_finished(IntPtr benchmark);

        [DllImport(Library)] public static extern int balls_benchmark_rows(IntPtr benchmark, [Out] NativeBenchmarkRow[] rows, int capacity);

        [DllImport(Library)] public static extern int balls_benchmark_winner(IntPtr benchmark);

        [DllImport(Library)] public static extern IntPtr balls_level_watcher_create([MarshalAs(UnmanagedType.LPUTF8Str)] string levelDirectory, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelName);

        [DllImport(Library)] public static extern void balls_level_watcher_destroy(IntPtr watcher);

        [DllImport(Library)] public static extern int balls_level_watcher_poll(IntPtr watcher);

        [DllImport(Library)] public static extern void balls_level_watcher_mark_loaded(IntPtr watcher);

        [DllImport(Library)] public static extern int balls_level_disk_notice(int state, byte[] buffer, int size);

        [DllImport(Library)] public static extern IntPtr balls_frame_timings_create();

        [DllImport(Library)] public static extern void balls_frame_timings_destroy(IntPtr timings);

        [DllImport(Library)] public static extern void balls_frame_timings_record(IntPtr timings, double frameMs, double workMs, double gpuMs);

        [DllImport(Library)] public static extern void balls_frame_timings_stats(IntPtr timings, out NativeFrameStats stats);

        [DllImport(Library)] public static extern IntPtr balls_profile_open([MarshalAs(UnmanagedType.LPUTF8Str)] string directory);

        [DllImport(Library)] public static extern void balls_profile_close(IntPtr profile);

        [DllImport(Library)] public static extern int balls_profile_ball_limit(IntPtr profile, int kind, out double targetFps, out ulong balls);

        [DllImport(Library)] public static extern void balls_profile_remember_ball_limit(IntPtr profile, int kind, double targetFps, ulong balls);

        [DllImport(Library)] public static extern int balls_profile_benchmark_score(IntPtr profile, int kind, out double ballStepsPerSecond);

        [DllImport(Library)] public static extern void balls_profile_remember_benchmark(IntPtr profile, IntPtr benchmark, int complete, [MarshalAs(UnmanagedType.LPUTF8Str)] string gameVersion);

        [DllImport(Library)] public static extern IntPtr balls_editor_create();

        [DllImport(Library)] public static extern void balls_editor_destroy(IntPtr editor);

        [DllImport(Library)] public static extern void balls_editor_new_level(IntPtr editor, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, int width, int height, int playableHeight, int launcherColumns, int visibleRows);

        [DllImport(Library)] public static extern int balls_editor_load_level(IntPtr editor, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelDirectory, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelName);

        [DllImport(Library)] public static extern int balls_editor_save_level(IntPtr editor, [MarshalAs(UnmanagedType.LPUTF8Str)] string levelDirectory);

        [DllImport(Library)] public static extern void balls_editor_info(IntPtr editor, out NativeEditorInfo info);

        [DllImport(Library)] public static extern void balls_editor_resize_board(IntPtr editor, int width, int height, int playableHeight);

        [DllImport(Library)] public static extern void balls_editor_paint_cell(IntPtr editor, int column, int row, int color, int hits);

        [DllImport(Library)] public static extern int balls_editor_extend_brick(IntPtr editor, int column, int row);

        [DllImport(Library)] public static extern void balls_editor_erase_cell(IntPtr editor, int column, int row);

        [DllImport(Library)] public static extern void balls_editor_set_brick_hits(IntPtr editor, int column, int row, int hits);

        [DllImport(Library)] public static extern void balls_editor_set_brick_color(IntPtr editor, int column, int row, int color);

        [DllImport(Library)] public static extern void balls_editor_set_launcher_columns(IntPtr editor, int columns);

        [DllImport(Library)] public static extern void balls_editor_set_visible_rows(IntPtr editor, int rows);

        [DllImport(Library)] public static extern void balls_editor_set_launcher(IntPtr editor, int column, int row, int present, int color, int ballCount);

        [DllImport(Library)] public static extern void balls_editor_remove_launcher_row(IntPtr editor, int row);

        [DllImport(Library)] public static extern void balls_editor_auto_fill_launchers(IntPtr editor, int ballsPerLauncher);

        [DllImport(Library)] public static extern void balls_editor_begin_edit(IntPtr editor);

        [DllImport(Library)] public static extern int balls_editor_undo(IntPtr editor);

        [DllImport(Library)] public static extern int balls_editor_redo(IntPtr editor);

        [DllImport(Library)] public static extern void balls_editor_mark_saved(IntPtr editor);

        [DllImport(Library)] public static extern void balls_editor_validate(IntPtr editor, int mode, out NativeValidation validation);

        [DllImport(Library)] public static extern int balls_editor_problem(IntPtr editor, int index, byte[] buffer, int size);

        /// <summary>Decodes a packed ball into field coordinates.</summary>
        public static void UnpackBall(uint packed, float scaleX, float scaleY, out float x, out float y, out int color)
        {
            x = (packed & RenderCoordinateMask) / scaleX;
            y = ((packed >> RenderCoordinateBits) & RenderCoordinateMask) / scaleY;
            color = (int)(packed >> RenderColorShift);
        }

        public static string LastError()
        {
            var buffer = new byte[512];
            int length = balls_last_error(buffer, buffer.Length);
            return length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : string.Empty;
        }

        public static string BackendText(int kind, bool detail)
        {
            var buffer = new byte[256];
            int length = detail ? balls_backend_detail(kind, buffer, buffer.Length) : balls_backend_reason(kind, buffer, buffer.Length);
            return length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : string.Empty;
        }

        private const double BackendCacheSeconds = 1.0;
        private static readonly string[] BackendNames = new string[BackendCount];
        private static readonly string[] BackendTexts = new string[BackendCount * 2];
        private static readonly bool[] BackendAvailability = new bool[BackendCount];
        private static double backendCacheExpires = -1.0;

        /// <summary>The backend's fixed display name, looked up once.</summary>
        public static string BackendName(int kind)
        {
            if (kind >= 0 && kind < BackendCount && BackendNames[kind] != null)
            {
                return BackendNames[kind];
            }
            var buffer = new byte[64];
            int length = balls_backend_name(kind, buffer, buffer.Length);
            string name = length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : string.Empty;
            if (kind >= 0 && kind < BackendCount)
            {
                BackendNames[kind] = name;
            }
            return name;
        }

        /// <summary>The core's header notice for a level disk state, or an empty string when the files match.</summary>
        public static string LevelDiskNotice(int state)
        {
            var buffer = new byte[256];
            int length = balls_level_disk_notice(state, buffer, buffer.Length);
            return length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : string.Empty;
        }

        /// <summary>The core's toast for what turning ball collisions on did, or an empty string for no change.</summary>
        public static string BallCollisionsMessage(int change)
        {
            var buffer = new byte[256];
            int length = balls_ball_collisions_message(change, buffer, buffer.Length);
            return length > 0 ? System.Text.Encoding.UTF8.GetString(buffer, 0, length) : string.Empty;
        }

        /// <summary>Matches a backend by its display name, ignoring case, '+', '-' and '_', as the core does.</summary>
        public static bool TryBackendKind(string name, out int kind)
        {
            string wanted = Normalize(name);
            for (int candidate = 0; candidate < BackendCount; ++candidate)
            {
                if (Normalize(BackendName(candidate)) == wanted)
                {
                    kind = candidate;
                    return true;
                }
            }
            kind = -1;
            return false;
        }

        private static string Normalize(string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                return string.Empty;
            }
            var text = new System.Text.StringBuilder(name.Length);
            foreach (char letter in name)
            {
                if (letter != '+' && letter != '-' && letter != '_' && letter != ' ')
                {
                    text.Append(char.ToLowerInvariant(letter));
                }
            }
            return text.ToString();
        }

        /// <summary>
        /// Availability and its muted text, refreshed about once a second: asking the core can probe GPU libraries, which
        /// is too slow to repeat on every OnGUI pass.
        /// </summary>
        public static bool BackendAvailable(int kind)
        {
            RefreshBackendCache();
            return BackendAvailability[kind];
        }

        public static string BackendTextCached(int kind, bool detail)
        {
            RefreshBackendCache();
            return BackendTexts[kind * 2 + (detail ? 1 : 0)];
        }

        private static void RefreshBackendCache()
        {
            double now = UnityEngine.Time.realtimeSinceStartupAsDouble;
            if (now < backendCacheExpires)
            {
                return;
            }
            backendCacheExpires = now + BackendCacheSeconds;
            for (int kind = 0; kind < BackendCount; ++kind)
            {
                BackendAvailability[kind] = balls_backend_available(kind) != 0;
                BackendTexts[kind * 2] = BackendText(kind, false);
                BackendTexts[kind * 2 + 1] = BackendText(kind, true);
            }
        }
    }
}
