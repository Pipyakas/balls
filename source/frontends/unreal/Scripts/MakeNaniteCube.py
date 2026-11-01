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
