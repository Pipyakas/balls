using Balls.Ecs;
using Balls.Game;
using Unity.Collections;
using UnityEngine;
using UnityEngine.Rendering;
using Unity.Entities;

namespace Balls.Rendering
{
    /// <summary>
    /// Draws the board's cells and the launcher queue. Both are small (a level is at most a few thousand cells), so
    /// they share one instanced draw with per-instance colour rather than a draw per colour.
    /// </summary>
    public partial class BoardRenderSystem : SystemBase
    {
        private const float CellHalfSize = 0.46f;
        private const float LauncherHalfSize = 0.42f;
        private const float LauncherRowSpacing = 1.15f;
        private const float LauncherFirstRowOffset = 0.9f;

        private GraphicsBuffer quads;
        private GraphicsBuffer args;
        private NativeArray<QuadInstance> scratch;
        private Material material;
        private int capacity;

        protected override void OnCreate()
        {
            RequireForUpdate<BoardInfoData>();
        }

        protected override void OnStartRunning()
        {
            material = RenderAssets.QuadMaterial();
        }

        protected override void OnDestroy()
        {
            quads?.Release();
            args?.Release();
            if (scratch.IsCreated)
            {
                scratch.Dispose();
            }
        }

        protected override void OnUpdate()
        {
            if (material == null || !SystemAPI.HasSingleton<BoardInfoData>())
            {
                return;
            }
            var board = SystemAPI.GetSingleton<BoardInfoData>();
            if (!board.valid)
            {
                return;
            }
            var cells = SystemAPI.GetSingletonBuffer<CellElement>(true);
            var launchers = SystemAPI.GetSingletonBuffer<LauncherElement>(true);

            int needed = cells.Length + launchers.Length;
            EnsureCapacity(needed);

            int count = 0;
            for (int index = 0; index < cells.Length; ++index)
            {
                CellElement cell = cells[index];
                if (cell.brick < 0 || cell.hitsLeft <= 0)
                {
                    continue;
                }
                int column = index % board.paddedWidth - 1;
                int row = index / board.paddedWidth - 1;
                // The core counts the flash down in seconds; the shader wants a 0..1 strength.
                float flash = Mathf.Clamp01(cell.flash / 0.15f);
                Color color = BallPalette.For(cell.color);
                color = Color.Lerp(color, Color.white, flash * 0.75f);
                // A brick with several hits left reads as a slightly larger, dimmer block.
                float half = CellHalfSize * (cell.hitsLeft > 1 ? 0.92f : 1f);
                scratch[count++] = new QuadInstance
                {
                    center = new Unity.Mathematics.float2(column + 0.5f, row + 0.5f + cell.rise),
                    size = new Unity.Mathematics.float2(half, half),
                    color = new Vector4(color.r, color.g, color.b, 1f),
                };
            }

            for (int index = 0; index < launchers.Length; ++index)
            {
                LauncherElement launcher = launchers[index];
                if (!launcher.present)
                {
                    continue;
                }
                Color color = BallPalette.For(launcher.color);
                scratch[count++] = new QuadInstance
                {
                    center = new Unity.Mathematics.float2(
                        launcher.column + 0.5f,
                        board.rows + LauncherFirstRowOffset + launcher.row * LauncherRowSpacing + launcher.rise),
                    size = new Unity.Mathematics.float2(LauncherHalfSize, LauncherHalfSize),
                    color = new Vector4(color.r, color.g, color.b, 1f),
                };
            }

            if (count == 0)
            {
                return;
            }

            quads.SetData(scratch, 0, 0, count);
            RenderPrimitives.SetIndirectArgs(args, count, RenderPrimitives.UnitQuad);
            material.SetBuffer("_Quads", quads);
            material.SetFloat("_CornerRadius", 0f);
            var bounds = new Bounds(new Vector3(500f, 500f, 0f), new Vector3(4000f, 4000f, 10f));
            var parameters = new RenderParams(material)
            {
                worldBounds = bounds,
                shadowCastingMode = ShadowCastingMode.Off,
                receiveShadows = false,
            };
            Graphics.RenderMeshIndirect(parameters, RenderPrimitives.UnitQuad, args, 1);
        }

        private void EnsureCapacity(int needed)
        {
            if (needed <= capacity && quads != null)
            {
                return;
            }
            quads?.Release();
            args?.Release();
            if (scratch.IsCreated)
            {
                scratch.Dispose();
            }
            capacity = RenderPrimitives.NextPowerOfTwo(Mathf.Max(needed, 64));
            scratch = new NativeArray<QuadInstance>(capacity, Allocator.Persistent);
            quads = new GraphicsBuffer(GraphicsBuffer.Target.Structured, capacity, System.Runtime.InteropServices.Marshal.SizeOf<QuadInstance>());
            args = RenderPrimitives.CreateIndirectArgs(0, RenderPrimitives.UnitQuad);
        }
    }
}
