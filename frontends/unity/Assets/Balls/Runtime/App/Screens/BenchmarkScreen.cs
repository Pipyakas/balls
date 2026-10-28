using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's machine benchmark: measure every loadable backend on the bundled performance level, one row each
    /// with its progress or result, then keep the winner as the default backend.
    /// </summary>
    public sealed class BenchmarkScreen : Screen
    {
        private const string BenchmarkLevel = "DA_1000Bricks";
        private const float SkipDelay = 1.5f;

        private System.IntPtr benchmark;
        private NativeBenchmarkRow[] rows = System.Array.Empty<NativeBenchmarkRow>();
        private int winner = -1;
        private bool started;
        private float finishedAt = -1f;
        private bool loggedRows;

        public override void Enter()
        {
            rows = new NativeBenchmarkRow[8];
            benchmark = BallsNative.balls_benchmark_create(App.LevelDirectoryFor(BallsApp.PerformanceMode), BenchmarkLevel);
            if (benchmark == System.IntPtr.Zero)
            {
                App.Toast($"cannot start the benchmark: {BallsNative.LastError()}", true);
                return;
            }
            Debug.Log($"balls: benchmark handle={benchmark} dir={App.LevelDirectoryFor(BallsApp.PerformanceMode)} err='{BallsNative.LastError()}'");
            BallsNative.balls_benchmark_start(benchmark);
            started = true;
            finishedAt = -1f;
            winner = -1;
        }

        public override void Leave()
        {
            if (benchmark != System.IntPtr.Zero)
            {
                BallsNative.balls_benchmark_cancel(benchmark);
                BallsNative.balls_benchmark_destroy(benchmark);
                benchmark = System.IntPtr.Zero;
            }
        }

        public override void HandleEscape()
        {
            Continue();
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);

            widgets.Label(new Rect(0f, 48f, theme.UnitsWidth, 56f), "Setting up for this machine", TypeSizes.Large,
                Palette.Text, TextAnchor.MiddleCenter, true);
            widgets.Muted(new Rect(0f, 108f, theme.UnitsWidth, 24f),
                "Measuring each physics backend on this machine.", TypeSizes.Small, TextAnchor.MiddleCenter);

            int count = RefreshRows();
            float width = Mathf.Min(760f, theme.UnitsWidth - 80f);
            float x = (theme.UnitsWidth - width) * 0.5f;
            float y = 168f;
            for (int index = 0; index < count; ++index)
            {
                NativeBenchmarkRow row = rows[index];
                bool isWinner = winner >= 0 && row.kind == winner && row.status == 2;
                Rect line = new Rect(x, y, width, 40f);
                theme.Frame(line, isWinner ? Palette.Accent : Palette.Panel, Palette.Border);
                Color labelColor = isWinner ? Palette.Background : Palette.Text;
                widgets.Label(new Rect(line.x + 12f, line.y, line.width * 0.45f, line.height), row.Label,
                    TypeSizes.Header, labelColor, TextAnchor.MiddleLeft);
                string detail = row.status switch
                {
                    2 => $"{row.ballStepsPerSecond / 1e6:N0} M ball-steps/s",
                    3 => $"failed: {row.Failure}",
                    4 => "not available",
                    1 => $"{row.progress * 100f:F0}%",
                    _ => "waiting",
                };
                widgets.Label(new Rect(line.x, line.y, line.width - 12f, line.height), detail, TypeSizes.Value,
                    isWinner ? Palette.Background : Palette.TextMuted, TextAnchor.MiddleRight, true);
                y += 46f;
            }

            if (count == 0)
            {
                widgets.Muted(new Rect(x, y, width, 24f), "starting…", TypeSizes.Small);
            }

            if (winner >= 0)
            {
                widgets.Label(new Rect(x, y + 12f, width, 28f),
                    $"Default backend: {BallsNative.BackendName(winner)}", TypeSizes.Header, Palette.Accent,
                    TextAnchor.MiddleLeft, true);
                if (finishedAt < 0f)
                {
                    finishedAt = Time.realtimeSinceStartup;
                }
                if (!BallsApp.HoldingForCapture && Time.realtimeSinceStartup - finishedAt > SkipDelay)
                {
                    Continue();
                }
            }

            if (widgets.Button(new Rect((theme.UnitsWidth - 200f) * 0.5f, theme.UnitsHeight - 120f, 200f, 56f),
                    winner >= 0 ? "Continue" : "Skip"))
            {
                Continue();
            }
        }

        private int RefreshRows()
        {
            if (benchmark == System.IntPtr.Zero)
            {
                return 0;
            }
            int count = BallsNative.balls_benchmark_rows(benchmark, rows, rows.Length);
            if (!loggedRows)
            {
                loggedRows = true;
                Debug.Log($"balls: benchmark rows={count} firstStatus={(count > 0 ? rows[0].status : -1)} label='{(count > 0 ? rows[0].Label : "")}'");
            }
            int currentWinner = BallsNative.balls_benchmark_winner(benchmark);
            if (currentWinner >= 0)
            {
                winner = currentWinner;
            }
            return Mathf.Min(count, rows.Length);
        }

        private void Continue()
        {
            if (winner >= 0)
            {
                if (App.Settings != null)
                {
                    App.Settings.DefaultBackend = BallsNative.BackendName(winner);
                    App.SaveSettings();
                }
                App.SwitchBackend(winner);
                App.Toast($"Default backend: {BallsNative.BackendName(winner)}");
            }
            App.Go<MainMenuScreen>();
        }
    }
}
