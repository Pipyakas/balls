using Balls.Native;
using UnityEngine;

namespace Balls.Ui
{

    public sealed class CoreLayout
    {
        public Rect Header { get; private set; }

        public Rect Main { get; private set; }

        public Rect Interaction { get; private set; }

        public Rect Debug { get; private set; }

        public Rect BoardRect { get; private set; }

        public Vector2 BoardOrigin { get; private set; }

        public float CellSize { get; private set; }

        public bool Portrait { get; private set; }

        public void SetPortraitRects(Rect header, Rect main, Rect interaction, Rect debug, Rect boardRect,
            Vector2 boardOrigin, float cellSize)
        {
            Header = header;
            Main = main;
            Interaction = interaction;
            Debug = debug;
            BoardRect = boardRect;
            BoardOrigin = boardOrigin;
            CellSize = cellSize;
            Portrait = true;
        }

        public static CoreLayout Compute(float width, float height, int columns, int rows, int orientation, int launcherRows)
        {
            BallsNative.balls_compute_layout(width, height, Mathf.Max(1, columns), Mathf.Max(1, rows), orientation,
                Mathf.Max(1, launcherRows), out NativeLayout native);
            return new CoreLayout
            {
                Header = new Rect(native.headerX, native.headerY, native.headerW, native.headerH),
                Main = new Rect(native.mainX, native.mainY, native.mainW, native.mainH),
                Interaction = new Rect(native.interactionX, native.interactionY, native.interactionW, native.interactionH),
                Debug = new Rect(native.debugX, native.debugY, native.debugW, native.debugH),
                BoardRect = new Rect(native.boardX, native.boardY, native.boardW, native.boardH),
                BoardOrigin = new Vector2(native.boardOriginX, native.boardOriginY),
                CellSize = native.cellSize,
                Portrait = native.portrait != 0,
            };
        }
    }
}
