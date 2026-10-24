"""Refresh the four fruit trees and keeper after native visual changes; preserve the layout."""
import os
import shutil
import hashlib
import datetime
import unreal

source = os.path.join(unreal.Paths.project_content_dir(), "Maps", "JungleBananaTrap.umap")
folder = os.path.join(unreal.Paths.project_saved_dir(), "Backups", "JunglePresentation")
os.makedirs(folder, exist_ok=True)
backup = os.path.join(folder, datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".umap")
shutil.copy2(source, backup)
with open(source, "rb") as a, open(backup, "rb") as b:
    assert hashlib.sha256(a.read()).digest() == hashlib.sha256(b.read()).digest()

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels.load_level("/Game/Maps/JungleBananaTrap")
tree_class = unreal.load_class(None, "/Script/BlastDelivery568.BDBananaTree")
keeper_class = unreal.load_class(None, "/Script/BlastDelivery568.BDWildMan")
replacements = []
for old in actors.get_all_level_actors():
    if old.get_class() not in (tree_class, keeper_class):
        continue
    new = actors.spawn_actor_from_class(old.get_class(), old.get_actor_location(), old.get_actor_rotation())
    assert new
    new.set_actor_label(old.get_actor_label())
    new.set_actor_scale3d(old.get_actor_scale3d())
    if old.get_class() == tree_class:
        for prop in ("has_bananas", "drop_offset"):
            new.set_editor_property(prop, old.get_editor_property(prop))
    else:
        for prop in ("passage_guard", "gate_opener", "chase_speed", "lure_speed"):
            new.set_editor_property(prop, old.get_editor_property(prop))
        new.set_editor_property("patrol_radius", 320.0)
    replacements.append((old, new))
assert len(replacements) == 5, "Expected exactly four trees and one keeper"
for old, new in replacements:
    actors.destroy_actor(old)
for actor in actors.get_all_level_actors():
    if actor.get_class().get_name() == "BDJungleSettings":
        actor.build_navigation()
assert levels.save_current_level()
unreal.log("JUNGLE_PRESENTATION_REFRESH_COMPLETE: backup=" + backup)
