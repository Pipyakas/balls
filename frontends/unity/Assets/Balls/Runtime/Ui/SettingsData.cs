using System;
using System.Text;
using Balls.Native;

namespace Balls.Ui
{
    /// <summary>
    /// The game's settings, held in the form the core's settings file uses so the other ports read the same values.
    /// </summary>
    public sealed class SettingsData
    {
        public const int OrientationAuto = 0;
        public const int OrientationLandscape = 1;
        public const int OrientationPortrait = 2;

        private readonly string path;

        public SettingsData(NativeSettings native, string path)
        {
            this.path = path;
            BallCollisions = native.ballCollisions != 0;
            Orientation = native.orientation;
            Vsync = native.vsync != 0;
            DefaultBackend = native.DefaultBackend;
        }

        public bool BallCollisions { get; set; }

        public int Orientation { get; set; }

        public bool Vsync { get; set; }

        public string DefaultBackend { get; set; } = "";

        public int BackendKind(int fallback)
        {
            if (string.IsNullOrEmpty(DefaultBackend))
            {
                return fallback;
            }
            for (int kind = 0; kind < BallsNative.BackendCount; ++kind)
            {
                if (BallsNative.BackendName(kind).Equals(DefaultBackend, StringComparison.OrdinalIgnoreCase))
                {
                    return BallsNative.balls_backend_available(kind) != 0 ? kind : fallback;
                }
            }
            return fallback;
        }

        public string OrientationName => Orientation switch
        {
            OrientationLandscape => "Landscape",
            OrientationPortrait => "Portrait",
            _ => "Auto",
        };

        public void Write()
        {
            var native = new NativeSettings
            {
                ballCollisions = BallCollisions ? 1 : 0,
                orientation = Orientation,
                vsync = Vsync ? 1 : 0,
                defaultBackend = new byte[64],
            };
            byte[] name = Encoding.UTF8.GetBytes(DefaultBackend ?? "");
            Array.Copy(name, native.defaultBackend, Math.Min(name.Length, 63));
            BallsNative.balls_settings_save(path, ref native);
        }
    }
}
