using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's machine benchmark: measure every loadable backend on the bundled performance level, one row each
    /// with its progress or result, then keep the winner as the default backend. The screen flow owns advance and navigation.
    /// </summary>
    public sealed class BenchmarkScreen : Screen
    {
        private const string BenchmarkLevel = "DA_1000Bricks";
        private const float ProgressWidth = 220f;
        private const float ProgressHeight = 8f;
        private const int RowPending = 0;
        private const int RowRunning = 1;

        private System.IntPtr benchmark;
        private NativeBenchmarkRow[] rows = System.Array.Empty<NativeBenchmarkRow>();
        private int winner = -1;
        private bool started;
        private bool loggedRows;
        private bool recorded;
        private bool adopted;

        public void StartBenchmark()
        {
            if (started)
            {
                return;
            }
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
            recorded = false;
            adopted = false;
            winner = -1;
        }

        public void CancelBenchmark()
        {
            if (benchmark != System.IntPtr.Zero)
            {
                BallsNative.balls_benchmark_cancel(benchmark);
            }
        }

        public override void Enter()
        {
            StartBenchmark();
        }

        public override void Leave()
        {
            if (benchmark != System.IntPtr.Zero)
            {
                BallsNative.balls_benchmark_cancel(benchmark);
                BallsNative.balls_benchmark_destroy(benchmark);
                benchmark = System.IntPtr.Zero;
            }
            started = false;
        }

        public override void HandleEscape()
        {
            App.EscapeFlow();
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            theme.Fill(new Rect(0f, 0f, theme.UnitsWidth, theme.UnitsHeight), Palette.Background);

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = "Setting up for this machine";
            }
            string footnote = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextFootnote);
            if (string.IsNullOrEmpty(footnote))
            {
                footnote = "Measuring each physics backend on this machine.";
            }

            widgets.Label(new Rect(0f, 48f, theme.UnitsWidth, 56f), title, TypeSizes.Large,
                Palette.Text, TextAnchor.MiddleCenter, true);
            widgets.Muted(new Rect(0f, 108f, theme.UnitsWidth, 24f),
                footnote, TypeSizes.Small, TextAnchor.MiddleCenter);

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
                if (row.status == RowRunning)
                {
                    // The game's progress bar while the candidate runs.
                    var track = new Rect(line.xMax - 12f - ProgressWidth, line.center.y - ProgressHeight * 0.5f,
                        ProgressWidth, ProgressHeight);
                    theme.Fill(track, Palette.ControlPressed);
                    theme.Fill(new Rect(track.x, track.y, track.width * Mathf.Clamp01((float)row.progress), track.height),
                        Palette.Accent);
                }
                else
                {
                    // The core's text: balls at 60 FPS and ball-steps, failed: reason, or not available.
                    string detail = row.status == RowPending ? "waiting" : row.Result;
                    widgets.Label(new Rect(line.x, line.y, line.width - 12f, line.height), detail, TypeSizes.Value,
                        isWinner ? Palette.Background : Palette.TextMuted, TextAnchor.MiddleRight, true);
                }
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
            }

            int buttonCount = App.FlowButtonCount;
            if (buttonCount > 0 && BallsNative.balls_flow_button(App.FlowHandle, 0, out NativeFlowButton button) != 0)
            {
                if (widgets.Button(new Rect((theme.UnitsWidth - 200f) * 0.5f, theme.UnitsHeight - 120f, 200f, 56f),
                        button.Label))
                {
                    App.ActivateFlow(button.action);
                }
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
            if (!recorded && BallsNative.balls_benchmark_finished(benchmark) != 0)
            {
                recorded = true;
                App.Profile.RememberBenchmark(benchmark, true);
                BallsNative.balls_flow_benchmark_finished(App.FlowHandle);
            }
            int currentWinner = BallsNative.balls_benchmark_winner(benchmark);
            if (currentWinner >= 0)
            {
                winner = currentWinner;
                if (!adopted)
                {
                    adopted = true;
                    AdoptWinner(winner);
                }
            }
            return Mathf.Min(count, rows.Length);
        }

        private void AdoptWinner(int win)
        {
            if (win >= 0)
            {
                if (App.Settings != null)
                {
                    App.Settings.DefaultBackend = BallsNative.StoredBackendName(win);
                    App.SaveSettings();
                }
                App.SetBackend(win);
                App.Toast($"Default backend: {BallsNative.BackendName(win)}");
            }
        }

        /// <summary>The running benchmark, for the control API; zero when none.</summary>
        public System.IntPtr Handle => benchmark;

        /// <summary>The control API's skip and continue: the same as the button.</summary>
        public void ControlContinue()
        {
            if (App.FlowButtonCount > 0 && BallsNative.balls_flow_button(App.FlowHandle, 0, out NativeFlowButton button) != 0)
            {
                App.ActivateFlow(button.action);
            }
        }
    }
}
