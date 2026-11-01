using System;
using Balls.Native;

namespace Balls.App
{

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

        public State Poll() => (State)BallsNative.balls_level_watcher_poll(handle);

        public static string Notice(State state)
        {
            string text = BallsNative.LevelDiskNotice((int)state);
            return text.Length > 0 ? text : null;
        }
    }
}
