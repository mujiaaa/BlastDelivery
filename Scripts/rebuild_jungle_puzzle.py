"""Rebuild only JungleBananaTrap using the project's existing shapes/materials.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Coordinates follow the sketch: -Y is the upper room; +Y is the player's room.
"""
import unreal
import os
import hashlib

PATH = "/Game/Maps/JungleBananaTrap"
# Refuse to replace the layout unless a byte-identical original is safely backed up.
# The original .umap is also tracked in Git. No other map is loaded or saved.
original = os.path.join(unreal.Paths.project_content_dir(), "Maps", "JungleBananaTrap.umap")
backup = os.path.join(unreal.Paths.project_saved_dir(), "Backups", "JungleRedesign", "JungleBananaTrap.umap")
def digest(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()
assert os.path.isfile(backup), "Create a verified original map backup before rebuilding"
assert digest(original) == digest(backup), "Source changed since backup; inspect before replacing"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels.load_level(PATH)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
nav_bounds = None
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.NavMeshBoundsVolume):
        nav_bounds = actor
        continue
    if isinstance(actor, (unreal.DirectionalLight, unreal.PostProcessVolume, unreal.RecastNavMesh)):
        continue
    actors.destroy_actor(actor)

def spawn(cls, label, pos, yaw=0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*pos), unreal.Rotator(yaw=yaw))
    actor.set_actor_label(label)
    return actor

def native(name):
    return unreal.load_class(None, "/Script/BlastDelivery568." + name)

def mesh(label, pos, size, material):
    actor = spawn(unreal.StaticMeshActor, label, pos)
    component = actor.static_mesh_component
    component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    component.set_material(0, unreal.load_asset("/Game/Materials/M_" + material))
    component.set_collision_profile_name("BlockAll" if label == "Jungle floor" else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
    return actor

mesh("Jungle floor", (0, 0, -35), (1960, 1360, 70), "JungleGrass")
mesh("Jungle backdrop", (0,0,-90), (6000,6000,30), "Backdrop")
mesh("Lower clearing", (0, 325, 1), (1800, 550, 2), "JungleLightGrass")
mesh("Gate trail", (0, 100, 3), (360, 600, 2), "JunglePath")
mesh("Delivery trail", (390, -350, 2), (850, 220, 2), "JungleLightGrass")

for label, pos, size in (
    ("Top boundary", (0,-650,0), (1960,60,100)),
    ("Bottom boundary", (0,650,0), (1960,60,75)),
    ("Left boundary", (-950,0,0), (60,1300,85)),
    ("Right boundary", (950,0,0), (60,1300,85)),
    ("Divider west", (-550,0,0), (800,55,125)),
    ("Divider east", (550,0,0), (800,55,125)),
):
    actor = spawn(native("BDBarrier"), label, pos)
    actor.configure(unreal.Vector(*size))
    actor.get_editor_property("mesh").set_material(0, unreal.load_asset("/Game/Materials/M_JungleStone"))

door = spawn(native("BDJungleDoor"), "Golden lock - keeper's double door", (0,0,0))
door.set_editor_property("tags", ["JunglePassage"])
spawn(unreal.PlayerStart, "Courier start - lower left", (-720,390,100))
spawn(native("BDBananaPackage"), "Banana delivery package", (-330,180,63))
goal = spawn(native("BDGoal"), "Delivery goal - upper right", (705,-455,100))
goal.set_editor_property("extent", unreal.Vector(180,145,100))

for label, pos, fruit in (
    ("Banana tree - lower middle", (-350,480,0), True),
    ("Banana tree - lower right", (720,440,0), True),
    ("Empty palm - upper left", (-730,-440,0), False),
    ("Empty palm - upper middle", (80,-480,0), False),
):
    tree = spawn(native("BDBananaTree"), label, pos)
    tree.set_editor_property("has_bananas", fruit)

for index, pos in enumerate(((-470,-200,0),(165,-255,0),(435,-190,0),(510,-460,0),(70,-370,0),(420,380,0))):
    peel = spawn(native("BDBananaPeel"), "Banana peel %d" % (index+1), pos, index*31)
    peel.set_editor_property("trigger_extent", unreal.Vector2D(45,45))

keeper = spawn(native("BDWildMan"), "Patrolling keeper - golden key", (-370,-340,100), 90)
keeper.set_editor_property("gate_opener", True)
keeper.set_editor_property("patrol_radius", 320.0)
settings = spawn(native("BDJungleSettings"), "Compact puzzle tuning", (0,0,0))
settings.set_editor_property("max_wild_men", 1)
settings.set_editor_property("spawn_points", [])
settings.set_editor_property("eating_seconds", 12.0)
settings.set_editor_property("detection_range", 230.0)
settings.set_editor_property("banana_attraction_range", 850.0)
settings.set_editor_property("gate_lure_range", 300.0)

if nav_bounds:
    _, extent = nav_bounds.get_actor_bounds(False)
    scale = nav_bounds.get_actor_scale3d()
    nav_bounds.set_actor_location(unreal.Vector(0,0,150), False, False)
    nav_bounds.set_actor_scale3d(unreal.Vector(scale.x*1050/extent.x, scale.y*750/extent.y, scale.z*400/extent.z))
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.RecastNavMesh):
        actor.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
world.get_world_settings().set_editor_property("default_game_mode", native("BDJungleGameMode"))
settings.build_navigation()
levels.save_current_level()
unreal.log("COMPACT_JUNGLE_REBUILD_COMPLETE")
