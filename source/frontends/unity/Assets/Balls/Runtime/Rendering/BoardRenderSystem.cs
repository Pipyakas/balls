using Balls.Ecs;
using Balls.Game;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;
using UnityEngine.Rendering;
using Unity.Entities;

namespace Balls.Rendering
{

    [Unity.Entities.UpdateAfter(typeof(Balls.App.SessionSystem))]
    public partial class BoardRenderSystem : SystemBase
    {
        private const float CellHalfSize = 0.46f;

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
            EnsureCapacity(cells.Length);

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

                float flash = Mathf.Clamp01(cell.flash / 0.15f);
                Color color = BallPalette.For(cell.color);
                color = Color.Lerp(color, Color.white, flash * 0.75f);

                float half = CellHalfSize * (cell.hitsLeft > 1 ? 0.92f : 1f);
                scratch[count++] = new QuadInstance
                {
                    center = new Unity.Mathematics.float2(column + 0.5f, row + 0.5f + cell.rise),
                    size = new Unity.Mathematics.float2(half, half),
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
