using System.Collections.Generic;
using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's settings panel: a page from the main menu and an overlay from the pause menu. Values are saved as
    /// soon as they change and apply immediately.
    /// </summary>
    public sealed class SettingsScreen : Screen
    {
        private static readonly string[] BackendNames = BuildBackendNames();
        private bool overlay;
        private System.Action overlayClosed;
        private Vector2 scroll;
        private float measuredHeight = 730f;

        private static string[] BuildBackendNames()
        {
            var names = new string[BallsNative.BackendCount];
            for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
            {
                names[kind] = BallsNative.BackendName(kind);
            }
            return names;
        }

        public void DrawAsOverlay(System.Action closed)
        {
            overlay = true;
            overlayClosed = closed;
            Draw();
        }

        /// <summary>Scrolls the panel to its end, so a conformance screenshot can cover its last section.</summary>
        public void ScrollToEnd() => scroll.y = 100000f;

        public override void HandleEscape()
        {
            Close();
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            float width = theme.UnitsWidth;
            float height = theme.UnitsHeight;

            if (!overlay)
            {
                theme.Fill(new Rect(0f, 0f, width, height), Palette.Background);
                widgets.Label(new Rect(0f, 40f, width, 56f), "Settings", TypeSizes.Title, Palette.Accent,
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
                widgets.Label(new Rect(panel.x, panel.y + 12f, panel.width, 44f), "Settings", TypeSizes.Large,
                    Palette.Text, TextAnchor.MiddleCenter, true);
            }

            Rect inner = new Rect(panel.x + 16f, panel.y + (overlay ? 62f : 16f), panel.width - 32f,
                panel.height - (overlay ? 132f : 86f));
            SettingsData settings = App.Settings;

            // The height is measured while drawing, so a section added later (the device rows) cannot fall outside
            // the scroll range; the first frame after that uses the previous measurement.
            widgets.ScrollColumn(inner, measuredHeight, ref scroll, content =>
            {
                float y = content.y;
                // Gameplay
                widgets.Label(new Rect(content.x, y, content.width, 22f), "Gameplay", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                widgets.Label(new Rect(content.x, y, content.width, 22f), "Ball collisions", TypeSizes.Small,
                    Palette.Text, TextAnchor.UpperLeft, false);
                y += 26f;
                if (widgets.OptionRow(new Rect(content.x, y, content.width, 36f), "Off", !settings.BallCollisions))
                {
                    settings.BallCollisions = false;
                    App.ApplyBallCollisions(false);
                    Apply(settings);
                }
                y += 40f;
                if (widgets.OptionRow(new Rect(content.x, y, content.width, 36f), "On", settings.BallCollisions))
                {
                    settings.BallCollisions = true;
                    App.ApplyBallCollisions(true);
                    Apply(settings);
                }
                y += 40f;
                widgets.Muted(new Rect(content.x, y, content.width, 60f),
                    "Testing only - the game rules have balls\npass through each other.", TypeSizes.Small);
                y += 50f;

                // Layout
                widgets.Label(new Rect(content.x, y, content.width, 22f), "Layout orientation", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                string[] orientations = { "Auto", "Landscape", "Portrait" };
                for (int option = 0; option < orientations.Length; ++option)
                {
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), orientations[option],
                            settings.Orientation == option))
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

                // Display
                widgets.Label(new Rect(content.x, y, content.width, 22f), "Display", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                widgets.Label(new Rect(content.x, y, content.width, 22f), "VSync", TypeSizes.Small,
                    Palette.Text, TextAnchor.UpperLeft, false);
                y += 26f;
                if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), "Off", !settings.Vsync))
                {
                    settings.Vsync = false;
                    QualitySettings.vSyncCount = 0;
                    Apply(settings);
                }
                y += 38f;
                if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), "On", settings.Vsync))
                {
                    settings.Vsync = true;
                    QualitySettings.vSyncCount = 1;
                    Apply(settings);
                }
                y += 44f;

                // Physics
                widgets.Label(new Rect(content.x, y, content.width, 22f), "Physics", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
                {
                    if (kind == BallsNative.BackendHybrid && !BallsNative.BackendAvailable(kind))
                    {
                        continue;
                    }
                    bool available = BallsNative.BackendAvailable(kind);
                    bool selected = App.DefaultBackendKind == kind;
                    string detail = !available ? BallsNative.BackendTextCached(kind, false)
                        : App.Profile.TryScore(kind, out double score) ? $"{score / 1e6:N0} M ball-steps/s"
                        : BallsNative.BackendTextCached(kind, true);
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), BackendNames[kind], selected,
                            available, detail) && available)
                    {
                        settings.DefaultBackend = BallsNative.StoredBackendName(kind);
                        Apply(settings);
                    }
                    y += 38f;
                }
                y += 8f;
                if (widgets.Button(new Rect(content.x, y, content.width, 44f), "Run machine benchmark", TypeSizes.Label))
                {
                    App.Go<BenchmarkScreen>();
                }
                y += 56f;

                y = DrawDevicePicks(widgets, content, y);
                measuredHeight = Mathf.Max(730f, y - content.y + 24f);
            });

            Rect back = new Rect(panel.x + (panel.width - 200f) * 0.5f, panel.yMax - 68f, 200f, 56f);
            if (widgets.Button(back, "Back"))
            {
                Close();
            }
        }

        /// <summary>
        /// The game's Devices rows: one per physical device, Off and one button per backend it runs. Each device takes at
        /// most one backend, and two or more devices become the composer, which the core validates before it is stored.
        /// </summary>
        private float DrawDevicePicks(Widgets widgets, Rect content, float y)
        {
            widgets.Label(new Rect(content.x, y, content.width, 22f), "Devices", TypeSizes.Small, Palette.Accent,
                TextAnchor.UpperLeft, true);
            y += 26f;
            List<BallsNative.Pick> picks = new(App.DefaultBackendKind >= 0
                ? BallsNative.DisplayPicks(App.DefaultBackendKind)
                : new List<BallsNative.Pick>());
            foreach (BallsNative.PhysicsDevice device in BallsNative.Devices)
            {
                string chosen = null;
                foreach (BallsNative.Pick pick in picks)
                {
                    if (pick.device == device.id)
                    {
                        chosen = BallsNative.BackendName(pick.kind);
                    }
                }
                string name = device.type == 0 ? $"{device.name} (CPU)" : device.name;
                widgets.Label(new Rect(content.x, y, content.width, 22f), name, TypeSizes.Small, Palette.Text,
                    TextAnchor.UpperLeft, true);
                y += 24f;
                int[] kinds = BallsNative.PickableKinds(device.id);
                float buttonWidth = (content.width - (kinds.Length) * 8f) / (kinds.Length + 1);
                if (widgets.AccentButton(new Rect(content.x, y, buttonWidth, 32f), "Off", chosen == null,
                        TypeSizes.Small))
                {
                    ApplyDevicePick(picks, device.id, -1);
                }
                for (int index = 0; index < kinds.Length; ++index)
                {
                    int kind = kinds[index];
                    var button = new Rect(content.x + (index + 1) * (buttonWidth + 8f), y, buttonWidth, 32f);
                    if (widgets.AccentButton(button, BallsNative.BackendName(kind), chosen == BallsNative.BackendName(kind),
                            TypeSizes.Small))
                    {
                        ApplyDevicePick(picks, device.id, kind);
                    }
                }
                y += 40f;
            }
            widgets.Muted(new Rect(content.x, y, content.width, 40f),
                "One backend per device. Two or more devices\nrun as Hybrid, splitting the balls;\none runs that backend alone.",
                TypeSizes.Small);
            return y + 50f;
        }

        private void ApplyDevicePick(List<BallsNative.Pick> picks, string device, int kind)
        {
            picks.RemoveAll(pick => pick.device == device);
            if (kind >= 0)
            {
                picks.Add(new BallsNative.Pick(device, kind));
            }
            if (picks.Count == 0)
            {
                // The design keeps the last device on, and the core refuses an empty list for the same reason.
                App.Toast("The last device cannot be turned off", true);
                return;
            }
            if (BallsNative.SetPicks(picks, out string problem))
            {
                // The picks become the default backend, saved as picks so they come back on the next start.
                App.Settings.DefaultBackend = BallsNative.StoredBackendName(BallsNative.KindForPicks(picks));
                Apply(App.Settings);
                App.Toast(kind >= 0 ? $"Device: {BallsNative.BackendName(kind)}" : "Device off");
            }
            else
            {
                App.Toast($"Cannot use that backend: {problem}", true);
            }
        }

        private void Apply(SettingsData settings)
        {
            App.SaveSettings();
            App.Toast("Settings saved");
        }

        private void Close()
        {
            if (overlay)
            {
                overlay = false;
                overlayClosed?.Invoke();
                return;
            }
            App.Go<MainMenuScreen>();
        }
    }
}
