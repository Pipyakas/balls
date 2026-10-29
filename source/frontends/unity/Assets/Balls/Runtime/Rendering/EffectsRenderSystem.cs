using System.Collections.Generic;
using Balls.Ecs;
using Balls.Game;
using Unity.Collections;
using Unity.Entities;
using Unity.Mathematics;
using UnityEngine;
using UnityEngine.Rendering;

namespace Balls.Rendering
{
    /// <summary>
    /// The game's block and ball effects, derived from what the session publishes: a brick whose hits drop gets a
    /// contact flash and the vanishing ball, a brick that breaks bursts into fragments. The shader is opaque, so
    /// "fading" means shrinking and darkening toward the board backdrop. All effects share two pooled instanced draws.
    /// </summary>
    [UpdateAfter(typeof(BoardRenderSystem))]
    [UpdateAfter(typeof(BallRenderSystem))]
    public partial class EffectsRenderSystem : SystemBase
    {
        private const float BurstSeconds = 0.3f;
        private const float BallVanishSeconds = 0.15f;
        private const float FlashSeconds = 0.12f;
        private const int FragmentCount = 8;
        private const float FragmentSpeed = 2.4f;
        private const float FragmentHalfSize = 0.1f;
        private const float BallRadius = 0.2f;
        private const int MaxEffects = 6000;
        private const float LaunchMarkerSize = 0.6f;
        private const float LaunchMarkerGrey = 0.44f;
        private static readonly Color Backdrop = new Color(0.063f, 0.063f, 0.063f);

        private struct Effect
        {
            public float2 origin;
            public float2 velocity;
            public float birth;
            public float life;
            public float startSize;
            public float endSize;
            public Color color;
            public bool disc;
        }

        private readonly List<Effect> effects = new();
        private int[] previousHits = System.Array.Empty<int>();
        private float2[] previousPosition = System.Array.Empty<float2>();
        private int[] previousColor = System.Array.Empty<int>();
        private int[] currentHits = System.Array.Empty<int>();
        private float2[] currentPosition = System.Array.Empty<float2>();
        private int[] currentColor = System.Array.Empty<int>();
        private bool havePrevious;
        private Material discMaterial;
        private Material quadMaterial;
        private GraphicsBuffer discBuffer;
        private GraphicsBuffer quadBuffer;
        private GraphicsBuffer discArgs;
        private GraphicsBuffer quadArgs;
        private Material markerMaterial;
        private GraphicsBuffer markerBuffer;
        private GraphicsBuffer markerArgs;
        private NativeArray<QuadInstance> discScratch;
        private NativeArray<QuadInstance> quadScratch;
        private int capacity;

        protected override void OnCreate()
        {
            RequireForUpdate<BoardInfoData>();
        }

        protected override void OnStartRunning()
        {
            Material shared = RenderAssets.QuadMaterial();
            if (shared != null)
            {
                discMaterial = new Material(shared) { name = "BallsEffectDiscs" };
                quadMaterial = new Material(shared) { name = "BallsEffectQuads" };
                markerMaterial = new Material(shared) { name = "BallsLaunchMarker" };
                markerBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, 1,
                    System.Runtime.InteropServices.Marshal.SizeOf<QuadInstance>());
                markerArgs = RenderPrimitives.CreateIndirectArgs(0, RenderPrimitives.UnitTriangle);
            }
        }

        protected override void OnDestroy()
        {
            discBuffer?.Release();
            quadBuffer?.Release();
            discArgs?.Release();
            quadArgs?.Release();
            markerBuffer?.Release();
            markerArgs?.Release();
            if (discScratch.IsCreated)
            {
                discScratch.Dispose();
            }
            if (quadScratch.IsCreated)
            {
                quadScratch.Dispose();
            }
            if (markerMaterial != null)
            {
                Object.Destroy(markerMaterial);
            }
            if (discMaterial != null)
            {
                Object.Destroy(discMaterial);
            }
            if (quadMaterial != null)
            {
                Object.Destroy(quadMaterial);
            }
        }

        protected override void OnUpdate()
        {
            if (discMaterial == null || !SystemAPI.HasSingleton<BoardInfoData>())
            {
                return;
            }
            var board = SystemAPI.GetSingleton<BoardInfoData>();
            float now = (float)SystemAPI.Time.ElapsedTime;
            if (!board.valid)
            {
                effects.Clear();
                havePrevious = false;
                return;
            }

            DetectBrickEvents(board, SystemAPI.GetSingletonBuffer<CellElement>(true), now);
            DrawLaunchMarker(board);
            Draw(now);
        }

        /// <summary>The triangle where balls enter, 0.6 cell wide, matching the core's launch position.</summary>
        private void DrawLaunchMarker(BoardInfoData board)
        {
            var instance = new NativeArray<QuadInstance>(1, Allocator.Temp);
            instance[0] = new QuadInstance
            {
                center = new float2(board.columns * 0.5f, board.rows - BallRadius * 2f),
                size = new float2(LaunchMarkerSize, LaunchMarkerSize),
                color = new Vector4(LaunchMarkerGrey, LaunchMarkerGrey, LaunchMarkerGrey, 0f),
            };
            Submit(markerMaterial, markerBuffer, markerArgs, instance, 1, RenderPrimitives.UnitTriangle);
            instance.Dispose();
        }

