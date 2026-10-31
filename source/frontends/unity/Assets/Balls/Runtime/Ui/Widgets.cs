using System;
using System.Collections.Generic;
using UnityEngine;

namespace Balls.Ui
{
    /// <summary>
    /// The game's widget set, drawn in layout units. The same inventory the SDL front end has in
    /// source/frontends/sdl/ui/Widgets.h: buttons, option rows, dropdowns, steppers, text fields, scrolling frames,
    /// gadgets, toasts and section headings.
    /// </summary>
    public sealed class Widgets
    {
        public const float RowHeight = 44f;
        public const float RowGap = 10f;
        public const float ButtonHeight = 60f;
        public const float ButtonGap = 14f;
        public const float StepperHeight = 40f;

        private readonly UiTheme theme;
        private readonly Dictionary<string, bool> openDropdowns = new();
        private readonly Dictionary<string, string> edits = new();
        private string editingKey;

        public Widgets(UiTheme theme)
        {
            this.theme = theme;
        }

        public UiTheme Theme => theme;

        /// <summary>A full-width button. Returns true on click.</summary>
        public bool Button(Rect units, string label, int size = TypeSizes.Header, bool enabled = true)
        {
            // Buttons shrink their type rather than clip: the game's labels are exact strings, so the wording stays and
            // only the size gives way when a narrow gadget cannot hold it.
            size = FitSize(label, units.width - 16f, size);
            if (!enabled)
            {
                theme.Frame(units, Palette.Panel, Palette.Border);
                GUI.Label(theme.Units(units.x, units.y, units.width, units.height),
                    label, theme.TextStyle(size, Palette.TextDim, false, TextAnchor.MiddleCenter));
                return false;
            }
            var style = theme.ButtonStyle(size, Palette.Control, Palette.ControlHover, Palette.ControlPressed,
                Palette.Text);
            return GUI.Button(theme.Units(units.x, units.y, units.width, units.height), label, style);
        }

        /// <summary>A button whose clicked state is drawn with the accent fill, as the game's selected options are.</summary>
        public bool AccentButton(Rect units, string label, bool selected, int size = TypeSizes.Header)
        {
            Color fill = selected ? Palette.Accent : Palette.Control;
            Color text = selected ? Palette.Background : Palette.Text;
            var style = theme.ButtonStyle(size, fill, selected ? Palette.Accent : Palette.ControlHover,
                selected ? Palette.Accent : Palette.ControlPressed, text);
            return GUI.Button(theme.Units(units.x, units.y, units.width, units.height), label, style);
        }

        /// <summary>One row of a "few values worth seeing" choice: full width, selected one in accent.</summary>
        public bool OptionRow(Rect units, string label, bool selected, bool enabled = true, string detail = null)
        {
            if (!enabled && !selected)
            {
                OptionFace(units, label, false, false, detail);
                return false;
            }
            Color fill = selected ? Palette.Accent : Palette.Control;
            if (GUI.Button(theme.Units(units.x, units.y, units.width, units.height), GUIContent.none,
                    theme.ButtonStyle(TypeSizes.Header, fill, selected ? Palette.Accent : Palette.ControlHover,
                        Palette.ControlPressed, Palette.Text)))
            {
                return true;
            }
            OptionFace(units, label, selected, enabled, detail);
            return false;
        }

        /// <summary>
        /// Shortens text with an ellipsis until it fits the width, so a long level name keeps its tail visible
        /// instead of being cut off by the rect.
        /// </summary>
        public string Ellipsize(string text, int size, float widthUnits)
        {
            GUIStyle style = theme.TextStyle(size, Palette.Text, false, TextAnchor.MiddleRight, true);
            float pixels = widthUnits * theme.Scale;
            var content = new GUIContent(text);
            if (style.CalcSize(content).x <= pixels)
            {
                return text;
            }
            for (int length = text.Length - 1; length > 1; --length)
            {
                content.text = text.Substring(0, length) + "...";
                if (style.CalcSize(content).x <= pixels)
                {
                    break;
                }
            }
            return content.text;
        }

