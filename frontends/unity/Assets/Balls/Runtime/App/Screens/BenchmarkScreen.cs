using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's machine benchmark: measure every loadable backend and keep the winner as the default.
    ///
    /// Parity slice S6. The core already runs the workload (`BackendBenchmark`); it needs an
    /// ABI entry point before this screen can drive it, so for now it reports what the plugins say about themselves.
    /// </summary>
    public sealed class BenchmarkScreen : Screen
    {
        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);

            widgets.Label(new Rect(0f, 48f, theme.UnitsWidth, 56f), "Setting up for this machine", TypeSizes.Large,
                Palette.Text, TextAnchor.MiddleCenter, true);
            widgets.Muted(new Rect(0f, 108f, theme.UnitsWidth, 24f),
                "Measuring each physics backend on this machine.", TypeSizes.Small, TextAnchor.MiddleCenter);

            float width = Mathf.Min(720f, theme.UnitsWidth - 80f);
            float x = (theme.UnitsWidth - width) * 0.5f;
            float y = 160f;
            for (int kind = 0; kind < Balls.Native.BallsNative.BackendCount; ++kind)
            {
                bool available = Balls.Native.BallsNative.balls_backend_available(kind) != 0;
                widgets.OptionRow(new Rect(x, y, width, 40f), Balls.Native.BallsNative.BackendName(kind), false,
                    available, available ? Balls.Native.BallsNative.BackendText(kind, true) : "not available");
                y += 46f;
            }

            widgets.Muted(new Rect(x, y + 16f, width, 44f),
                "Scores need the core's benchmark entry point; until it exists this page reports availability only.",
                TypeSizes.Small);

            if (widgets.Button(new Rect((theme.UnitsWidth - 200f) * 0.5f, theme.UnitsHeight - 120f, 200f, 56f), "Continue"))
            {
                App.Go<MainMenuScreen>();
            }
        }
    }
}
