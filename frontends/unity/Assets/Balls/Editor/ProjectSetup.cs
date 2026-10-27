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

            // The game's input model is mouse and keyboard, and the bootstrap reads it through UnityEngine.Input;
            // that needs the old backend enabled alongside the new Input System package.
            UseBothInputBackends();

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
                WritePluginMeta(Path.GetFileName(file), false);
            }
            // Linux libraries come from the client machine, not from this build tree: they are ELF files and are
            // built where Linux runs them.
            string linuxSource = Path.Combine(Application.dataPath, "..", "NativeLibraries", "linux-x86_64");
            if (Directory.Exists(linuxSource))
            {
                foreach (string file in Directory.GetFiles(linuxSource, "*.so"))
                {
                    File.Copy(file, Path.Combine(destination, Path.GetFileName(file)), true);
                    WritePluginMeta(Path.GetFileName(file), true);
                }
            }
            AssetDatabase.Refresh();
            Debug.Log($"copied native plugins from {built}");
        }

        /// <summary>
        /// A native plugin copied in with File.Copy arrives without import settings, and a plugin with no
        /// platform entry is left out of builds. Writing the importer block makes the rules explicit: a
        /// Windows DLL loads in the editor and the Win64 player, a Linux ELF only on Linux.
        /// </summary>
        private static void WritePluginMeta(string fileName, bool linux)
        {
            string assetPath = "Assets/Plugins/x86_64/" + fileName;
            string metaPath = assetPath + ".meta";
            string editorOs = linux ? "Linux" : "Windows";
            string playerPlatform = linux ? "Linux64" : "Win64";
            var meta = new System.Text.StringBuilder();
            meta.Append("fileFormatVersion: 2\n");
            meta.Append("guid: ").Append(StableGuid(fileName)).Append("\n");
            meta.Append("PluginImporter:\n");
            meta.Append("  externalObjects: {}\n");
            meta.Append("  serializedVersion: 2\n");
            meta.Append("  iconMap: {}\n");
            meta.Append("  executionOrder: {}\n");
            meta.Append("  defineConstraints: []\n");
            meta.Append("  isPreloaded: 0\n");
            meta.Append("  isOverridable: 0\n");
            meta.Append("  isExplicitlyReferenced: 0\n");
            meta.Append("  validateReferences: 1\n");
            meta.Append("  platformData:\n");
            meta.Append("  - first:\n");
            meta.Append("      Any: \n");
            meta.Append("    second:\n");
            meta.Append("      enabled: 0\n");
            meta.Append("      settings: {}\n");
            meta.Append("  - first:\n");
            meta.Append("      Editor: Editor\n");
            meta.Append("    second:\n");
            meta.Append("      enabled: 1\n");
            meta.Append("      settings:\n");
            meta.Append("        CPU: x86_64\n");
            meta.Append("        DefaultValueInitialized: true\n");
            meta.Append("        OS: ").Append(editorOs).Append("\n");
            meta.Append("  - first:\n");
            meta.Append("      Standalone: ").Append(playerPlatform).Append("\n");
            meta.Append("    second:\n");
            meta.Append("      enabled: 1\n");
            meta.Append("      settings:\n");
            meta.Append("        CPU: x86_64\n");
            meta.Append("  userData: \n");
            meta.Append("  assetBundleName: \n");
            meta.Append("  assetBundleVariant: \n");
            if (!File.Exists(metaPath) || !File.ReadAllText(metaPath).Contains("PluginImporter") ||
                !File.ReadAllText(metaPath).Contains(playerPlatform))
            {
                File.WriteAllText(metaPath, meta.ToString());
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

        /// <summary>
        /// Player Settings has three input modes and only "Both" keeps UnityEngine.Input alive next to the Input
        /// System package the URP template installs; the setting has no public API, so it goes through the asset.
        /// </summary>
        private static void UseBothInputBackends()
        {
            UnityEngine.Object[] settings = AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/ProjectSettings.asset");
            if (settings.Length == 0)
            {
                Debug.LogWarning("could not open ProjectSettings.asset to set the input handler");
                return;
            }
            var serialized = new SerializedObject(settings[0]);
            SerializedProperty handler = serialized.FindProperty("activeInputHandler");
            if (handler != null && handler.intValue != 2)
            {
                handler.intValue = 2;
                serialized.ApplyModifiedProperties();
                Debug.Log("set Active Input Handling to Both");
            }
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