        private void OptionFace(Rect units, string label, bool selected, bool enabled, string detail)
        {
            Color text = selected ? Palette.Background : enabled ? Palette.Text : Palette.TextDim;
            float detailWidth = string.IsNullOrEmpty(detail) ? 0f : Mathf.Min(units.width * 0.52f, 260f);
            float labelWidth = units.width - 20f - detailWidth;
            GUI.Label(theme.Units(units.x + 10f, units.y, labelWidth, units.height), label,
                theme.TextStyle(TypeSizes.Header, text, false, TextAnchor.MiddleLeft, true));
            if (detailWidth > 0f)
            {
                detail = Ellipsize(detail, TypeSizes.Small, detailWidth);
                GUI.Label(theme.Units(units.xMax - 10f - detailWidth, units.y, detailWidth, units.height),
                    detail, theme.TextStyle(TypeSizes.Small, selected ? Palette.Background : Palette.TextMuted,
                        false, TextAnchor.MiddleRight, true));
            }
        }

        /// <summary>
        /// The game's dropdown: a field showing the current value with a chevron on the right, opening a vertical list
        /// under it. Returns true when the value changed.
        /// </summary>
        public bool Dropdown(string key, Rect units, string[] options, ref int index, int size = TypeSizes.Label)
        {
            bool changed = false;
            bool open = openDropdowns.TryGetValue(key, out bool isOpen) && isOpen;
            bool hovered = UnitsContains(units, Event.current.mousePosition);

            // As SDL's ui::dropdown: the board backdrop fill, accent border while hovered or open, and a square handle
            // on the right with a hairline before it and a triangle pointing down (up while open).
            theme.Frame(units, hovered ? Palette.ControlHover : Palette.BoardBackdrop,
                open || hovered ? Palette.Accent : Palette.Border);
            Rect handle = new Rect(units.xMax - units.height, units.y, units.height, units.height);
            theme.Fill(new Rect(handle.x, units.y + 6f, 1f, units.height - 12f), Palette.Border);
            Triangle(new Vector2(handle.center.x, handle.center.y), open ? -1 : 1,
                open || hovered ? Palette.Accent : Palette.TextMuted);
            GUI.Label(theme.Units(units.x + 12f, units.y, handle.x - units.x - 24f, units.height),
                index >= 0 && index < options.Length ? options[index] : "",
                theme.TextStyle(size, Palette.Text, false, TextAnchor.MiddleLeft, true));

            if (GUI.Button(theme.Units(units.x, units.y, units.width, units.height), GUIContent.none,
                    GUIStyle.none))
            {
                openDropdowns[key] = !open;
                open = !open;
            }

            if (open)
            {
                float rowHeight = units.height;
                for (int option = 0; option < options.Length; ++option)
                {
                    Rect row = new Rect(units.x, units.y + (rowHeight + RowGap) * (option + 1), units.width, rowHeight);
                    bool selected = option == index;
                    Color fill = selected ? Palette.Accent : Palette.Control;
                    if (GUI.Button(theme.Units(row.x, row.y, row.width, row.height), GUIContent.none,
                            theme.ButtonStyle(size, fill, selected ? Palette.Accent : Palette.ControlHover,
                                Palette.ControlPressed, Palette.Text)))
                    {
                        index = option;
                        changed = true;
                        openDropdowns[key] = false;
                    }
                    GUI.Label(theme.Units(row.x + 10f, row.y, row.width - 20f, row.height), options[option],
                        theme.TextStyle(size, selected ? Palette.Background : Palette.Text, false,
                            TextAnchor.MiddleLeft));
                }
            }
            return changed;
        }

        public bool IsDropdownOpen(string key) => openDropdowns.TryGetValue(key, out bool open) && open;

        /// <summary>Height a dropdown takes in a column: its row, plus its option list while open.</summary>
        public float DropdownHeight(string key, int optionCount) =>
            RowHeight + (IsDropdownOpen(key) ? (RowHeight + RowGap) * optionCount : 0f);

