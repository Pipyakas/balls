using System.Collections.Generic;

namespace Balls.App
{
    /// <summary>
    /// The one-way street from Unity's Update (and the HUD buttons) into the session system: plain fields, consumed
    /// once per frame. Keeping input here instead of in systems means the systems stay free of UnityEngine input.
    /// </summary>
    public static class BallsInput
    {
        public struct StartRequest
        {
            public int mode;
            public string levelDirectory;
            public string levelName;
            public int backend;
            public bool collisions;
        }

        private static readonly Queue<(int column, int row)> LauncherClicks = new();
        private static StartRequest pendingStart;
        private static bool startPending;
        private static bool restartPending;
        private static bool autoReleasePending;
        private static int backendSwitch = -1;
        private static int collisionsRequest = -1;

        public static void RequestStart(in StartRequest request)
        {
            pendingStart = request;
            startPending = true;
        }

        public static void ClickLauncher(int column, int row) => LauncherClicks.Enqueue((column, row));

        public static void RequestRestart() => restartPending = true;

        public static void RequestAutoRelease() => autoReleasePending = true;

        public static void RequestBackendSwitch(int kind) => backendSwitch = kind;

        public static void RequestCollisions(bool enabled) => collisionsRequest = enabled ? 1 : 0;

        public static bool TryTakeStartRequest(out StartRequest request)
        {
            request = pendingStart;
            bool pending = startPending;
            startPending = false;
            return pending;
        }

        public static bool TryTakeLauncher(out int column, out int row)
        {
            if (LauncherClicks.Count == 0)
            {
                column = 0;
                row = 0;
                return false;
            }
            (column, row) = LauncherClicks.Dequeue();
            return true;
        }

        public static bool TryTakeRestart()
        {
            bool pending = restartPending;
            restartPending = false;
            return pending;
        }

        public static bool TryTakeAutoRelease()
        {
            bool pending = autoReleasePending;
            autoReleasePending = false;
            return pending;
        }

        public static bool TryTakeBackendSwitch(out int kind)
        {
            kind = backendSwitch;
            backendSwitch = -1;
            return kind >= 0;
        }

        public static bool TryTakeCollisions(out bool enabled)
        {
            enabled = collisionsRequest == 1;
            bool pending = collisionsRequest >= 0;
            collisionsRequest = -1;
            return pending;
        }

        public static void Reset()
        {
            LauncherClicks.Clear();
            startPending = false;
            restartPending = false;
            autoReleasePending = false;
            backendSwitch = -1;
            collisionsRequest = -1;
        }
    }
}
