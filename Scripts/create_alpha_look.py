"""Run once with Unreal's Python editor commandlet; writes only /Game/AlphaLook.

The original tutorial materials/maps are deliberately not loaded or changed.
Colors are copied from Unity's saved AlphaPlayground material assets.
"""
import unreal

ROOT = "/Game/AlphaLook"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
assets.make_directory(ROOT)


def linear(rgb):
    def channel(v):
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4
    return unreal.LinearColor(*(channel(v) for v in rgb), 1.)


def expression(material, cls, x, y, **properties):
    node = mel.create_material_expression(material, cls, x, y)
    for name, value in properties.items():
        node.set_editor_property(name, value)
    return node


def base_material(name, unlit):
    path = ROOT + "/" + name
    material = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if not material:
        material = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(material)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    color = expression(material, unreal.MaterialExpressionVectorParameter, -600, -100,
                       parameter_name="Color", default_value=unreal.LinearColor(1., 1., 1., 1.))
    emission = expression(material, unreal.MaterialExpressionScalarParameter, -600, 120,
                          parameter_name="Emission", default_value=1. if unlit else 0.)
    glow = expression(material, unreal.MaterialExpressionMultiply, -250, 120)
    mel.connect_material_expressions(color, "", glow, "A")
    mel.connect_material_expressions(emission, "", glow, "B")
    if not unlit:
        emission_color = expression(material, unreal.MaterialExpressionVectorParameter, -600, 520,
                                    parameter_name="EmissionColor", default_value=unreal.LinearColor(0., 0., 0., 1.))
        combined = expression(material, unreal.MaterialExpressionAdd, -50, 120)
        mel.connect_material_expressions(glow, "", combined, "A")
        mel.connect_material_expressions(emission_color, "", combined, "B")
        mel.connect_material_property(combined, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        roughness = expression(material, unreal.MaterialExpressionScalarParameter, -300, 340,
                               parameter_name="Roughness", default_value=.82)
        mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        metallic = expression(material, unreal.MaterialExpressionScalarParameter, -300, 450,
                              parameter_name="Metallic", default_value=0.)
        mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    else:
        mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    return material


lit = base_material("M_AlphaLit", False)
unlit = base_material("M_AlphaUnlit", True)
effect = base_material("M_AlphaEffect", True)
effect.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
effect.set_editor_property("two_sided", True)
effect.set_editor_property("used_with_instanced_static_meshes", True)
opacity = expression(effect, unreal.MaterialExpressionScalarParameter, -300, 400,
                     parameter_name="Opacity", default_value=1.)
mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(effect)
assets.save_loaded_asset(effect)
palette = {
    "Floor": (.62, .48, .31), "FloorLight": (.81, .68, .45),
    "Stone": (.72, .58, .40), "Wall": (.27, .32, .39),
    "Gate": (.19, .29, .33), "Pit": (.20, .27, .29),
    "Backdrop": (.045, .31, .38), "Player": (.08, .78, .68),
    "Light": (.92, .97, .85), "Ink": (.045, .07, .11),
    "Crate": (.84, .39, .12), "CrateBand": (1., .64, .25),
    "Goal": (.12, .62, .40), "GoalOutline": (.45, 1., .73),
    "GoalSymbol": (.6, 1., .82), "Plate": (1., .62, .14),
    "PlateActive": (.18, .95, .57), "Indicator": (1., .50, .12),
    "IndicatorActive": (.22, 1., .62), "Takeoff": (1., .60, .13),
    "Landing": (.23, .88, .68), "BombGlow": (1., .61, .17),
}
line_colors = {"GoalOutline", "GoalSymbol", "Takeoff", "Landing", "Indicator", "IndicatorActive", "BombGlow"}
for key, rgb in palette.items():
    name = "M_Alpha_" + key
    path = ROOT + "/" + name
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if not instance:
        instance = tools.create_asset(name, ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, unlit if key in line_colors else lit)
    mel.set_material_instance_vector_parameter_value(instance, "Color", linear(rgb))
    mel.set_material_instance_scalar_parameter_value(instance, "Roughness", .82)
    mel.set_material_instance_scalar_parameter_value(instance, "Emission", 1. if key in line_colors else 0.)
    assets.save_loaded_asset(instance)

unreal.log("ALPHA_LOOK_READY: generated three reusable parameterized bases and {} palette instances in {}".format(len(palette), ROOT))