        /// <summary>
        /// A 12 x 7 triangle centred on a point, drawn as thin strips: direction 1 points down, -1 up, and 0 right
        /// (as a 7 x 12 triangle).
        /// </summary>
        public void Triangle(Vector2 center, int direction, Color color)
        {
            const float halfWidth = 6f;
            const float halfHeight = 3.5f;
            float strip = 1f / theme.Scale;
            if (direction == 0)
            {
                for (float x = -halfHeight; x < halfHeight; x += strip)
                {
                    float half = halfWidth * (halfHeight - (x + strip * 0.5f)) / (2f * halfHeight);
                    theme.Fill(new Rect(center.x + x, center.y - half, strip, half * 2f), color);
                }
                return;
            }
            for (float y = -halfHeight; y < halfHeight; y += strip)
            {
                float fromWide = direction > 0 ? y + halfHeight : halfHeight - y - strip;
                float half = halfWidth * (1f - (fromWide + strip * 0.5f) / (2f * halfHeight));
                theme.Fill(new Rect(center.x - half, center.y + y, half * 2f, strip), color);
            }
        }

        /// <summary>
        /// The game's collapsible section header, as SDL's ui::collapsingHeader: a hairline above, a chevron (right when
        /// closed, down when open) and the label at 20, hover fill and accent while hovered. Returns true on click.
        /// </summary>
        public bool CollapsingHeader(Rect units, string label, bool open)
        {
            bool hovered = UnitsContains(units, Event.current.mousePosition);
            if (hovered)
            {
                theme.Fill(units, Event.current.type == EventType.MouseDown ? Palette.ControlPressed : Palette.ControlHover);
            }
            theme.Fill(new Rect(units.x, units.y, units.width, 1f), Palette.Border);
            Color color = hovered ? Palette.Accent : Palette.Text;
            var center = new Vector2(units.x + 10f, units.y + units.height * 0.5f);
            Triangle(center, open ? 1 : 0, color);
            float labelX = center.x + 16f;
            GUI.Label(theme.Units(labelX, units.y, units.xMax - labelX, units.height), label,
                theme.TextStyle(TypeSizes.Label, color, false, TextAnchor.MiddleLeft, true));
            return GUI.Button(theme.Units(units.x, units.y, units.width, units.height), GUIContent.none, GUIStyle.none);
        }

        /// <summary>A muted group label (20) above a control group, as SDL's gameplay gadget writes them.</summary>
        public void GroupLabel(Rect units, string text)
        {
            Label(units, text, TypeSizes.Label, Palette.TextMuted, TextAnchor.UpperLeft);
        }

        /// <summary>A number with minus and plus buttons; steps by 1, or by 10 with Shift held.</summary>
        public bool Stepper(Rect units, string label, ref int value, int minimum, int maximum,
            out bool changed)
        {
            changed = false;
            const float buttonWidth = 40f;
            GUI.Label(theme.Units(units.x, units.y, units.width - buttonWidth * 2f - 90f, units.height),
                label, theme.TextStyle(TypeSizes.Small, Palette.TextMuted, false, TextAnchor.MiddleLeft));
            int step = Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift) ? 10 : 1;

            Rect minus = new Rect(units.x + units.width - buttonWidth * 2f - 90f, units.y, buttonWidth, units.height);
            Rect valueRect = new Rect(minus.xMax + 6f, units.y, 78f, units.height);
            Rect plus = new Rect(valueRect.xMax + 6f, units.y, buttonWidth, units.height);

            if (ButtonSquare(minus, "-"))
            {
                value = Mathf.Max(minimum, value - step);
                changed = true;
            }
            theme.Frame(valueRect, Palette.Background, Palette.Border);
            GUI.Label(theme.Units(valueRect.x, valueRect.y, valueRect.width, valueRect.height), value.ToString(),
                theme.TextStyle(TypeSizes.Header, Palette.Text, false, TextAnchor.MiddleCenter));
            if (ButtonSquare(plus, "+"))
            {
                value = Mathf.Min(maximum, value + step);
                changed = true;
            }
            return changed;
        }

