using Balls.Ecs;
using Balls.Game;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using Unity.Entities;
using UnityEngine;
using UnityEngine.Rendering;

namespace Balls.Rendering
{
    /// <summary>
    /// Draws every live ball as one disc instance, decoded from the packed words the physics backend hands over.
    /// Balls get their own draw and their own mesh so the board can stay square quads without either draw having to
    /// change material state — a material property set between two draws in one frame applies to both.
    /// </summary>
    [UpdateAfter(typeof(Balls.App.SessionSystem))]
    public partial class BallRenderSystem : SystemBase
    {
        // One ball radius in cells, matching SimulationTuning::ballRadius in the core.
        private const float BallRadius = 0.2f;

        private GraphicsBuffer instances;
        private GraphicsBuffer args;
        private NativeArray<QuadInstance> scratch;
        private NativeArray<uint> packed;
        private Material material;
        private int capacity;

        protected override void OnCreate()
        {
            RequireForUpdate<BallBatch>();
        }

        protected override void OnStartRunning()
        {
            material = RenderAssets.QuadMaterial();
        }

        protected override void OnDestroy()
        {
            instances?.Release();
            args?.Release();
            if (scratch.IsCreated)
            {
                scratch.Dispose();
            }
            if (packed.IsCreated)
            {
                packed.Dispose();
            }
        }

        protected override void OnUpdate()
        {
            if (material == null || !SystemAPI.HasSingleton<BoardInfoData>())
            {
                return;
            }
            var board = SystemAPI.GetSingleton<BoardInfoData>();
            var batch = SystemAPI.GetSingleton<BallBatch>();
            if (!board.valid || batch.count <= 0 || batch.packed == System.IntPtr.Zero)
            {
                return;
            }
            EnsureCapacity(batch.count);
            if (!packed.IsCreated || packed.Length < batch.count)
            {
                if (packed.IsCreated)
                {
                    packed.Dispose();
                }
                packed = new NativeArray<uint>(RenderPrimitives.NextPowerOfTwo(batch.count), Allocator.Persistent);
            }
            unsafe
            {
                UnsafeUtility.MemCpy(packed.GetUnsafePtr(), (void*)batch.packed, (long)batch.count * sizeof(uint));
            }

            float scaleX = batch.scaleX <= 0f ? 1f : 1f / batch.scaleX;
            float scaleY = batch.scaleY <= 0f ? 1f : 1f / batch.scaleY;
            int count = 0;
            for (int index = 0; index < batch.count && count < capacity; ++index)
            {
                uint word = packed[index];
                float x = (word & 0x3FFFu) * scaleX;
                float y = ((word >> 14) & 0x3FFFu) * scaleY;
                if (!float.IsFinite(x) || !float.IsFinite(y) || x < -1f || y < -1f ||
                    x > board.columns + 1f || y > board.rows + 1f)
                {
                    continue;
                }
                Color tint = BallPalette.For((int)(word >> 28));
                scratch[count++] = new QuadInstance
                {
                    center = new Unity.Mathematics.float2(x, y),
                    size = new Unity.Mathematics.float2(BallRadius, BallRadius),
                    color = new Vector4(tint.r, tint.g, tint.b, 1f),
                };
            }
            if (count == 0)
            {
                return;
            }

            instances.SetData(scratch, 0, 0, count);
            RenderPrimitives.SetIndirectArgs(args, count, RenderPrimitives.UnitDisc);
            material.SetBuffer("_Quads", instances);
            var bounds = new Bounds(new Vector3(500f, 500f, 0f), new Vector3(4000f, 4000f, 10f));
            var parameters = new RenderParams(material)
            {
                worldBounds = bounds,
                shadowCastingMode = ShadowCastingMode.Off,
                receiveShadows = false,
            };
            Graphics.RenderMeshIndirect(parameters, RenderPrimitives.UnitDisc, args, 1);
        }

        private void EnsureCapacity(int needed)
        {
            if (needed <= capacity && instances != null)
            {
                return;
            }
            instances?.Release();
            args?.Release();
            if (scratch.IsCreated)
            {
                scratch.Dispose();
            }
            capacity = RenderPrimitives.NextPowerOfTwo(Mathf.Max(needed, 64));
            scratch = new NativeArray<QuadInstance>(capacity, Allocator.Persistent);
            instances = new GraphicsBuffer(GraphicsBuffer.Target.Structured, capacity,
                System.Runtime.InteropServices.Marshal.SizeOf<QuadInstance>());
            args = RenderPrimitives.CreateIndirectArgs(0, RenderPrimitives.UnitDisc);
        }
    }
}
