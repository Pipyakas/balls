using UnityEngine;

namespace Balls.Rendering
{
    /// <summary>
    /// The two materials the instanced draws use. Project setup creates them as assets so their shaders survive a
    /// build; the fallback keeps the editor usable when the setup step has not run.
    /// </summary>
    public static class RenderAssets
    {
        private static Material ballMaterial;
        private static Material quadMaterial;

        public static void Set(Material ball, Material quad)
        {
            ballMaterial = ball;
            quadMaterial = quad;
        }

        public static Material BallMaterial()
        {
            if (ballMaterial == null)
            {
                ballMaterial = Create("Balls/Procedural");
            }
            return ballMaterial;
        }

        public static Material QuadMaterial()
        {
            if (quadMaterial == null)
            {
                quadMaterial = Create("Balls/Quads");
            }
            return quadMaterial;
        }

        public static void SetBallRadius(float radius)
        {
            Material material = BallMaterial();
            if (material != null)
            {
                material.SetFloat("_Radius", radius);
            }
        }

        private static Material Create(string shaderName)
        {
            Shader shader = Shader.Find(shaderName);
            if (shader == null)
            {
                Debug.LogError($"shader {shaderName} is missing from the build");
                return null;
            }
            return new Material(shader) { name = shaderName };
        }
    }
}