        private int FitSize(string text, float widthUnits, int size)
        {
            int fitted = size;
            while (fitted > 12)
            {
                GUIStyle probe = theme.TextStyle(fitted, Palette.Text);
                if (probe.CalcSize(new GUIContent(text)).x / theme.Scale <= widthUnits)
                {
                    break;
                }
                --fitted;
            }
            return fitted;
        }

        private bool ButtonSquare(Rect units, string label)
        {
            return GUI.Button(theme.Units(units.x, units.y, units.width, units.height), label,
                theme.ButtonStyle(TypeSizes.Header, Palette.Control, Palette.ControlHover, Palette.ControlPressed,
                    Palette.Text));
        }

        /// <summary>A text field that commits on Enter or when focus leaves; the value is returned through the ref.</summary>
        public bool TextField(string key, Rect units, ref string value, int maxLength = 40)
        {
            if (!edits.TryGetValue(key, out string buffer))
            {
                buffer = value;
                edits[key] = buffer;
            }
            GUI.SetNextControlName(key);
            string typed = GUI.TextField(theme.Units(units.x, units.y, units.width, units.height), buffer,
                maxLength, theme.FieldStyle(TypeSizes.Header, Palette.Background, Palette.Border, Palette.Text));
            if (typed != buffer)
            {
                edits[key] = typed;
                buffer = typed;
            }
            bool committed = false;
            if (Event.current.type == EventType.KeyDown &&
                (Event.current.keyCode == KeyCode.Return || Event.current.keyCode == KeyCode.KeypadEnter))
            {
                committed = true;
                editingKey = null;
            }
            if (editingKey != key && GUI.GetNameOfFocusedControl() == key)
            {
                editingKey = key;
            }
            if (editingKey == key && GUI.GetNameOfFocusedControl() != key && Event.current.type == EventType.Repaint)
            {
                committed = true;
                editingKey = null;
            }
            if (committed)
            {
                value = buffer;
                edits[key] = buffer;
            }
            return committed;
        }

        public void ForgetTextField(string key)
        {
            edits.Remove(key);
        }

        /// <summary>A gadget: panel fill, one-unit border, an accent heading and a vertical column inside.</summary>
        public Rect Gadget(Rect units, string heading)
        {
            theme.Frame(units, Palette.Panel, Palette.Border);
            const float inset = 12f;
            Rect inner = new Rect(units.x + inset, units.y + inset, units.width - inset * 2f, units.height - inset * 2f);
            if (!string.IsNullOrEmpty(heading))
            {
                GUI.Label(theme.Units(inner.x, inner.y, inner.width, 24f), heading,
                    theme.TextStyle(TypeSizes.Value, Palette.Accent, true));
                inner.y += 26f;
                inner.height -= 26f;
            }
            return inner;
        }

        public void Label(Rect units, string text, int size = TypeSizes.Label, Color? color = null,
            TextAnchor anchor = TextAnchor.UpperLeft, bool bold = false)
        {
            GUI.Label(theme.Units(units.x, units.y, units.width, units.height), text,
                theme.TextStyle(size, color ?? Palette.Text, bold, anchor));
        }

        public void Muted(Rect units, string text, int size = TypeSizes.Small,
            TextAnchor anchor = TextAnchor.UpperLeft)
        {
            Label(units, text, size, Palette.TextMuted, anchor);
        }

        /// <summary>A two-line readout: muted label above, value under it, as the debug gadget uses.</summary>
        public void Readout(Rect units, string label, string value, Color? valueColor = null)
        {
            Label(new Rect(units.x, units.y, units.width, 18f), label, TypeSizes.Small, Palette.TextMuted);
            Label(new Rect(units.x, units.y + 18f, units.width, 24f), value, TypeSizes.Value, valueColor ?? Palette.Text,
                TextAnchor.UpperLeft, true);
        }

