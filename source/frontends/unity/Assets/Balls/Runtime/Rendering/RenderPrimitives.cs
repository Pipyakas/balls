using System;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using Unity.Mathematics;
using UnityEngine;
using UnityEngine.Rendering;

namespace Balls.Rendering
{

    public struct QuadInstance
    {
        public float2 center;
        public float2 size;

        public Vector4 color;
    }

    public static class RenderPrimitives
    {
        private static Mesh unitQuad;

        private static Mesh unitDisc;

        private static Mesh unitTriangle;

        public static Mesh UnitTriangle
        {
            get
            {
                if (unitTriangle == null)
                {
                    var mesh = new Mesh { name = "BallsUnitTriangle" };
                    mesh.vertices = new[]
                    {
                        new Vector3(0f, 0.6f, 0f),
                        new Vector3(-1f, -0.4f, 0f),
                        new Vector3(1f, -0.4f, 0f),
                    };
                    mesh.triangles = new[] { 0, 2, 1 };
                    mesh.RecalculateBounds();
                    unitTriangle = mesh;
                }
                return unitTriangle;
            }
        }

        public static Mesh UnitDisc
        {
            get
            {
                if (unitDisc == null)
                {
                    const int segments = 32;
                    var mesh = new Mesh { name = "BallsUnitDisc" };
                    var vertices = new Vector3[segments + 1];
                    var triangles = new int[segments * 3];
                    vertices[0] = Vector3.zero;
                    for (int index = 0; index < segments; ++index)
                    {
                        float angle = (60f + 360f * index / segments) * Mathf.Deg2Rad;
                        vertices[index + 1] = new Vector3(Mathf.Cos(angle), Mathf.Sin(angle), 0f);
                        triangles[index * 3] = 0;
                        triangles[index * 3 + 1] = index + 1;
                        triangles[index * 3 + 2] = (index + 1) % segments + 1;
                    }
                    mesh.vertices = vertices;
                    mesh.triangles = triangles;
                    mesh.RecalculateBounds();
                    unitDisc = mesh;
                }
                return unitDisc;
            }
        }

        public static Mesh UnitQuad
        {
            get
            {
                if (unitQuad == null)
                {
                    var mesh = new Mesh { name = "BallsUnitQuad" };
                    mesh.vertices = new[]
                    {
                        new Vector3(-1f, -1f, 0f),
                        new Vector3(1f, -1f, 0f),
                        new Vector3(1f, 1f, 0f),
                        new Vector3(-1f, 1f, 0f),
                    };
                    mesh.uv = new[]
                    {
                        new Vector2(0f, 0f),
                        new Vector2(1f, 0f),
                        new Vector2(1f, 1f),
                        new Vector2(0f, 1f),
                    };
                    mesh.triangles = new[] { 0, 2, 1, 0, 3, 2 };
                    mesh.RecalculateBounds();
                    unitQuad = mesh;
                }
                return unitQuad;
            }
        }

        public static GraphicsBuffer CreateIndirectArgs(int instanceCount, Mesh mesh)
        {
            var buffer = new GraphicsBuffer(GraphicsBuffer.Target.IndirectArguments, 1, 5 * sizeof(uint));
            SetIndirectArgs(buffer, instanceCount, mesh);
            return buffer;
        }

        public static void SetIndirectArgsWithStart(GraphicsBuffer buffer, int instanceCount, Mesh mesh, int startInstance)
        {
            var args = new uint[5];
            args[0] = mesh != null ? (uint)mesh.GetIndexCount(0) : 0u;
            args[1] = (uint)System.Math.Max(0, instanceCount);
            args[2] = mesh != null ? (uint)mesh.GetIndexStart(0) : 0u;
            args[3] = 0u;
            args[4] = (uint)System.Math.Max(0, startInstance);
            buffer.SetData(args);
        }

        public static void SetIndirectArgs(GraphicsBuffer buffer, int instanceCount, Mesh mesh)
        {
            var args = new uint[5];
            args[0] = mesh != null ? (uint)mesh.GetIndexCount(0) : 0u;
            args[1] = (uint)instanceCount;
            args[2] = mesh != null ? (uint)mesh.GetIndexStart(0) : 0u;
            args[3] = 0u;
            args[4] = 0u;
            buffer.SetData(args);
        }

        public static Camera CreateCamera(int columns, int rows, int launcherRows)
        {
            var existing = Camera.main;
            Camera camera = existing;
            if (camera == null)
            {
                var go = new GameObject("Balls Camera") { tag = "MainCamera" };
                camera = go.AddComponent<Camera>();
            }
            float top = rows + 1.5f + launcherRows;
            float centerY = (top - 0.5f) * 0.5f;
            camera.orthographic = true;
            camera.transform.position = new Vector3(columns * 0.5f - 0.5f, centerY, -10f);
            camera.transform.rotation = Quaternion.identity;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(0.06f, 0.07f, 0.09f);
            camera.nearClipPlane = 0.01f;
            camera.farClipPlane = 100f;
            Resize(camera, columns, top);
            return camera;
        }

        public static void Resize(Camera camera, float fieldWidth, float fieldHeight)
        {
            float aspect = camera.aspect <= 0f ? 16f / 9f : camera.aspect;
            float vertical = Mathf.Max(fieldHeight, fieldWidth / aspect);
            camera.orthographicSize = vertical * 0.5f;
        }

        public static void SetPalette(Material material)
        {
            var palette = new Vector4[Game.BallPalette.ColorCount];
            for (int index = 0; index < palette.Length; ++index)
            {
                Color color = Game.BallPalette.For(index);
                palette[index] = new Vector4(color.r, color.g, color.b, 1f);
            }
            material.SetVectorArray("_Palette", palette);
        }

        public static int NextPowerOfTwo(int value)
        {
            int result = 64;
            while (result < value)
            {
                result <<= 1;
            }
            return result;
        }
    }
}
