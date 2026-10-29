using Balls.Native;
using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's level select: the mode's folder listed live, with buttons and level rows provided by the core flow.
    /// </summary>
    public sealed class LevelSelectScreen : Screen
    {
        private const float RowHeight = 58f;
        private const float RowGap = 12f;
        private const float ListWidth = 560f;

        private Vector2 scroll;

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

            int scrollToRow = BallsNative.balls_flow_take_scroll_to_row(App.FlowHandle);
            if (scrollToRow >= 0)
            {
                scroll.y = scrollToRow * (RowHeight + RowGap);
            }

            string title = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextTitle);
            if (string.IsNullOrEmpty(title))
            {
                title = App.Mode == BallsApp.PerformanceMode ? "Performance Mode" : "Normal Mode";
            }
            widgets.Label(new Rect(0f, 48f, width, 60f), title, TypeSizes.Title, Palette.Accent,
                TextAnchor.MiddleCenter, true);

            int levelCount = App.FlowLevelRowCount;
            float listWidth = Mathf.Min(ListWidth, width - 64f);
            float listX = (width - listWidth) * 0.5f;
            float listTop = 150f;
            float listHeight = Mathf.Min(levelCount * (RowHeight + RowGap), height - listTop - 240f);
            Rect viewport = new Rect(listX, listTop, listWidth, Mathf.Max(listHeight, RowHeight));

            if (levelCount == 0)
            {
                widgets.Muted(new Rect(listX, listTop + 20f, listWidth, 32f), "No levels found", TypeSizes.Big,
                    TextAnchor.MiddleCenter);
            }
            else
            {
                Rect content = new Rect(viewport.x, viewport.y - scroll.y, viewport.width,
                    levelCount * (RowHeight + RowGap));
                scroll.y = Mathf.Clamp(scroll.y, 0f, Mathf.Max(0f, content.height - viewport.height));
                for (int index = 0; index < levelCount; ++index)
                {
                    if (BallsNative.balls_flow_level_row(App.FlowHandle, index, out NativeFlowLevelRow row) != 0)
                    {
                        Rect rowRect = new Rect(content.x, content.y + index * (RowHeight + RowGap), content.width, RowHeight);
                        if (rowRect.yMax >= viewport.y - RowHeight && rowRect.y <= viewport.y + viewport.height)
                        {
                            DrawRow(widgets, rowRect, row.Name, row.highlighted != 0, index);
                        }
                    }
                }
            }

            float buttonY = Mathf.Min(height - 150f, listTop + viewport.height + 24f);
            float buttonWidth = 200f;
            float gap = 12f;
            int buttonCount = App.FlowButtonCount;
            float total = buttonCount * buttonWidth + Mathf.Max(0, buttonCount - 1) * gap;
            float startX = (width - total) * 0.5f;

            for (int index = 0; index < buttonCount; ++index)
            {
                if (BallsNative.balls_flow_button(App.FlowHandle, index, out NativeFlowButton button) != 0)
                {
                    Rect btnRect = new Rect(startX + index * (buttonWidth + gap), buttonY, buttonWidth, 56f);
                    if (widgets.Button(btnRect, button.Label, TypeSizes.Header, button.enabled != 0))
                    {
                        App.ActivateFlow(button.action);
                    }
                }
            }

            string footnote = BallsNative.FlowText(App.FlowHandle, BallsNative.FlowTextFootnote);
            if (!string.IsNullOrEmpty(footnote))
            {
                widgets.Muted(new Rect(0f, buttonY + 72f, width, 24f), footnote,
                    TypeSizes.Small, TextAnchor.MiddleCenter);
            }
        }

        private void DrawRow(Widgets widgets, Rect row, string name, bool fresh, int rowIndex)
        {
            Rect edit = new Rect(row.xMax - 110f, row.y, 110f, row.height);
            Rect label = new Rect(row.x, row.y, row.width - 110f - 8f, row.height);
            bool clicked = fresh
                ? widgets.AccentButton(label, name, true, TypeSizes.Header)
                : widgets.Button(label, name, TypeSizes.Header);
            if (clicked)
            {
                App.ActivateFlow(BallsNative.FlowPlayLevel, rowIndex);
            }
            if (widgets.Button(edit, "Edit", TypeSizes.Label))
            {
                App.ActivateFlow(BallsNative.FlowEditLevel, rowIndex);
            }
        }
    }
}