        /// <summary>
        /// A gadget that scrolls when its content is taller than it. The design has no scrollbars: an edge with more
        /// content beyond it fades into the panel over 40 units under a faded arrow pointing that way, so at the start
        /// only the far edge shows one and at the end only the near edge. The wheel and a drag both scroll.
        /// </summary>
        public void ScrollColumn(Rect viewport, float contentHeight, ref Vector2 scroll, Action<Rect> draw)
        {
            float max = Mathf.Max(0f, contentHeight - viewport.height);
            bool inside = UnitsContains(viewport, Event.current.mousePosition);
            if (max > 0f && inside)
            {
                if (Event.current.type == EventType.ScrollWheel)
                {
                    scroll.y = Mathf.Clamp(scroll.y + Event.current.delta.y * 24f, 0f, max);
                }
                else if (Event.current.type == EventType.MouseDrag && Event.current.button == 0)
                {
                    scroll.y = Mathf.Clamp(scroll.y - Event.current.delta.y / theme.Scale, 0f, max);
                }
            }
            scroll.y = Mathf.Clamp(scroll.y, 0f, max);
            var content = new Rect(viewport.x, viewport.y - scroll.y, viewport.width, contentHeight);
            if (max > 0f)
            {
                // Clip to the viewport but keep absolute coordinates, so the callback lays out as if nothing scrolled.
                Rect pixels = theme.Units(viewport.x, viewport.y, viewport.width, viewport.height);
                GUI.BeginClip(pixels, -pixels.position, Vector2.zero, false);
                try
                {
                    draw(content);
                }
                finally
                {
                    GUI.EndClip();
                }
                if (scroll.y > 0.5f)
                {
                    DrawScrollEdge(viewport, true, Palette.Panel);
                }
                if (scroll.y < max - 0.5f)
                {
                    DrawScrollEdge(viewport, false, Palette.Panel);
                }
            }
            else
            {
                draw(content);
            }
        }

        /// <summary>A 40-unit fade into the panel colour with a 20 x 10 arrow in the text colour at 55%.</summary>
        private void DrawScrollEdge(Rect viewport, bool top, Color background)
        {
            const float fade = 40f;
            const int steps = 20;
            float stepHeight = fade / steps;
            for (int index = 0; index < steps; ++index)
            {
                float share = (index + 1f) / steps;
                float y = top ? viewport.y + index * stepHeight : viewport.yMax - (index + 1) * stepHeight;
                theme.Fill(new Rect(viewport.x, y, viewport.width, stepHeight + 0.5f),
                    UiTheme.WithAlpha(background, share));
            }
            Color arrow = UiTheme.WithAlpha(Palette.Text, 0.55f);
            float arrowWidth = 20f;
            float arrowHeight = 10f;
            float arrowY = top ? viewport.y + 6f : viewport.yMax - 6f - arrowHeight;
            float arrowX = viewport.x + (viewport.width - arrowWidth) * 0.5f;
            // A stepped triangle: ten rows, each narrower towards the point.
            for (int row = 0; row < (int)arrowHeight; ++row)
            {
                float share = top ? (row + 1f) / arrowHeight : 1f - row / arrowHeight;
                float width = arrowWidth * share;
                theme.Fill(new Rect(arrowX + (arrowWidth - width) * 0.5f, arrowY + row, width, 1f), arrow);
            }
        }

        private bool UnitsContains(Rect units, Vector2 pixels)
        {
            Rect screen = theme.Units(units.x, units.y, units.width, units.height);
            return screen.Contains(pixels);
        }

        public void Toast(Rect units, string text, bool error = false)
        {
            var style = theme.TextStyle(TypeSizes.Body, error ? Palette.Danger : Palette.Text, true,
                TextAnchor.MiddleCenter);
            Vector2 size = style.CalcSize(new GUIContent(text));
            float width = Mathf.Min(units.width, size.x / theme.Scale + 40f);
            Rect box = new Rect(units.x + (units.width - width) * 0.5f, units.y, width, 40f);
            theme.Fill(box, error ? Palette.Danger : Palette.Control);
            GUI.Label(theme.Units(box.x, box.y, box.width, box.height), text,
                theme.TextStyle(TypeSizes.Body, error ? Palette.Background : Palette.Text, true,
                    TextAnchor.MiddleCenter));
        }
    }
}
