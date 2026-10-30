# Creates Content/Balls/NaniteCube.uasset: the engine's basic cube with Nanite enabled, the brick mesh the Graphics
# page's Nanite switch needs (the engine ships no Nanite cube, and the project is otherwise code-only). Run it with
#   UnrealEditor-Cmd.exe Balls.uproject -run=pythonscript -script=<this file> -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended
import unreal

DESTINATION = "/Game/Balls"
NAME = "NaniteCube"

path = f"{DESTINATION}/{NAME}"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
source = unreal.load_asset("/Engine/BasicShapes/Cube")
mesh = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(NAME, DESTINATION, source)
settings = mesh.get_editor_property("nanite_settings")
settings.set_editor_property("enabled", True)
mesh.set_editor_property("nanite_settings", settings)
unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
unreal.log(f"MakeNaniteCube: saved {path}, Nanite {mesh.get_editor_property('nanite_settings').enabled}")
