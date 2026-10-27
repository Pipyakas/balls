using UnityEngine;

namespace Balls.Ui
{
    /// <summary>
    /// Layout units, fonts and the shared styles. One layout unit is one pixel at a 1440x900 window with 100% DPI
    /// scale, exactly as the design measures it; everything in the front end is written in those units and scaled once.
    /// </summary>
    public sealed class UiTheme
    {
        public const float ReferenceHeight = 900f;

        private static readonly Color Transparent = new Color(0f, 0f, 0f, 0f);

        private Font regular;
        private Font semibold;
        private Texture2D white;
        private readonly System.Collections.Generic.Dictionary<(int size, Color color, bool bold, TextAnchor anchor, bool clip), GUIStyle> textStyles = new();
        private readonly System.Collections.Generic.Dictionary<(Color fill, Color edge), Texture2D> solids = new();

        public float Scale { get; private set; } = 1f;

        public float UnitsWidth { get; private set; } = 1440f;

        public float UnitsHeight { get; private set; } = 900f;

        public Texture2D White => white;

        public void Resize(int screenWidth, int screenHeight)
        {
            if (white == null)
            {
                white = Texture2D.whiteTexture;
            }
            if (regular == null)
            {
                regular = Resources.Load<Font>("Fonts/IBMPlexMono-Regular");
            }
            if (semibold == null)
            {
                semibold = Resources.Load<Font>("Fonts/IBMPlexMono-SemiBold") ?? regular;
            }
            Scale = Mathf.Max(0.35f, screenHeight / ReferenceHeight);
            UnitsWidth = screenWidth / Scale;
            UnitsHeight = screenHeight / Scale;
        }

        public Rect Units(float x, float y, float width, float height) =>
            new Rect(x * Scale, y * Scale, width * Scale, height * Scale);

        public GUIStyle TextStyle(int size, Color color, bool bold = false, TextAnchor anchor = TextAnchor.UpperLeft,
            bool clip = false)
        {
            var key = (size, color, bold, anchor, clip);
            if (textStyles.TryGetValue(key, out GUIStyle cached) && cached.fontSize > 0)
            {
                cached.font = bold ? semibold : regular;
                return cached;
            }
            var style = new GUIStyle(GUI.skin.label)
            {
                font = bold ? semibold : regular,
                fontSize = Mathf.Max(8, Mathf.RoundToInt(size * Scale)),
                alignment = anchor,
                wordWrap = false,
                richText = false,
                padding = new RectOffset(0, 0, 0, 0),
                margin = new RectOffset(0, 0, 0, 0),
                clipping = clip ? TextClipping.Clip : TextClipping.Overflow,
            };
            style.normal.textColor = color;
            style.hover.textColor = color;
            style.active.textColor = color;
            if (style.font == null)
            {
                // Before the font assets exist (or in a build where they were not copied) IMGUI's default still
                // draws readable text.
                style.font = null;
            }
            textStyles[key] = style;
            return style;
        }

        public GUIStyle ButtonStyle(int size, Color background, Color hover, Color pressed, Color textColor, bool bold = false)
        {
            var style = new GUIStyle(GUI.skin.button)
            {
                font = bold ? semibold : regular,
                fontSize = Mathf.Max(8, Mathf.RoundToInt(size * Scale)),
                alignment = TextAnchor.MiddleCenter,
                border = new RectOffset(1, 1, 1, 1),
                padding = new RectOffset(6, 6, 2, 2),
                margin = new RectOffset(0, 0, 0, 0),
                richText = false,
            };
            style.normal.background = Solid(background);
            style.hover.background = Solid(hover);
            style.active.background = Solid(pressed);
            style.focused.background = Solid(background);
            style.normal.textColor = textColor;
            style.hover.textColor = textColor;
            style.active.textColor = textColor;
            style.focused.textColor = textColor;
            return style;
        }

