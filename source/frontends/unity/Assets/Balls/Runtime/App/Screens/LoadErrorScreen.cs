using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{

    public sealed class LoadErrorScreen : Screen
    {
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

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = "Level failed to load";
            }
            string message = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextLoadError);
            string footnote = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextFootnote);
            if (string.IsNullOrEmpty(footnote))
            {
                footnote = "Fix the file and save - it reloads automatically";
            }

            float margin = 32f;
            float y = height * 0.28f;
            widgets.Label(new Rect(margin, y, width - 2f * margin, 56f), title, TypeSizes.Heading,
                Palette.Danger, TextAnchor.MiddleCenter, true);
            y += 72f;
            widgets.Label(new Rect(margin, y, width - 2f * margin, 120f), message, TypeSizes.Header,
                Palette.Text, TextAnchor.UpperCenter);
            y += 132f;
            widgets.Muted(new Rect(margin, y, width - 2f * margin, 30f), footnote,
                TypeSizes.Header, TextAnchor.MiddleCenter);
            y += 64f;

            int buttonCount = App.FlowButtonCount;
            float buttonWidth = 240f;
            for (int index = 0; index < buttonCount; ++index)
            {
                if (BallsNative.balls_flow_button(App.FlowHandle, index, out NativeFlowButton button) != 0)
                {
                    if (widgets.Button(new Rect((width - buttonWidth) * 0.5f, y, buttonWidth, 56f), button.Label, TypeSizes.Header, button.enabled != 0))
                    {
                        App.ActivateFlow(button.action);
                        return;
                    }
                    y += 64f;
                }
            }
        }
    }
}
