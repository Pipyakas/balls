using System.IO;
using UnityEngine;

namespace Balls.App
{
    /// <summary>
    /// Polls a level's file pair so the game can tell when the files on disk differ from what was loaded: an editor
    /// save, a copy or an import (Changed), or the files being deleted (Removed).
    /// </summary>
    public sealed class LevelWatcher
    {
        public enum State
        {
            Unchanged,
            Changed,
            Removed,
        }

        private const float PollSeconds = 0.5f;
        private const long Missing = -1;

        private readonly long[] loaded = new long[2];
        private readonly string[] paths = new string[2];
        private float nextPoll;
        private State state;

        public void Watch(string directory, string name)
        {
            paths[0] = Path.Combine(directory, name + "_Bricks.txt");
            paths[1] = Path.Combine(directory, name + "_SpawnBall.txt");
            for (int index = 0; index < paths.Length; ++index)
            {
                loaded[index] = Stamp(paths[index]);
            }
            state = State.Unchanged;
            nextPoll = 0f;
        }

        /// <summary>Changed stays latched until the next Watch, so the notice outlives an edit that is undone.</summary>
        public State Poll()
        {
            if (paths[0] == null || Time.realtimeSinceStartup < nextPoll)
            {
                return state;
            }
            nextPoll = Time.realtimeSinceStartup + PollSeconds;
            bool removed = false;
            bool changed = false;
            for (int index = 0; index < paths.Length; ++index)
            {
                long stamp = Stamp(paths[index]);
                removed |= stamp == Missing;
                changed |= stamp != loaded[index];
            }
            if (removed)
            {
                state = State.Removed;
            }
            else if (changed || state == State.Changed)
            {
                state = State.Changed;
            }
            else
            {
                state = State.Unchanged;
            }
            return state;
        }

        private static long Stamp(string path)
        {
            try
            {
                return File.Exists(path) ? File.GetLastWriteTimeUtc(path).Ticks ^ new FileInfo(path).Length : Missing;
            }
            catch (IOException)
            {
                return Missing;
            }
        }
    }
}
