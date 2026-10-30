using UnityEngine;

namespace Balls.Game
{
    /// <summary>The seven ball colours, in the core's BallColor order.</summary>
    public static class BallPalette
    {
        public const int ColorCount = 7;

        /// <summary>The core theme's ball colour (see <see cref="Ui.Palette"/>).</summary>
        public static Color For(int color) => Ui.Palette.Ball(color);

        public static string Name(int color)
        {
            return color switch
            {
                0 => "Red",
                1 => "Orange",
                2 => "Yellow",
                3 => "Green",
                4 => "Blue",
                5 => "Indigo",
                6 => "Violet",
                _ => "?",
            };
        }
    }
}
