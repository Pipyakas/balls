using System;
using System.Collections.Generic;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{

    public static class SharedControls
    {
        private const string DevicesHint =
            "The devices Hybrid splits the balls between, one backend each. Leaving one device on runs that backend alone.";
        private const float HintLine = 20f;

        public static float DrawBallCollisions(BallsApp app, Widgets widgets, Rect content, float y)
        {
            widgets.Label(new Rect(content.x, y, content.width, 22f), "Ball collisions", TypeSizes.Small,
                Palette.Accent, TextAnchor.UpperLeft, true);
            y += 26f;
            bool on = app.Settings != null && app.Settings.BallCollisions;
            List<NativeUiOption> options = BallsNative.MenuOptions(BallsNative.MenuBallCollisions, on ? 1 : 0, IntPtr.Zero);
            float half = (content.width - 8f) * 0.5f;

            if (options.Count >= 2)
            {
                NativeUiOption offOpt = options[0];
                NativeUiOption onOpt = options[1];

                if (widgets.AccentButton(new Rect(content.x, y, half, 36f), offOpt.Label, offOpt.selected != 0, TypeSizes.Label) && on)
                {
                    SetCollisions(app, false);
                }

                var onButton = new Rect(content.x + half + 8f, y, half, 36f);
                if (onOpt.enabled != 0)
                {
                    if (widgets.AccentButton(onButton, onOpt.Label, onOpt.selected != 0, TypeSizes.Label) && !on)
                    {
                        SetCollisions(app, true);
                    }
                }
                else
                {
                    widgets.Button(onButton, onOpt.Label, TypeSizes.Label, false);
                }
                y += 42f;

                string hint = offOpt.Detail;
                if (!string.IsNullOrEmpty(hint))
                {
                    GUIStyle hintStyle = app.Theme.TextStyle(TypeSizes.Tiny, Palette.TextMuted);
                    hintStyle.wordWrap = true;
                    float hintHeight = hintStyle.CalcHeight(new GUIContent(hint), content.width);
                    if (hintHeight < HintLine) hintHeight = HintLine;
                    GUI.Label(app.Theme.Units(content.x, y, content.width, hintHeight), hint, hintStyle);
                    return y + hintHeight + 8f;
                }
            }
            return y + 8f;
        }

        public static float BallCollisionsHeight() =>
            26f + 42f + 3 * HintLine + 8f;

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

        public static float DrawDevicePicks(BallsApp app, Widgets widgets, Rect content, float y, int runningKind,
            Action<List<BallsNative.Pick>> applied)
        {

            if (runningKind != BallsNative.BackendHybrid)
            {
                return y;
            }
            widgets.Label(new Rect(content.x, y, content.width, 22f), "Devices", TypeSizes.Small, Palette.Accent,
                TextAnchor.UpperLeft, true);
            y += 26f;
            var currentPicks = new List<BallsNative.Pick>(BallsNative.DisplayPicks(runningKind));
            List<NativeDevicePickRow> rows = BallsNative.MenuDeviceRows(currentPicks);

            for (int rowIndex = 0; rowIndex < rows.Count; ++rowIndex)
            {
                NativeDevicePickRow row = rows[rowIndex];
                widgets.Label(new Rect(content.x, y, content.width, 22f),
                    widgets.Ellipsize(row.Label, TypeSizes.Small, content.width), TypeSizes.Small, Palette.Text,
                    TextAnchor.UpperLeft, true);
                y += 24f;

                int optCount = row.optionCount;
                float buttonWidth = (content.width - (optCount - 1) * 8f) / optCount;
                for (int optIndex = 0; optIndex < optCount; ++optIndex)
                {
                    NativeUiOption opt = row.options[optIndex];
                    var button = new Rect(content.x + optIndex * (buttonWidth + 8f), y, buttonWidth, 32f);
                    bool isSelected = opt.selected != 0;
                    bool isEnabled = opt.enabled != 0;

                    if (isEnabled)
                    {
                        if (widgets.AccentButton(button, opt.Label, isSelected, TypeSizes.Small) && !isSelected)
                        {
                            List<BallsNative.Pick> nextPicks = BallsNative.MenuPickDevice(currentPicks, rowIndex, optIndex, out bool changed);
                            if (changed)
                            {
                                applied(nextPicks);
                            }
                        }
                    }
                    else
                    {
                        widgets.Button(button, opt.Label, TypeSizes.Small, false);
                    }
                }
                y += 40f;
            }

            widgets.Muted(new Rect(content.x, y, content.width, 3 * HintLine), DevicesHint, TypeSizes.Tiny);
            return y + 3 * HintLine + 8f;
        }

        public static float DevicePicksHeight() => 26f + BallsNative.Devices.Count * 64f + 3 * HintLine + 8f;
    }
}
