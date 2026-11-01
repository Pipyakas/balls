using Balls.Native;
using UnityEngine;

namespace Balls.Ui
{

    public static class Palette
    {
        public static Color Background = Color.black;
        public static Color BoardBackdrop = Color.black;
        public static Color Panel = Color.black;
        public static Color Control = Color.gray;
        public static Color ControlHover = Color.gray;
        public static Color ControlPressed = Color.gray;
        public static Color Border = Color.gray;
        public static Color Text = Color.white;
        public static Color TextStrong = Color.white;
        public static Color TextMuted = Color.gray;
        public static Color TextDim = Color.gray;
        public static Color Accent = Color.white;
        public static Color Danger = Color.white;
        public static Color Success = Color.white;

        public static readonly Color[] Balls = new Color[BallColorCount];

        private static readonly Color[] Bricks = new Color[BallColorCount];

        public const int BallColorCount = 7;

        public static bool Loaded { get; private set; }

        public static float ToastSeconds = 3f;

        public static Color Brick(int color) => color >= 0 && color < Bricks.Length ? Bricks[color] : Color.magenta;

        public static Color Ball(int color) => color >= 0 && color < Balls.Length ? Balls[color] : Color.magenta;

        public static void LoadFromCore()
        {
            Background = Core(BallsNative.ThemeBackground);
            BoardBackdrop = Core(BallsNative.ThemeBoardBackdrop);
            Panel = Core(BallsNative.ThemePanel);
            Control = Core(BallsNative.ThemeControl);
            ControlHover = Core(BallsNative.ThemeControlHover);
            ControlPressed = Core(BallsNative.ThemeControlPressed);
            Border = Core(BallsNative.ThemeBorder);
            Text = Core(BallsNative.ThemeText);
            TextStrong = Core(BallsNative.ThemeTextStrong);
            TextMuted = Core(BallsNative.ThemeTextMuted);
            TextDim = Core(BallsNative.ThemeTextDim);
            Accent = Core(BallsNative.ThemeAccent);
            Danger = Core(BallsNative.ThemeDanger);
            Success = Core(BallsNative.ThemeSuccess);
            for (int color = 0; color < BallColorCount; ++color)
            {
                Balls[color] = Unpack(BallsNative.balls_theme_ball_color(color));
                Bricks[color] = Unpack(BallsNative.balls_theme_brick_color(color));
            }
            ToastSeconds = BallsNative.balls_theme_value(BallsNative.ThemeToastSeconds);
            Loaded = true;
            TypeSizes.CheckAgainstCore();
        }

        private static Color Core(int id) => Unpack(BallsNative.balls_theme_color(id));

        private static Color Unpack(uint rgba) =>
            new Color32((byte)(rgba >> 24), (byte)(rgba >> 16), (byte)(rgba >> 8), (byte)rgba);
    }

    public static class TypeSizes
    {
        public const int Display = 72;
        public const int Title = 52;
        public const int Large = 44;
        public const int Heading = 40;
        public const int Big = 28;
        public const int Medium = 26;
        public const int Body = 24;
        public const int Header = 22;
        public const int Label = 20;
        public const int Value = 18;
        public const int Small = 16;
        public const int Tiny = 15;

        internal static void CheckAgainstCore()
        {
            int[] sizes = { Display, Title, Large, Heading, Big, Medium, Body, Header, Label, Value, Small, Tiny };
            for (int index = 0; index < sizes.Length; ++index)
            {
                float core = BallsNative.balls_theme_value(BallsNative.ThemeSizeDisplay + index);
                if (!Mathf.Approximately(core, sizes[index]))
                {
                    Debug.LogError($"balls: type size {sizes[index]} differs from the core theme's {core}");
                }
            }
        }
    }
}
