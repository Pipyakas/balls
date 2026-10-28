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
            float panelHeight = Mathf.Min(560f, height - 120f);
            Rect panel = new Rect((width - panelWidth) * 0.5f, (height - panelHeight) * 0.5f, panelWidth, panelHeight);
            theme.Frame(panel, Palette.Panel, Palette.Border);
            if (overlay)
            {
                widgets.Label(new Rect(panel.x, panel.y + 12f, panel.width, 44f), "Settings", TypeSizes.Large,
                    Palette.Text, TextAnchor.MiddleCenter, true);
            }

            Rect inner = new Rect(panel.x + 16f, panel.y + (overlay ? 62f : 16f), panel.width - 32f,
                panel.height - (overlay ? 132f : 86f));
            SettingsData settings = App.Settings;

            widgets.ScrollColumn(inner, 570f, ref scroll, content =>
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
                widgets.Muted(new Rect(content.x, y, content.width, 22f),
                    "Testing only — the game rules have balls pass through each other.", TypeSizes.Small);
                y += 34f;

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

                // Display
                widgets.Label(new Rect(content.x, y, content.width, 22f), "Display", TypeSizes.Small,
                    Palette.Accent, TextAnchor.UpperLeft, true);
                y += 26f;
                if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), settings.Vsync ? "VSync on" : "VSync off", true))
                {
                    settings.Vsync = !settings.Vsync;
                    QualitySettings.vSyncCount = settings.Vsync ? 1 : 0;
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
                    bool selected = settings.BackendKind(BallsNative.BackendAvx2) == kind;
                    string detail = available
                        ? BallsNative.BackendTextCached(kind, true)
                        : BallsNative.BackendTextCached(kind, false);
                    if (widgets.OptionRow(new Rect(content.x, y, content.width, 34f), BackendNames[kind], selected,
                            available, detail) && available)
                    {
                        settings.DefaultBackend = BackendNames[kind];
                        App.SwitchBackend(kind);
                        Apply(settings);
                    }
                    y += 38f;
                }
                y += 8f;
                if (widgets.Button(new Rect(content.x, y, content.width, 44f), "Run machine benchmark", TypeSizes.Label))
                {
                    App.Go<BenchmarkScreen>();
                }
            });

            Rect back = new Rect(panel.x + (panel.width - 200f) * 0.5f, panel.yMax - 68f, 200f, 56f);
            if (widgets.Button(back, "Back"))
            {
                Close();
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