        public GUIStyle FieldStyle(int size, Color background, Color borderColor, Color textColor)
        {
            var style = new GUIStyle(GUI.skin.textField)
            {
                font = regular,
                fontSize = Mathf.Max(8, Mathf.RoundToInt(size * Scale)),
                alignment = TextAnchor.MiddleLeft,
                border = new RectOffset(1, 1, 1, 1),
                padding = new RectOffset(8, 8, 2, 2),
                margin = new RectOffset(0, 0, 0, 0),
            };
            style.normal.background = Solid(background);
            style.normal.textColor = textColor;
            style.focused.background = Solid(background);
            style.focused.textColor = Palette.TextStrong;
            style.hover.background = Solid(background);
            return style;
        }

        public GUIStyle PanelStyle(Color fill, Color borderColor)
        {
            var style = new GUIStyle(GUI.skin.box)
            {
                border = new RectOffset(1, 1, 1, 1),
                padding = new RectOffset(0, 0, 0, 0),
                margin = new RectOffset(0, 0, 0, 0),
            };
            style.normal.background = Solid(fill, borderColor);
            return style;
        }

        /// <summary>A flat fill, or a 1-unit border drawn over a fill when a border colour is given.</summary>
        public Texture2D Solid(Color fill, Color? border = null)
        {
            Color edge = border ?? fill;
            if (solids.TryGetValue((fill, edge), out Texture2D cached))
            {
                return cached;
            }
            var texture = new Texture2D(3, 3, TextureFormat.RGBA32, false)
            {
                hideFlags = HideFlags.HideAndDontSave,
                filterMode = FilterMode.Point,
                wrapMode = TextureWrapMode.Clamp,
            };
            for (int y = 0; y < 3; ++y)
            {
                for (int x = 0; x < 3; ++x)
                {
                    bool isEdge = x == 0 || y == 0 || x == 2 || y == 2;
                    texture.SetPixel(x, y, isEdge ? edge : fill);
                }
            }
            texture.Apply();
            solids[(fill, edge)] = texture;
            return texture;
        }

        /// <summary>A white disc with a soft edge, tinted by GUI.color — how the UI draws balls.</summary>
        public Texture2D Disc
        {
            get
            {
                if (disc != null)
                {
                    return disc;
                }
                const int size = 64;
                disc = new Texture2D(size, size, TextureFormat.RGBA32, false)
                {
                    hideFlags = HideFlags.HideAndDontSave,
                    filterMode = FilterMode.Bilinear,
                    wrapMode = TextureWrapMode.Clamp,
                };
                float half = size * 0.5f;
                for (int y = 0; y < size; ++y)
                {
                    for (int x = 0; x < size; ++x)
                    {
                        float dx = (x + 0.5f - half) / half;
                        float dy = (y + 0.5f - half) / half;
                        float radius = Mathf.Sqrt(dx * dx + dy * dy);
                        float alpha = Mathf.Clamp01((1f - radius) * half);
                        disc.SetPixel(x, y, new Color(1f, 1f, 1f, alpha));
                    }
                }
                disc.Apply();
                return disc;
            }
        }

        private Texture2D disc;

        public void FillDisc(Rect units, Color color)
        {
            var previous = GUI.color;
            GUI.color = color;
            GUI.DrawTexture(Units(units.x, units.y, units.width, units.height), Disc, ScaleMode.StretchToFill);
            GUI.color = previous;
        }

        public void Fill(Rect units, Color color)
        {
            var previous = GUI.color;
            GUI.color = color;
            GUI.DrawTexture(Units(units.x, units.y, units.width, units.height), white, ScaleMode.StretchToFill);
            GUI.color = previous;
        }

        public void Frame(Rect units, Color fill, Color border)
        {
            Fill(units, border);
            Fill(new Rect(units.x + 1f, units.y + 1f, units.width - 2f, units.height - 2f), fill);
        }

        public static Color Dim(Color color, float alpha)
        {
            return new Color(color.r, color.g, color.b, alpha);
        }

        public static Color Lighten(Color color, float amount)
        {
            return new Color(
                Mathf.Lerp(color.r, 1f, amount),
                Mathf.Lerp(color.g, 1f, amount),
                Mathf.Lerp(color.b, 1f, amount),
                color.a);
        }

        public static Color WithAlpha(Color color, float alpha) => new Color(color.r, color.g, color.b, alpha);

        public static readonly Color Clear = Transparent;
    }
}
