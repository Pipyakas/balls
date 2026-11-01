using System.Collections.Generic;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{

    public sealed class SettingsScreen : Screen
    {
        private bool overlay;
        private System.Action overlayClosed;
        private Vector2 scroll;
        private float measuredHeight = 730f;

        public void DrawAsOverlay(System.Action closed)
        {
            overlay = true;
            overlayClosed = closed;
            Draw();
        }

        public void ScrollToEnd() => scroll.y = 100000f;

        public override void HandleEscape()
        {
            App.EscapeFlow();
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            float width = theme.UnitsWidth;
            float height = theme.UnitsHeight;

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = "Settings";
            }

            if (!overlay)
            {
                theme.Fill(new Rect(0f, 0f, width, height), Palette.Background);
                widgets.Label(new Rect(0f, 40f, width, 56f), title, TypeSizes.Title, Palette.Accent,
                    TextAnchor.MiddleCenter, true);
            }
            else
            {
                theme.Fill(new Rect(0f, 0f, width, height), new Color(0f, 0f, 0f, 0.70f));
            }

            float panelWidth = Mathf.Min(560f, width - 64f);
            float panelTop = overlay ? 60f : 110f;
            float panelHeight = Mathf.Min(780f, height - panelTop - 24f);
            Rect panel = new Rect((width - panelWidth) * 0.5f, overlay ? (height - panelHeight) * 0.5f : panelTop,
                panelWidth, panelHeight);
            theme.Frame(panel, Palette.Panel, Palette.Border);
            if (overlay)
            {
                widgets.Label(new Rect(panel.x, panel.y + 12f, panel.width, 44f), title, TypeSizes.Large,
                    Palette.Text, TextAnchor.MiddleCenter, true);
            }

            Rect inner = new Rect(panel.x + 16f, panel.y + (overlay ? 62f : 16f), panel.width - 32f,
                panel.height - (overlay ? 132f : 86f));
            SettingsData settings = App.Settings;

            widgets.ScrollColumn(inner, measuredHeight, ref scroll, content =>
            {
                float y = content.y;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Gameplay", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                y = SharedControls.DrawBallCollisions(App, widgets, content, y);

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Layout orientation", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                List<NativeUiOption> orientationOptions = BallsNative.MenuOptions(BallsNative.MenuOrientation, settings.Orientation, System.IntPtr.Zero);
                for (int option = 0; option < orientationOptions.Count; ++option)
                {
                    NativeUiOption opt = orientationOptions[option];
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), opt.Label,
                            opt.selected != 0, opt.enabled != 0, opt.Detail))
                    {
                        settings.Orientation = option;
                        App.ApplyOrientation();
                        Apply(settings);
                    }
                    y += 38f;
                }
                widgets.Muted(new Rect(content.x, y, content.width, 22f), "Auto follows the window shape.",
                    TypeSizes.Small);
                y += 34f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Display", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                widgets.Label(new Rect(content.x, y, content.width, 22f), "VSync", TypeSizes.Small,
                    Palette.Text, TextAnchor.UpperLeft, false);
                y += 26f;
                List<NativeUiOption> vsyncOptions = BallsNative.MenuOptions(BallsNative.MenuOffOn, settings.Vsync ? 1 : 0, System.IntPtr.Zero);
                for (int option = 0; option < vsyncOptions.Count; ++option)
                {
                    NativeUiOption opt = vsyncOptions[option];
                    bool isOptionOn = option == 1;
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), opt.Label, opt.selected != 0, opt.enabled != 0, opt.Detail))
                    {
                        settings.Vsync = isOptionOn;
                        QualitySettings.vSyncCount = isOptionOn ? 1 : 0;
                        Apply(settings);
                    }
                    y += 38f;
                }
                y += 6f;

                widgets.Label(new Rect(content.x, y, content.width, 22f), "Physics", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                List<NativeUiOption> backendOptions = BallsNative.MenuDefaultBackends(App.DefaultBackendKind, App.Profile.Handle);
                for (int index = 0; index < backendOptions.Count; ++index)
                {
                    NativeUiOption opt = backendOptions[index];
                    int kind = opt.kind;
                    bool available = opt.enabled != 0;
                    bool selected = opt.selected != 0;
                    string detail = opt.Detail;
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), opt.Label, selected,
                            available, detail) && available)
                    {
                        settings.DefaultBackend = BallsNative.StoredBackendName(kind);
                        Apply(settings);
                    }
                    y += 38f;
                }

                if (App.FlowState.canRunMachineBenchmark != 0)
                {
                    y += 8f;
                    if (widgets.Button(new Rect(content.x, y, content.width, 44f), "Run machine benchmark", TypeSizes.Label))
                    {
                        App.ActivateFlow(BallsNative.FlowRunMachineBenchmark);
                    }
                    y += 56f;
                }

                y = SharedControls.DrawDevicePicks(App, widgets, content, y, App.DefaultBackendKind, picks =>
                {

                    int kind = BallsNative.KindForPicks(picks);
                    settings.DefaultBackend = BallsNative.StoredBackendName(kind);
                    App.SetBackend(kind);
                    Apply(settings);
                });
                measuredHeight = Mathf.Max(730f, y - content.y + 24f);
            });

            Rect back = new Rect(panel.x + (panel.width - 200f) * 0.5f, panel.yMax - 68f, 200f, 56f);
            int buttonCount = App.FlowButtonCount;
            string backLabel = "Back";
            int backAction = BallsNative.FlowBack;
            if (buttonCount > 0 && BallsNative.balls_flow_button(App.FlowHandle, 0, out NativeFlowButton button) != 0)
            {
                backLabel = button.Label;
                backAction = button.action;
            }
            if (widgets.Button(back, backLabel))
            {
                if (overlay)
                {
                    overlay = false;
                    overlayClosed?.Invoke();
                }
                App.ActivateFlow(backAction);
            }
        }

        private void Apply(SettingsData settings)
        {
            App.SaveSettings();
        }
    }
}
