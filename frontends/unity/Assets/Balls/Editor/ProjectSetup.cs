using System.IO;
using Balls.App;
using Balls.Rendering;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace Balls.Editor
{
    /// <summary>
    /// Everything the project needs that is normally clicked together in the editor, so a batchmode run can produce
    /// a working project from a clean checkout: the two instanced materials, the game scene, and the level files
    /// and native plugins copied where a build can find them.
    /// </summary>
    public static class ProjectSetup
    {
        public const string ScenePath = "Assets/Balls/Scenes/Balls.unity";
        private const string MaterialDirectory = "Assets/Balls/Materials";

        [MenuItem("Balls/Set Up Project")]
        public static void SetUp()
        {
            Directory.CreateDirectory(Path.Combine(Application.dataPath, "Balls/Scenes"));
            Directory.CreateDirectory(MaterialDirectory);

            Material ballMaterial = CreateMaterial("BallsProcedural", "Balls/Procedural");
            Material quadMaterial = CreateMaterial("BallsQuads", "Balls/Quads");
            IncludeShader("Balls/Procedural");
            IncludeShader("Balls/Quads");

            Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            var cameraObject = new GameObject("Main Camera") { tag = "MainCamera" };
            var camera = cameraObject.AddComponent<Camera>();
            camera.orthographic = true;
            camera.orthographicSize = 9f;
            camera.transform.position = new Vector3(3f, 9f, -10f);
            camera.transform.rotation = Quaternion.identity;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(0.06f, 0.07f, 0.09f);
            cameraObject.AddComponent<AudioListener>();

            var bootstrapObject = new GameObject("Balls");
            var bootstrap = bootstrapObject.AddComponent<BallsBootstrap>();
            var serialized = new SerializedObject(bootstrap);
            serialized.FindProperty("ballMaterial").objectReferenceValue = ballMaterial;
            serialized.FindProperty("quadMaterial").objectReferenceValue = quadMaterial;
            serialized.ApplyModifiedPropertiesWithoutUndo();

            EditorSceneManager.SaveScene(scene, ScenePath);
            EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(ScenePath, true) };
            CopyNativePlugins();
            CopyLevels();
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            Debug.Log($"Balls project set up: {ScenePath}");
        }

        [MenuItem("Balls/Copy Native Plugin")]
        public static void CopyNativePlugins()
        {
            string repositoryRoot = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", ".."));
            string built = Path.Combine(repositoryRoot, "build", "bin", "Release");
            string destination = Path.Combine(Application.dataPath, "Plugins", "x86_64");
            Directory.CreateDirectory(destination);
            if (!Directory.Exists(built))
            {
                Debug.LogWarning($"no native build at {built}; run the CMake build with BALLS_BUILD_UNITY_PLUGIN before playing");
                return;
            }
            foreach (string file in Directory.GetFiles(built, "*.dll"))
            {
                string target = Path.Combine(destination, Path.GetFileName(file));
                File.Copy(file, target, true);
                WritePluginMeta(Path.GetFileNameWithoutExtension(file));
            }
            AssetDatabase.Refresh();
            Debug.Log($"copied native plugins from {built}");
        }

        /// <summary>
        /// A native plugin copied in with File.Copy arrives without import settings, and a plugin with no platform
        /// entry is left out of builds. Writing the importer block makes the rules explicit: editor on Windows and
        /// the Win64 player both load it.
        /// </summary>
        private static void WritePluginMeta(string pluginName)
        {
            string assetPath = $"Assets/Plugins/x86_64/{pluginName}.dll";
            string metaPath = assetPath + ".meta";
            string guid = StableGuid(pluginName);
            string meta =
                "fileFormatVersion: 2\n" +
                $"guid: {guid}\n" +
                "PluginImporter:\n" +
                "  externalObjects: {}\n" +
                "  serializedVersion: 2\n" +
                "  iconMap: {}\n" +
                "  executionOrder: {}\n" +
                "  defineConstraints: []\n" +
                "  isPreloaded: 0\n" +
                "  isOverridable: 0\n" +
                "  isExplicitlyReferenced: 0\n" +
                "  validateReferences: 1\n" +
                "  platformData:\n" +
                "  - first:\n" +
                "      Any: \n" +
                "    second:\n" +
                "      enabled: 0\n" +
                "      settings: {}\n" +
                "  - first:\n" +
                "      Editor: Editor\n" +
                "    second:\n" +
                "      enabled: 1\n" +
                "      settings:\n" +
                "        CPU: x86_64\n" +
                "        DefaultValueInitialized: true\n" +
                "        OS: Windows\n" +
                "  - first:\n" +
                "      Standalone: Win64\n" +
                "    second:\n" +
                "      enabled: 1\n" +
                "      settings:\n" +
                "        CPU: x86_64\n" +
                "  userData: \n" +
                "  assetBundleName: \n" +
                "  assetBundleVariant: \n";
            if (!File.Exists(metaPath) || !File.ReadAllText(metaPath).Contains("PluginImporter"))
            {
                File.WriteAllText(metaPath, meta);
            }
            AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceUpdate);
        }

        /// <summary>A GUID that stays the same between machines, so metas churn nothing in git.</summary>
        private static string StableGuid(string name)
        {
            using var md5 = System.Security.Cryptography.MD5.Create();
            byte[] hash = md5.ComputeHash(System.Text.Encoding.UTF8.GetBytes("balls." + name));
            var text = new System.Text.StringBuilder(32);
            for (int index = 0; index < 16; ++index)
            {
                text.Append(hash[index].ToString("x2"));
            }
            return text.ToString();
        }

        [MenuItem("Balls/Copy Levels")]
        public static void CopyLevels()
        {
            string repositoryRoot = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", ".."));
            string source = Path.Combine(repositoryRoot, "data", "assets", "levels");
            string destination = Path.Combine(Application.streamingAssetsPath, "levels");
            if (!Directory.Exists(source))
            {
                Debug.LogWarning($"no level data at {source}");
                return;
            }
            Directory.CreateDirectory(destination);
            foreach (string directory in Directory.GetDirectories(source))
            {
                string target = Path.Combine(destination, Path.GetFileName(directory));
                Directory.CreateDirectory(target);
                foreach (string file in Directory.GetFiles(directory, "*.txt"))
                {
                    File.Copy(file, Path.Combine(target, Path.GetFileName(file)), true);
                }
            }
            Debug.Log($"copied levels into {destination}");
        }

        private static Material CreateMaterial(string name, string shaderName)
        {
            string path = $"{MaterialDirectory}/{name}.mat";
            Shader shader = Shader.Find(shaderName);
            if (shader == null)
            {
                Debug.LogError($"shader {shaderName} not found");
                return null;
            }
            var material = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (material == null)
            {
                material = new Material(shader) { name = name };
                AssetDatabase.CreateAsset(material, path);
            }
            else
            {
                material.shader = shader;
            }
            if (shaderName == "Balls/Procedural")
            {
                material.SetFloat("_Radius", 0.2f);
            }
            EditorUtility.SetDirty(material);
            return material;
        }

        private static void IncludeShader(string shaderName)
        {
            Shader shader = Shader.Find(shaderName);
            if (shader == null)
            {
                return;
            }
            var graphics = AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/GraphicsSettings.asset");
            if (graphics == null || graphics.Length == 0)
            {
                return;
            }
            var serialized = new SerializedObject(graphics[0]);
            SerializedProperty list = serialized.FindProperty("m_AlwaysIncludedShaders");
            if (list == null)
            {
                return;
            }
            for (int index = 0; index < list.arraySize; ++index)
            {
                if (list.GetArrayElementAtIndex(index).objectReferenceValue == shader)
                {
                    return;
                }
            }
            list.InsertArrayElementAtIndex(list.arraySize);
            list.GetArrayElementAtIndex(list.arraySize - 1).objectReferenceValue = shader;
            serialized.ApplyModifiedProperties();
        }
    }
}
