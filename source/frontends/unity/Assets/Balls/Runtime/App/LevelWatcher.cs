using System;
using Balls.Native;

namespace Balls.App
{
    /// <summary>
    /// Tells when a level's files on disk differ from what was loaded: an editor save, a copy or an import (Changed),
    /// or the files being deleted (Removed). The core's LevelWatcher does the polling, as in SDL and Unreal; this only
    /// holds its handle.
    /// </summary>
    public sealed class LevelWatcher
    {
        public enum State
        {
            Unchanged = BallsNative.DiskUnchanged,
            Changed = BallsNative.DiskChanged,
            Removed = BallsNative.DiskRemoved,
        }

        private IntPtr handle;

        public void Watch(string directory, string name)
        {
            BallsNative.balls_level_watcher_destroy(handle);
            handle = BallsNative.balls_level_watcher_create(directory, name);
        }

        /// <summary>Changed stays latched until the next Watch, so the notice outlives an edit that is undone.</summary>
        public State Poll() => (State)BallsNative.balls_level_watcher_poll(handle);

        /// <summary>The header notice for the state, or null when the files still match.</summary>
        public static string Notice(State state)
        {
            string text = BallsNative.LevelDiskNotice((int)state);
            return text.Length > 0 ? text : null;
        }
    }
}
