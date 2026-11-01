using System;
using System.Text;
using Balls.Native;

namespace Balls.Ui
{

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
            AdvancedSettings = native.advancedSettings != 0;
            DefaultBackend = native.DefaultBackend;
        }

        public bool BallCollisions { get; set; }

        public int Orientation { get; set; }

        public bool Vsync { get; set; }

        public bool AdvancedSettings { get; set; }

        public string DefaultBackend { get; set; } = "";

        public int BackendKind(int fallback)
        {
            if (string.IsNullOrEmpty(DefaultBackend))
            {
                return fallback;
            }

            int kind = BallsNative.balls_backend_parse_saved(DefaultBackend);
            return kind >= 0 && BallsNative.balls_backend_available(kind) != 0 ? kind : fallback;
        }

        public string OrientationName => Orientation switch
        {
            OrientationLandscape => "Landscape",
            OrientationPortrait => "Portrait",
            _ => "Auto",
        };

        public void Write()
        {
            NativeSettings native = ToNative();
            BallsNative.balls_settings_save(path, ref native);
        }

        public NativeSettings ToNative()
        {
            var native = new NativeSettings
            {
                ballCollisions = BallCollisions ? 1 : 0,
                orientation = Orientation,
                vsync = Vsync ? 1 : 0,
                advancedSettings = AdvancedSettings ? 1 : 0,
                defaultBackend = new byte[64],
            };
            byte[] name = Encoding.UTF8.GetBytes(DefaultBackend ?? "");
            Array.Copy(name, native.defaultBackend, Math.Min(name.Length, 63));
            return native;
        }
    }
}
