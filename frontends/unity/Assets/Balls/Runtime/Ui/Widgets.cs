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
            if (!enabled)
            {
                theme.Fill(units, Palette.Control);
                theme.Frame(units, Palette.Control, Palette.Border);
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

        private void OptionFace(Rect units, string label, bool selected, bool enabled, string detail)
        {
            Color text = selected ? Palette.Background : enabled ? Palette.Text : Palette.TextDim;
            float detailWidth = string.IsNullOrEmpty(detail) ? 0f : Mathf.Min(units.width * 0.52f, 260f);
            float labelWidth = units.width - 20f - detailWidth;
            GUI.Label(theme.Units(units.x + 10f, units.y, labelWidth, units.height), label,
                theme.TextStyle(TypeSizes.Header, text, false, TextAnchor.MiddleLeft, true));
            if (detailWidth > 0f)
            {
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

            theme.Frame(units, Palette.Background, open ? Palette.Accent : Palette.Border);
            GUI.Label(theme.Units(units.x + 10f, units.y, units.width - 44f, units.height),
                index >= 0 && index < options.Length ? options[index] : "",
                theme.TextStyle(size, Palette.Text, false, TextAnchor.MiddleLeft));
            // The chevron: a square handle on the right, down when closed and up when open.
            Rect handle = new Rect(units.x + units.width - 28f, units.y, 28f, units.height);
            theme.Fill(new Rect(handle.x, units.y + 1f, 1f, units.height - 2f), Palette.Border);
            float cy = units.y + units.height * 0.5f;
            Color chevron = open ? Palette.Accent : Palette.TextMuted;
            theme.Fill(new Rect(handle.x + 8f, cy - 4f, 12f, 2f), chevron);
            theme.Fill(new Rect(handle.x + 8f, cy + 2f, 12f, 2f), chevron);

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
                    Rect row = new Rect(units.x, units.y + units.height * (option + 1) + 2f, units.width, rowHeight);
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
        /// A gadget that scrolls when its content is taller than it, with a thin scrollbar kept clear of the content
        /// by a gutter, as the design asks of both gadgets. The callback draws into `content`, whose y is advanced.
        /// </summary>
        public void ScrollColumn(Rect viewport, float contentHeight, ref Vector2 scroll, Action<Rect> draw)
        {
            const float gutter = 10f;
            const float barWidth = 4f;
            Rect body = new Rect(viewport.x, viewport.y, viewport.width - gutter, viewport.height);
            float max = Mathf.Max(0f, contentHeight - body.height);
            if (max > 0f && Event.current.type == EventType.ScrollWheel &&
                UnitsContains(body, Event.current.mousePosition))
            {
                scroll.y = Mathf.Clamp(scroll.y + Event.current.delta.y * 24f, 0f, max);
            }
            scroll.y = Mathf.Clamp(scroll.y, 0f, max);
            var content = new Rect(body.x, body.y - scroll.y, body.width, contentHeight);
            draw(content);
            if (max > 0f)
            {
                float trackHeight = body.height;
                float thumbHeight = Mathf.Max(24f, trackHeight * body.height / contentHeight);
                float thumb = body.y + (trackHeight - thumbHeight) * (scroll.y / max);
                theme.Fill(new Rect(body.xMax + gutter - barWidth, body.y, barWidth, trackHeight), Palette.Control);
                theme.Fill(new Rect(body.xMax + gutter - barWidth, thumb, barWidth, thumbHeight), Palette.TextDim);
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
