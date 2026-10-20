import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
root = '/Game/AlphaLook'
path = root + '/M_AlphaGlass'
glass = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset('M_AlphaGlass', root, unreal.Material, unreal.MaterialFactoryNew())
mel.delete_all_material_expressions(glass)
glass.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
glass.set_editor_property('two_sided', True)
glass.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
def param(material, cls, name, value, prop):
    item=mel.create_material_expression(material,cls)
    item.set_editor_property('parameter_name',name)
    item.set_editor_property('default_value',value)
    mel.connect_material_property(item,'',prop)
param(glass,unreal.MaterialExpressionVectorParameter,'Color',unreal.LinearColor(.7,.92,.94,1),unreal.MaterialProperty.MP_BASE_COLOR)
param(glass,unreal.MaterialExpressionScalarParameter,'Opacity',.10,unreal.MaterialProperty.MP_OPACITY)
param(glass,unreal.MaterialExpressionScalarParameter,'Roughness',.08,unreal.MaterialProperty.MP_ROUGHNESS)
mel.recompile_material(glass)
assets.save_loaded_asset(glass)
lit=assets.load_asset(root+'/M_AlphaLit')
if lit and 'Metallic' not in [str(n) for n in mel.get_scalar_parameter_names(lit)]:
    param(lit,unreal.MaterialExpressionScalarParameter,'Metallic',0.,unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(lit)
    assets.save_loaded_asset(lit)
unreal.log('ALPHA_GLASS_READY')
