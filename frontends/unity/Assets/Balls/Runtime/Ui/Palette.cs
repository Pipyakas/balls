using UnityEngine;

namespace Balls.Ui
{
    /// <summary>
    /// The game's one UI scheme, copied from source/frontends/sdl/render/Palette.h so every port looks the same.
    /// Sizes are layout units: 1 unit = 1 pixel at 1440x900 with 100% DPI scale.
    /// </summary>
    public static class Palette
    {
        public static readonly Color Background = Hex(0x161616);
        public static readonly Color BoardBackdrop = Hex(0x101010);
        public static readonly Color Panel = Hex(0x1D1D1D);
        public static readonly Color Control = Hex(0x242424);
        public static readonly Color ControlHover = Hex(0x303030);
        public static readonly Color ControlPressed = Hex(0x3A3A3A);
        public static readonly Color Border = new Color(1f, 1f, 1f, 0.10f);
        public static readonly Color Text = Hex(0xD4D4D4);
        public static readonly Color TextStrong = Hex(0xFFFFFF);
        public static readonly Color TextMuted = Hex(0xA3A3A3);
        public static readonly Color TextDim = Hex(0x707070);
        public static readonly Color Accent = Hex(0x8190FF);
        public static readonly Color Danger = Hex(0xEB505A);
        public static readonly Color Success = Hex(0x5AD278);

        /// <summary>Ball colours in level-file order; a brick is its ball colour times 0.92.</summary>
        public static readonly Color[] Balls =
        {
            Rgb(235, 64, 72),
            Rgb(255, 150, 50),
            Rgb(250, 220, 60),
            Rgb(80, 205, 90),
            Rgb(60, 150, 245),
            Rgb(100, 90, 220),
            Rgb(190, 90, 230),
        };

        public const float BrickDim = 0.92f;

        public static Color Brick(int color) => color >= 0 && color < Balls.Length ? Balls[color] * BrickDim : Color.magenta;

        public static Color Hex(int rgb) => Rgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);

        public static Color Rgb(int r, int g, int b) => new Color(r / 255f, g / 255f, b / 255f, 1f);
    }

    /// <summary>The game's type scale, in layout units.</summary>
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
    }
}