        private void DetectBrickEvents(BoardInfoData board, DynamicBuffer<CellElement> cells, float now)
        {
            int ids = Mathf.Max(board.brickCount, 1);
            if (currentHits.Length < ids)
            {
                currentHits = new int[ids];
                currentPosition = new float2[ids];
                currentColor = new int[ids];
                havePrevious = false;
            }
            System.Array.Clear(currentHits, 0, ids);
            for (int index = 0; index < cells.Length; ++index)
            {
                CellElement cell = cells[index];
                if (cell.brick < 0 || cell.brick >= ids || cell.hitsLeft <= 0)
                {
                    continue;
                }
                int column = index % board.paddedWidth - 1;
                int row = index / board.paddedWidth - 1;
                currentHits[cell.brick] = cell.hitsLeft;
                currentPosition[cell.brick] = new float2(column + 0.5f, row + 0.5f);
                currentColor[cell.brick] = cell.color;
            }

            if (havePrevious && previousHits.Length >= ids)
            {
                bool restarted = false;
                for (int id = 0; id < ids && !restarted; ++id)
                {
                    restarted = currentHits[id] > previousHits[id];
                }
                if (!restarted)
                {
                    for (int id = 0; id < ids; ++id)
                    {
                        if (previousHits[id] > currentHits[id])
                        {
                            Spawn(previousPosition[id], previousColor[id], currentHits[id] <= 0, now);
                        }
                    }
                }
            }

            if (previousHits.Length < ids)
            {
                previousHits = new int[ids];
                previousPosition = new float2[ids];
                previousColor = new int[ids];
            }
            System.Array.Copy(currentHits, previousHits, ids);
            System.Array.Copy(currentPosition, previousPosition, ids);
            System.Array.Copy(currentColor, previousColor, ids);
            havePrevious = true;
        }

        private void Spawn(float2 position, int colorIndex, bool broken, float now)
        {
            if (effects.Count > MaxEffects - FragmentCount - 2)
            {
                return;
            }
            Color color = BallPalette.For(colorIndex);
            effects.Add(new Effect
            {
                origin = position, birth = now, life = BallVanishSeconds, startSize = BallRadius, endSize = 0f,
                color = color, disc = true,
            });
            effects.Add(new Effect
            {
                origin = position, birth = now, life = FlashSeconds, startSize = 0.12f, endSize = 0.32f,
                color = Color.white, disc = true,
            });
            if (!broken)
            {
                return;
            }
            for (int fragment = 0; fragment < FragmentCount; ++fragment)
            {
                float angle = (fragment + 0.5f) * Mathf.PI * 2f / FragmentCount;
                effects.Add(new Effect
                {
                    origin = position,
                    velocity = new float2(Mathf.Cos(angle), Mathf.Sin(angle)) * FragmentSpeed,
                    birth = now, life = BurstSeconds, startSize = FragmentHalfSize, endSize = 0f,
                    color = color, disc = false,
                });
            }
        }

        private void Draw(float now)
        {
            EnsureCapacity(effects.Count);
            int discCount = 0;
            int quadCount = 0;
            for (int index = effects.Count - 1; index >= 0; --index)
            {
                Effect effect = effects[index];
                float age = now - effect.birth;
                if (age >= effect.life)
                {
                    effects[index] = effects[effects.Count - 1];
                    effects.RemoveAt(effects.Count - 1);
                    continue;
                }
                float t = age / effect.life;
                float size = Mathf.Lerp(effect.startSize, effect.endSize, t);
                Color color = Color.Lerp(effect.color, Backdrop, t * t);
                float2 center = effect.origin + effect.velocity * age;
                var instance = new QuadInstance
                {
                    center = center,
                    size = new float2(size, size),
                    color = new Vector4(color.r, color.g, color.b, effect.disc ? 1f : 0f),
                };
                if (effect.disc)
                {
                    discScratch[discCount++] = instance;
                }
                else
                {
                    quadScratch[quadCount++] = instance;
                }
            }

            Submit(discMaterial, discBuffer, discArgs, discScratch, discCount, RenderPrimitives.UnitDisc);
            Submit(quadMaterial, quadBuffer, quadArgs, quadScratch, quadCount, RenderPrimitives.UnitQuad);
        }

        private static void Submit(Material material, GraphicsBuffer buffer, GraphicsBuffer args,
            NativeArray<QuadInstance> scratch, int count, Mesh mesh)
        {
            if (count == 0)
            {
                return;
            }
            buffer.SetData(scratch, 0, 0, count);
            RenderPrimitives.SetIndirectArgs(args, count, mesh);
            material.SetBuffer("_Quads", buffer);
            var parameters = new RenderParams(material)
            {
                worldBounds = new Bounds(new Vector3(500f, 500f, 0f), new Vector3(4000f, 4000f, 10f)),
                shadowCastingMode = ShadowCastingMode.Off,
                receiveShadows = false,
            };
            Graphics.RenderMeshIndirect(parameters, mesh, args, 1);
        }

        private void EnsureCapacity(int needed)
        {
            if (needed <= capacity && discBuffer != null)
            {
                return;
            }
            discBuffer?.Release();
            quadBuffer?.Release();
            discArgs?.Release();
            quadArgs?.Release();
            if (discScratch.IsCreated)
            {
                discScratch.Dispose();
            }
            if (quadScratch.IsCreated)
            {
                quadScratch.Dispose();
            }
            capacity = RenderPrimitives.NextPowerOfTwo(Mathf.Max(needed, 64));
            int stride = System.Runtime.InteropServices.Marshal.SizeOf<QuadInstance>();
            discScratch = new NativeArray<QuadInstance>(capacity, Allocator.Persistent);
            quadScratch = new NativeArray<QuadInstance>(capacity, Allocator.Persistent);
            discBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, capacity, stride);
            quadBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, capacity, stride);
            discArgs = RenderPrimitives.CreateIndirectArgs(0, RenderPrimitives.UnitDisc);
            quadArgs = RenderPrimitives.CreateIndirectArgs(0, RenderPrimitives.UnitQuad);
        }
    }
}
