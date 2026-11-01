using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Balls.Physics.Burst
{

    public struct BrickType
    {
        public int id;
        public int color;
        public int hits;

        public BrickType(int id, int color, int hits)
        {
            this.id = id;
            this.color = color;
            this.hits = hits;
        }
    }

    public sealed class BrickLayout
    {
        public const int EmptyBrickId = -1;

        public int width;
        public int height;
        public int playableHeight;
        public int[] cellIds = Array.Empty<int>();
        public List<BrickType> types = new();

        public int CellId(int column, int row) => cellIds[row * width + column];

        public BrickType? FindType(int id)
        {
            for (int i = 0; i < types.Count; ++i)
            {
                if (types[i].id == id) return types[i];
            }
            return null;
        }

        public Dictionary<int, BrickType> TypesById()
        {
            var map = new Dictionary<int, BrickType>(types.Count);
            for (int i = 0; i < types.Count; ++i)
            {
                map[types[i].id] = types[i];
            }
            return map;
        }
    }

    public struct Launcher
    {
        public int color;
        public int ballCount;

        public Launcher(int color, int ballCount)
        {
            this.color = color;
            this.ballCount = ballCount;
        }
    }

    public sealed class LauncherLayout
    {
        public int columns;
        public int visibleRows;
        public Launcher?[] slots = Array.Empty<Launcher?>();

        public int RowCount => columns == 0 ? 0 : slots.Length / columns;

        public Launcher? Slot(int column, int row) => slots[row * columns + column];
    }

    public sealed class LevelData
    {
        public string name;
        public BrickLayout bricks;
        public LauncherLayout launchers;

        public LevelData(string name, BrickLayout bricks, LauncherLayout launchers)
        {
            this.name = name;
            this.bricks = bricks;
            this.launchers = launchers;
        }
    }

    public static class LevelDocument
    {
        public const int EmptyBrickId = -1;

        private static readonly string[] ColorNames =
        {
            "Red", "Orange", "Yellow", "Green", "Blue", "Indigo", "Violet"
        };

        public static string ColorName(int color)
        {
            return color >= 0 && color < ColorNames.Length ? ColorNames[color] : "Unknown";
        }

        public static bool TryParseBallColor(string name, out int color)
        {
            for (int i = 0; i < ColorNames.Length; ++i)
            {
                if (string.Equals(ColorNames[i], name, StringComparison.OrdinalIgnoreCase))
                {
                    color = i;
                    return true;
                }
            }
            color = -1;
            return false;
        }

        public static BrickLayout ParseBrickLayout(string text)
        {
            var sections = SplitSections(text);
            if (!sections.TryGetValue("Board", out var boardLines))
            {
                throw new FormatException("missing [Board] section");
            }

            var boardValues = ParseBoardSection(boardLines);
            if (!boardValues.TryGetValue("Width", out int width) || width <= 0)
                throw new FormatException("[Board] 'Width' must be positive");
            if (!boardValues.TryGetValue("Height", out int height) || height <= 0)
                throw new FormatException("[Board] 'Height' must be positive");
            if (!boardValues.TryGetValue("PlayableHeight", out int playableHeight) || playableHeight <= 0)
                throw new FormatException("[Board] 'PlayableHeight' must be positive");
            if (playableHeight < height)
                throw new FormatException("PlayableHeight must be at least Height");

            var layout = new BrickLayout
            {
                width = width,
                height = height,
                playableHeight = playableHeight,
                cellIds = new int[width * height]
            };

            if (!sections.TryGetValue("Layout", out var layoutLines) || layoutLines.Count != height)
            {
                throw new FormatException($"[Layout] must have {height} rows");
            }

            for (int row = 0; row < height; ++row)
            {
                var tokens = layoutLines[row];
                if (tokens.Length != width)
                {
                    throw new FormatException($"[Layout] row {row} must have {width} cells");
                }
                for (int col = 0; col < width; ++col)
                {
                    string token = tokens[col];
                    if (string.Equals(token, "Empty", StringComparison.OrdinalIgnoreCase) || token == ".")
                    {
                        layout.cellIds[row * width + col] = EmptyBrickId;
                    }
                    else if (int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out int id))
                    {
                        layout.cellIds[row * width + col] = id < 0 ? EmptyBrickId : id;
                    }
                    else
                    {
                        throw new FormatException($"[Layout] row {row} col {col}: invalid token '{token}'");
                    }
                }
            }

            if (!sections.TryGetValue("BrickTypes", out var typeLines))
            {
                throw new FormatException("missing [BrickTypes] section");
            }

            var typeIds = new HashSet<int>();
            foreach (var tokens in typeLines)
            {
                if (tokens.Length != 3)
                {
                    throw new FormatException("[BrickTypes] lines must be '<Id> <Color> <Hits>'");
                }
                if (!int.TryParse(tokens[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out int id))
                    throw new FormatException($"[BrickTypes] invalid id '{tokens[0]}'");
                if (!TryParseBallColor(tokens[1], out int color))
                    throw new FormatException($"[BrickTypes] unknown color '{tokens[1]}'");
                if (!int.TryParse(tokens[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int hits) || hits <= 0)
                    throw new FormatException($"[BrickTypes] invalid hits '{tokens[2]}'");

                if (!typeIds.Add(id))
                {
                    throw new FormatException($"[BrickTypes] duplicate id {id}");
                }
                layout.types.Add(new BrickType(id, color, hits));
            }

            for (int i = 0; i < layout.cellIds.Length; ++i)
            {
                if (layout.cellIds[i] != EmptyBrickId && !typeIds.Contains(layout.cellIds[i]))
                {
                    layout.cellIds[i] = EmptyBrickId;
                }
            }

            return layout;
        }

        public static LauncherLayout ParseLauncherLayout(string text)
        {
            var sections = SplitSections(text);
            if (!sections.TryGetValue("Board", out var boardLines))
            {
                throw new FormatException("missing [Board] section");
            }

            var boardValues = ParseBoardSection(boardLines);
            if (!boardValues.TryGetValue("Width", out int columns) || columns <= 0)
                throw new FormatException("[Board] 'Width' must be positive");
            if (!boardValues.TryGetValue("Height", out int visibleRows) || visibleRows <= 0)
                throw new FormatException("[Board] 'Height' must be positive");

            if (!sections.TryGetValue("Layout", out var layoutLines))
            {
                throw new FormatException("missing [Layout] section");
            }

            var slots = new List<Launcher?>();
            for (int row = 0; row < layoutLines.Count; ++row)
            {
                var tokens = layoutLines[row];
                if (tokens.Length != columns)
                {
                    throw new FormatException($"[Layout] row {row} must have {columns} launchers");
                }
                for (int col = 0; col < columns; ++col)
                {
                    string token = tokens[col];
                    if (string.Equals(token, "Empty", StringComparison.OrdinalIgnoreCase))
                    {
                        slots.Add(null);
                        continue;
                    }

                    int openIndex = token.IndexOf('(');
                    if (openIndex <= 0 || !token.EndsWith(")"))
                    {
                        throw new FormatException($"launcher '{token}' must look like Color(count)");
                    }

                    string colorStr = token.Substring(0, openIndex);
                    if (!TryParseBallColor(colorStr, out int color))
                    {
                        throw new FormatException($"unknown color in '{token}'");
                    }

                    string countStr = token.Substring(openIndex + 1, token.Length - openIndex - 2);
                    if (!int.TryParse(countStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int count) || count <= 0)
                    {
                        throw new FormatException($"invalid ball count in '{token}'");
                    }

                    slots.Add(new Launcher(color, count));
                }
            }

            return new LauncherLayout
            {
                columns = columns,
                visibleRows = visibleRows,
                slots = slots.ToArray()
            };
        }

        public static LevelData LoadLevel(string name, string bricksPath, string launchersPath)
        {
            string bricksText = File.ReadAllText(bricksPath, Encoding.UTF8);
            string launchersText = File.ReadAllText(launchersPath, Encoding.UTF8);
            return new LevelData(name, ParseBrickLayout(bricksText), ParseLauncherLayout(launchersText));
        }

        public static LevelData LoadLevelFromDirectory(string directory, string levelName)
        {
            string bricksPath = Path.Combine(directory, $"{levelName}_Bricks.txt");
            string launchersPath = Path.Combine(directory, $"{levelName}_SpawnBall.txt");
            return LoadLevel(levelName, bricksPath, launchersPath);
        }

        public static string FormatBrickLayout(BrickLayout layout)
        {
            var sb = new StringBuilder();
            sb.AppendFormat(CultureInfo.InvariantCulture, "[Board]\nWidth {0}\nHeight {1}\nPlayableHeight {2}\n\n[Layout]\n",
                layout.width, layout.height, layout.playableHeight);

            for (int row = 0; row < layout.height; ++row)
            {
                for (int col = 0; col < layout.width; ++col)
                {
                    if (col > 0) sb.Append(' ');
                    int id = layout.CellId(col, row);
                    sb.Append(id == EmptyBrickId ? "-1" : id.ToString(CultureInfo.InvariantCulture));
                }
                sb.Append('\n');
            }

            sb.Append("\n[BrickTypes]\n");
            var placedIds = new HashSet<int>();
            for (int i = 0; i < layout.cellIds.Length; ++i)
            {
                if (layout.cellIds[i] != EmptyBrickId)
                {
                    placedIds.Add(layout.cellIds[i]);
                }
            }

            var sortedTypes = new List<BrickType>(layout.types);
            sortedTypes.Sort((a, b) => a.id.CompareTo(b.id));

            foreach (var type in sortedTypes)
            {
                if (placedIds.Contains(type.id))
                {
                    sb.AppendFormat(CultureInfo.InvariantCulture, "{0} {1} {2}\n",
                        type.id, ColorName(type.color), type.hits);
                }
            }

            return sb.ToString();
        }

        public static string FormatLauncherLayout(LauncherLayout layout)
        {
            var sb = new StringBuilder();
            sb.AppendFormat(CultureInfo.InvariantCulture, "[Board]\nWidth {0}\nHeight {1}\n\n[Layout]\n",
                layout.columns, layout.visibleRows);

            int rows = layout.RowCount;
            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < layout.columns; ++col)
                {
                    if (col > 0) sb.Append(' ');
                    var slot = layout.Slot(col, row);
                    if (slot.HasValue)
                    {
                        sb.AppendFormat(CultureInfo.InvariantCulture, "{0}({1})",
                            ColorName(slot.Value.color), slot.Value.ballCount);
                    }
                    else
                    {
                        sb.Append("Empty");
                    }
                }
                sb.Append('\n');
            }

            return sb.ToString();
        }

        public static void SaveLevel(LevelData level, string bricksPath, string launchersPath)
        {
            WriteFileAtomically(bricksPath, FormatBrickLayout(level.bricks));
            WriteFileAtomically(launchersPath, FormatLauncherLayout(level.launchers));
        }

        private static void WriteFileAtomically(string path, string content)
        {
            string dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir))
            {
                Directory.CreateDirectory(dir);
            }
            string tempPath = path + ".tmp";
            File.WriteAllText(tempPath, content, Encoding.UTF8);
            if (File.Exists(path))
            {
                File.Delete(path);
            }
            File.Move(tempPath, path);
        }

        private static Dictionary<string, List<string[]>> SplitSections(string text)
        {
            var sections = new Dictionary<string, List<string[]>>(StringComparer.OrdinalIgnoreCase);
            string currentSection = "";

            using var reader = new StringReader(text);
            string line;
            while ((line = reader.ReadLine()) != null)
            {
                line = line.Trim();
                if (line.Length == 0) continue;

                if (line.StartsWith("[") && line.EndsWith("]"))
                {
                    currentSection = line.Substring(1, line.Length - 2).Trim();
                    if (!sections.ContainsKey(currentSection))
                    {
                        sections[currentSection] = new List<string[]>();
                    }
                    continue;
                }

                if (!string.IsNullOrEmpty(currentSection))
                {
                    string[] tokens = line.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                    if (tokens.Length > 0)
                    {
                        if (!sections.TryGetValue(currentSection, out var list))
                        {
                            list = new List<string[]>();
                            sections[currentSection] = list;
                        }
                        list.Add(tokens);
                    }
                }
            }

            return sections;
        }

        private static Dictionary<string, int> ParseBoardSection(List<string[]> lines)
        {
            var map = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            foreach (var tokens in lines)
            {
                if (tokens.Length != 2)
                {
                    throw new FormatException("[Board] lines must be '<Key> <Value>'");
                }
                if (!int.TryParse(tokens[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int val))
                {
                    throw new FormatException($"[Board] expected integer for key '{tokens[0]}', got '{tokens[1]}'");
                }
                map[tokens[0]] = val;
            }
            return map;
        }
    }
}
