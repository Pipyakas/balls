using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{

    public sealed class MainMenuScreen : Screen
    {
        private const int BallCount = 28;
        private const float ButtonWidth = 380f;
        private const float ButtonHeight = 64f;
        private const float ButtonGap = 18f;

        private struct Drifter
        {
            public Vector2 position;
            public Vector2 velocity;
            public int color;
            public float radius;
        }

        private readonly Drifter[] drifters = new Drifter[BallCount];

        public override void Enter()
        {
            var random = new System.Random(20261028);
            for (int index = 0; index < BallCount; ++index)
            {
                drifters[index] = new Drifter
                {
                    position = new Vector2((float)random.NextDouble() * 1440f, (float)random.NextDouble() * 900f),
                    velocity = new Vector2((float)(random.NextDouble() - 0.5) * 24f, (float)(random.NextDouble() - 0.5) * 18f),
                    color = index % Palette.Balls.Length,
                    radius = 12f + (float)random.NextDouble() * 26f,
                };
            }
        }

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
            theme.Fill(new Rect(0f, 0f, width, height), Palette.Background);

            DrawDrifters(theme, width, height);

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = "BALLS";
            }
            widgets.Label(new Rect(0f, height * 0.16f, width, 90f), title, TypeSizes.Title,
                Palette.Accent, TextAnchor.MiddleCenter, true);

            float columnWidth = Mathf.Min(ButtonWidth, width - 48f);
            float x = (width - columnWidth) * 0.5f;
            float y = height * 0.16f + 130f;

            int buttonCount = App.FlowButtonCount;
            for (int index = 0; index < buttonCount; ++index)
            {
                if (BallsNative.balls_flow_button(App.FlowHandle, index, out NativeFlowButton button) != 0)
                {
                    if (widgets.Button(new Rect(x, y, columnWidth, ButtonHeight), button.Label, TypeSizes.Header, button.enabled != 0))
                    {
                        App.ActivateFlow(button.action);
                    }
                    y += ButtonHeight + ButtonGap;
                }
            }

            string footnote = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextFootnote);
            if (string.IsNullOrEmpty(footnote))
            {
                footnote = $"Unity ECS front end · {Application.version} · {SystemInfo.graphicsDeviceName}";
            }
            widgets.Muted(new Rect(0f, height - 40f, width, 24f), footnote,
                TypeSizes.Small, TextAnchor.MiddleCenter);
        }

        private void DrawDrifters(UiTheme theme, float width, float height)
        {
            float delta = Time.unscaledDeltaTime;
            for (int index = 0; index < drifters.Length; ++index)
            {
                ref Drifter ball = ref drifters[index];
                ball.position += ball.velocity * delta;
                if (ball.position.x < -60f)
                {
                    ball.position.x = width + 60f;
                }
                if (ball.position.x > width + 60f)
                {
                    ball.position.x = -60f;
                }
                if (ball.position.y < -60f)
                {
                    ball.position.y = height + 60f;
                }
                if (ball.position.y > height + 60f)
                {
                    ball.position.y = -60f;
                }
                Color color = Palette.Balls[ball.color];
                theme.FillDisc(new Rect(ball.position.x - ball.radius, ball.position.y - ball.radius,
                    ball.radius * 2f, ball.radius * 2f), UiTheme.WithAlpha(color, 0.22f));
            }
        }
    }
}
