using Balls.Ecs;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;
using UnityEngine.Rendering;
using Unity.Entities;

namespace Balls.Rendering
{
    /// <summary>
    /// Draws every live ball in one indirect, procedurally instanced draw call: the packed words go to the GPU as
    /// they are and the vertex shader turns each into a quad.
    /// </summary>
    public partial class BallRenderSystem : SystemBase
    {
        private const int PackedCoordinateMax = 16383;

        private GraphicsBuffer balls;
        private GraphicsBuffer args;
        private int capacity;
        private Material material;

        protected override void OnCreate()
        {
            RequireForUpdate<BallBatch>();
        }

        protected override void OnStartRunning()
        {
            material = RenderAssets.BallMaterial();
        }

        protected override void OnDestroy()
        {
            balls?.Release();
            args?.Release();
        }

        protected override void OnUpdate()
        {
            var batch = SystemAPI.GetSingleton<BallBatch>();
            EnsureCapacity(batch.count);
            if (batch.count <= 0 || batch.packed == System.IntPtr.Zero || material == null)
            {
                return;
            }

            Upload(batch);
            RenderPrimitives.SetIndirectArgs(args, batch.count, RenderPrimitives.UnitQuad);
            material.SetVector("_FieldToWorld", new Vector4(1f / batch.scaleX, 1f / batch.scaleY, 0f, 0f));
            material.SetBuffer("_Balls", balls);

            var bounds = new Bounds(new Vector3(500f, 500f, 0f), new Vector3(4000f, 4000f, 10f));
            var parameters = new RenderParams(material)
            {
                worldBounds = bounds,
                shadowCastingMode = ShadowCastingMode.Off,
                receiveShadows = false,
            };
            Graphics.RenderMeshIndirect(parameters, RenderPrimitives.UnitQuad, args, 1);
        }

        private void EnsureCapacity(int count)
        {
            if (count <= capacity && balls != null)
            {
                return;
            }
            balls?.Release();
            args?.Release();
            capacity = RenderPrimitives.NextPowerOfTwo(Mathf.Max(count, 1));
            balls = new GraphicsBuffer(GraphicsBuffer.Target.Structured, capacity, sizeof(uint));
            args = RenderPrimitives.CreateIndirectArgs(0, RenderPrimitives.UnitQuad);
        }

        private unsafe void Upload(in BallBatch batch)
        {
            var view = NativeArrayUnsafeUtility.ConvertExistingDataToNativeArray<uint>(
                (void*)batch.packed, batch.count, Allocator.None);
#if ENABLE_UNITY_COLLECTIONS_CHECKS
            var handle = AtomicSafetyHandle.Create();
            NativeArrayUnsafeUtility.SetAtomicSafetyHandle(ref view, handle);
#endif
            balls.SetData(view, 0, 0, batch.count);
#if ENABLE_UNITY_COLLECTIONS_CHECKS
            AtomicSafetyHandle.Release(handle);
#endif
        }
    }
}
