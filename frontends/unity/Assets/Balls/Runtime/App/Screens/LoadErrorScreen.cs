using Balls.Ui;
using UnityEngine;

namespace Balls.App.Screens
{
    /// <summary>
    /// The game's load error: shown instead of Gameplay when a level cannot be parsed, and retried whenever the level's
    /// files change on disk.
    /// </summary>
    public sealed class LoadErrorScreen : Screen
    {
        private readonly LevelWatcher watcher = new();
        private string message = "";

        public void Show(string error)
        {
            message = error;
            watcher.Watch(App.LevelDirectory, App.LevelName);
        }

        public override void HandleEscape()
        {
            App.Go<LevelSelectScreen>();
        }

        public override void Draw()
        {
            UiTheme theme = App.Theme;
            Widgets widgets = App.Widgets;
            float width = theme.UnitsWidth;
            float height = theme.UnitsHeight;
            theme.Fill(new Rect(0f, 0f, width, height), Palette.Background);

            float margin = 32f;
            float y = height * 0.28f;
            widgets.Label(new Rect(margin, y, width - 2f * margin, 56f), "Level failed to load", TypeSizes.Heading,
                Palette.Danger, TextAnchor.MiddleCenter, true);
            y += 72f;
            widgets.Label(new Rect(margin, y, width - 2f * margin, 120f), message, TypeSizes.Header,
                Palette.Text, TextAnchor.UpperCenter);
            y += 132f;
            widgets.Muted(new Rect(margin, y, width - 2f * margin, 30f), "Fix the file and save - it reloads automatically",
                TypeSizes.Header, TextAnchor.MiddleCenter);
            y += 64f;
            if (widgets.Button(new Rect((width - 240f) * 0.5f, y, 240f, 56f), "Back"))
            {
                App.Go<LevelSelectScreen>();
                return;
            }

            if (watcher.Poll() != LevelWatcher.State.Unchanged)
            {
                App.StartGameplay(App.Mode, App.LevelName, App.BackendKind);
            }
        }
    }
}
