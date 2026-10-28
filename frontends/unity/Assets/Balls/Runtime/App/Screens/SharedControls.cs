using System;
using System.Collections.Generic;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// Controls the design puts in two places, the Settings page and the gameplay gadget's Advanced Settings: the Devices
    /// rows and the Ball collisions pair. Drawn once here, so the two places cannot drift apart.
    /// </summary>
    public static class SharedControls
    {
        private const string CollisionsHint = "Testing only - the game rules have balls\npass through each other.";
        private const string NeedsScalar = "\nNeeds Scalar, not offered here.";
        private const string DevicesHint =
            "One backend per device. Two or more devices\nrun as Hybrid, splitting the balls;\none runs that backend alone.";
        private const float HintLine = 20f;

        /// <summary>The Ball collisions pair, Off / On side by side, saved with the settings; returns the next y.</summary>
        public static float DrawBallCollisions(BallsApp app, Widgets widgets, Rect content, float y)
        {
            widgets.Label(new Rect(content.x, y, content.width, 22f), "Ball collisions", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            y += 26f;
            bool on = app.Settings != null && app.Settings.BallCollisions;
            bool scalarOffered = BallsNative.BackendAvailable(BallsNative.BackendScalar);
            float half = (content.width - 8f) * 0.5f;
            if (widgets.AccentButton(new Rect(content.x, y, half, 36f), "Off", !on, TypeSizes.Label) && on)
            {
                SetCollisions(app, false);
            }
            var onButton = new Rect(content.x + half + 8f, y, half, 36f);
            if (scalarOffered)
            {
                if (widgets.AccentButton(onButton, "On", on, TypeSizes.Label) && !on)
                {
                    SetCollisions(app, true);
                }
            }
            else
            {
                widgets.Button(onButton, "On", TypeSizes.Label, false);
            }
            y += 42f;
            int lines = scalarOffered ? 2 : 3;
            widgets.Muted(new Rect(content.x, y, content.width, lines * HintLine),
                scalarOffered ? CollisionsHint : CollisionsHint + NeedsScalar, TypeSizes.Tiny);
            return y + lines * HintLine + 8f;
        }

        public static float BallCollisionsHeight() =>
            26f + 42f + (BallsNative.BackendAvailable(BallsNative.BackendScalar) ? 2 : 3) * HintLine + 8f;

        private static void SetCollisions(BallsApp app, bool on)
        {
            if (app.Settings == null)
            {
                return;
            }
            app.Settings.BallCollisions = on;
            app.ApplyBallCollisions(on);
            app.SaveSettings();
        }

        /// <summary>
        /// The game's Devices rows: one per physical device, Off and one button per backend it runs, showing the picks of
        /// <paramref name="runningKind"/>. The core validates a change before <paramref name="applied"/> sees it, and the
        /// last device cannot be turned off.
        /// </summary>
        public static float DrawDevicePicks(BallsApp app, Widgets widgets, Rect content, float y, int runningKind,
            Action<List<BallsNative.Pick>> applied)
        {
            widgets.Label(new Rect(content.x, y, content.width, 22f), "Devices", TypeSizes.Small, Palette.Accent,
                TextAnchor.UpperLeft, true);
            y += 26f;
            var picks = new List<BallsNative.Pick>(BallsNative.DisplayPicks(runningKind));
            foreach (BallsNative.PhysicsDevice device in BallsNative.Devices)
            {
                int chosen = -1;
                foreach (BallsNative.Pick pick in picks)
                {
                    if (pick.device == device.id)
                    {
                        chosen = pick.kind;
                    }
                }
                string name = device.type == 0 ? $"{device.name} (CPU)" : device.name;
                widgets.Label(new Rect(content.x, y, content.width, 22f),
                    widgets.Ellipsize(name, TypeSizes.Small, content.width), TypeSizes.Small, Palette.Text,
                    TextAnchor.UpperLeft, true);
                y += 24f;
                int[] kinds = BallsNative.PickableKinds(device.id);
                float buttonWidth = (content.width - kinds.Length * 8f) / (kinds.Length + 1);
                if (widgets.AccentButton(new Rect(content.x, y, buttonWidth, 32f), "Off", chosen < 0, TypeSizes.Small) &&
                    chosen >= 0)
                {
                    Change(app, picks, device.id, -1, applied);
                }
                for (int index = 0; index < kinds.Length; ++index)
                {
                    int kind = kinds[index];
                    var button = new Rect(content.x + (index + 1) * (buttonWidth + 8f), y, buttonWidth, 32f);
                    if (widgets.AccentButton(button, BallsNative.BackendName(kind), chosen == kind, TypeSizes.Small) &&
                        chosen != kind)
                    {
                        Change(app, picks, device.id, kind, applied);
                    }
                }
                y += 40f;
            }
            widgets.Muted(new Rect(content.x, y, content.width, 3 * HintLine), DevicesHint, TypeSizes.Tiny);
            return y + 3 * HintLine + 8f;
        }

        public static float DevicePicksHeight() => 26f + BallsNative.Devices.Count * 64f + 3 * HintLine + 8f;

        private static void Change(BallsApp app, List<BallsNative.Pick> picks, string device, int kind,
            Action<List<BallsNative.Pick>> applied)
        {
            var next = new List<BallsNative.Pick>(picks);
            next.RemoveAll(pick => pick.device == device);
            if (kind >= 0)
            {
                next.Add(new BallsNative.Pick(device, kind));
            }
            if (next.Count == 0)
            {
                app.Toast("The last device cannot be turned off", true);
                return;
            }
            if (!BallsNative.SetPicks(next, out string problem))
            {
                app.Toast($"Cannot use that backend: {problem}", true);
                return;
            }
            applied(next);
        }
    }
}
