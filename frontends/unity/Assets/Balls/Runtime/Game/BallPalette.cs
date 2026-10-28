using UnityEngine;

namespace Balls.Game
{
    /// <summary>The seven ball colours, in the core's BallColor order.</summary>
    public static class BallPalette
    {
        public const int ColorCount = 7;

        public static readonly Color[] Colors =
        {
            new Color(0.86f, 0.20f, 0.24f), // red
            new Color(0.96f, 0.53f, 0.16f), // orange
            new Color(0.97f, 0.85f, 0.22f), // yellow
            new Color(0.32f, 0.76f, 0.36f), // green
            new Color(0.23f, 0.52f, 0.90f), // blue
            new Color(0.36f, 0.32f, 0.78f), // indigo
            new Color(0.66f, 0.35f, 0.82f), // violet
        };

        public static Color For(int color)
        {
            return color >= 0 && color < ColorCount ? Colors[color] : Color.magenta;
        }

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
