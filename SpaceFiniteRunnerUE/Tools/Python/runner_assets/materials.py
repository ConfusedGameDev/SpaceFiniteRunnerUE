"""M1.3: the runner's master materials and their instances, rebuilt from the Unity shaders.

Every master is generated, so the graph is the code below; re-running rebuilds it in place. The maths
follows the Unity sources (Road / laser / hologram shader graphs, NeonRoad.shader, TrackReveal.hlsl,
RealToon as configured); Custom nodes carry the formulas that would be long node chains.
Instance values are the Unity material values (Plans/M1-AssetImport, spec from the .mat files).

Units: Unity object space is metres; Unreal local space is cm, so local positions are scaled by 0.01
before any Unity-tuned constant is applied. Axes: Unity (x, y, z) = Unreal local (y, z, x).
"""
import unreal

from .common import asset_exists, load, log, make, save, split_object_path, warn

MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

M = "/Game/Runner/Materials"
T = "/Game/Runner/Textures"
MPC_PATH = f"{M}/MPC_Runner"
MF_REVEAL_PATH = f"{M}/Functions/MF_TrackReveal"

# Track reveal globals (TrackReveal.hlsl), metres / radians; all 0 = the assembled track.
REVEAL_PARAMS = ["TrackRevealOn", "TrackRevealFront", "TrackRevealBand", "TrackRevealStagger",
                 "TrackRevealDrop", "TrackRevealTilt", "TrackRevealScale", "TrackRevealOvershoot"]
# Toon light (RealToon reads the main light; Unreal materials cannot, so the runner writes these).
# Defaults = the gallery light: Euler(40, -30, 0) in Unity, intensity 1.2, flat ambient (0.35, 0.35, 0.38).
TOON_VECTORS = {
    "ToonLightDirection": (-0.663, 0.383, 0.643, 0.0),  # world, towards the light
    "ToonLightColor": (1.2, 1.2, 1.2, 1.0),
    "ToonAmbient": (0.35, 0.35, 0.38, 1.0),
}

# Unity serializes material colours in gamma space and, in a linear project (SpaceFiniteRunner is),
# converts them to linear before the shader sees them; Unreal vector parameters are linear. So every
# colour value taken from a Unity material goes through srgb_to_linear. These vector parameters are
# not colours and pass through unchanged.
NON_COLOR_VECTORS = {"BaseMapTilingOffset", "LateralAxis", "UpAxis", "MainSpeed", "ScanLinesSpeed",
                     "ScanLinesTiling", "ToonLightDirection",
                     "ToonLightColor"}  # light colour x intensity, already linear


def _to_linear(c):
    """Unity's Mathf.GammaToLinearSpace (exact sRGB curve, applied to HDR values too)."""
    sign = -1.0 if c < 0 else 1.0
    c = abs(c)
    return sign * (c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)


def srgb_to_linear(rgba):
    """A Unity colour (r, g, b, a) as the shader receives it: rgb converted, alpha untouched."""
    return tuple(_to_linear(v) for v in rgba[:3]) + tuple(rgba[3:])


def param_value(name, rgba):
    return tuple(rgba) if name in NON_COLOR_VECTORS else srgb_to_linear(rgba)


TRACK_REVEAL_HLSL = r"""
// TrackReveal.hlsl, in Unreal units. Returns a world position offset (cm).
// Data = custom primitive data 0..2: stamp distance (m), seed 0..1, baked flag (0 = never revealed).
if (On < 0.5 || Data.z < 0.5) return float3(0, 0, 0);
float start = Data.x + Data.y * Stagger;
float t = saturate((Front - start) / max(1e-3, Band));
if (t >= 1.0) return float3(0, 0, 0);
float3 up = normalize(Up);
float3 ax = normalize(Axis);
if (t <= 0.0) return (Pivot - up * Drop * 100.0) - WP;   // collapsed out of sight
float u = t - 1.0;
float c = Overshoot;
float eased = 1.0 + (c + 1.0) * u * u * u + c * u * u;
float left = 1.0 - eased;
float side = Data.y < 0.5 ? -1.0 : 1.0;
float3 rel = WP - Pivot;
float s, co;
sincos(Tilt * left * side, s, co);
rel = rel * co + cross(ax, rel) * s + ax * dot(ax, rel) * (1.0 - co);
rel *= lerp(1.0, Scale, saturate(left));
float3 world = Pivot + rel - up * (Drop * 100.0 * left);
return world - WP;
"""

NEON_HLSL = r"""
// NeonRoad.shader fragment. Local position in cm; constants in Unity metres.
float3 p = LocalPos * 0.01;
float across = dot(p, LateralAxis);
float lateral = (across - MeshCenter) / max(1e-4, MeshHalfWidth);
float wallX = step(abs(dot(normalize(LocalNormal), UpAxis)), 0.5);
float wallY = dot(p, UpAxis);
float au = abs(lateral);
float aa = fwidth(au) + 1e-4;

float d, edgeMask, edgeHalo, laneMask, laneHalo, m, h;
d = abs(au - 1.0) - EdgeWidth;        edgeMask = 1.0 - smoothstep(-aa, aa, d); edgeHalo = exp(-max(0.0, d) / EdgeGlow) * EdgeGlowStrength * (1.0 - edgeMask);
if (OuterStrength > 0.0)
{
    d = abs(au - OuterOffset) - OuterWidth; m = 1.0 - smoothstep(-aa, aa, d); h = exp(-max(0.0, d) / EdgeGlow) * EdgeGlowStrength * (1.0 - m);
    edgeMask = max(edgeMask, m * OuterStrength); edgeHalo = max(edgeHalo, h * OuterStrength);
}
d = abs(au - LaneOffset) - LineWidth; laneMask = 1.0 - smoothstep(-aa, aa, d); laneHalo = exp(-max(0.0, d) / LineGlow) * LineGlowStrength * (1.0 - laneMask);
if (CenterLine > 0.5)
{
    d = au - LineWidth; m = 1.0 - smoothstep(-aa, aa, d); h = exp(-max(0.0, d) / LineGlow) * LineGlowStrength * (1.0 - m);
    laneMask = max(laneMask, m); laneHalo = max(laneHalo, h);
}
float pulse = 1.0 + PulseAmount * sin(Time * PulseSpeed);
float strip = WallStrip > 0.0 ? lerp(1.0, 1.0 - smoothstep(0.0, WallStrip, WallTop - wallY), wallX) : 1.0;
edgeMask *= strip; edgeHalo *= strip; laneMask *= strip; laneHalo *= strip;

float3 c = BaseColor;
c += LineColor * laneHalo;  c = lerp(c, LineColor * pulse, laneMask);
c += EdgeColor * edgeHalo;  c = lerp(c, EdgeColor * pulse, edgeMask);
return c;
"""

# Road / hologram graphs. Vertex: P * (1 + scan * flicker * VertexOffset), P in Unity metres -> offset in cm.
SCAN_VERTEX_HLSL = r"""
float2 uv = UV * Tiling + Time * Speed;
float4 scan = Texture2DSampleLevel(Scan, ScanSampler, uv, 0);
float4 flick = Texture2DSampleLevel(Flicker, FlickerSampler, float2(Time, Time), 0);
float3 k = scan.xyz * lerp(0.7, 1.0, flick.xyz);
float3 kUE = float3(k.z, k.x, k.y);           // Unity xyz -> Unreal local
return LocalPos * kUE * VertexOffset;         // local offset, cm
"""

# Fragment: sum = fresnelColor * fresnel + scan * flicker; road graph overlays it on the road texture.
SCAN_PIXEL_HLSL = r"""
float2 uv = UV * Tiling + Time * Speed;
// Custom inputs from vector parameters arrive as float3: keep everything float3.
float3 scan = Texture2DSample(Scan, ScanSampler, uv).rgb;
float st = sin(Time);
float3 flick = Texture2DSample(Flicker, FlickerSampler, float2(st, st)).rgb;
scan *= lerp(0.7, 1.0, flick);
float ndv = saturate(dot(normalize(N), normalize(V)));
float fres = pow(max(1.0 - ndv, 0.02), FresnelPower);   // Unity uses negative powers on the road: clamp the pole
fres = min(fres, 256.0);
float3 sum = FresnelColor.rgb * fres + scan;
if (UseOverlay < 0.5) return min(sum, 512.0);
float4 road = Texture2DSampleLevel(Road, RoadSampler, UV, 0);
float3 r1 = 1.0 - 2.0 * (1.0 - road.rgb) * (1.0 - sum);
float3 r2 = 2.0 * road.rgb * sum;
float3 z = step(road.rgb, 0.5);
float3 ov = r2 * z + (1.0 - z) * r1;
return clamp(lerp(road.rgb, ov, sum.x), 0.0, 512.0);
"""

TOON_HLSL = r"""
// RealToon (D_Default_URP) as the runner configures it: half-Lambert hard terminator, black shadow,
// flat ambient, optional self-lit; no rim, gloss, matcap or normal map.
float3 vc = MVCOL > 0.5 ? VertexColor : float3(1, 1, 1);
float3 tex = Texture2DSample(MainTex, MainTexSampler, UV).rgb * MainColor * MaiColPo * vc;
// MPC vectors arrive as float4.
float3 L = normalize(LightDir.xyz);
float3 LC = LightColor.rgb;
float3 AMB = Ambient.rgb;
float LV = max(LC.r, max(LC.g, LC.b));
float ndl = 0.5 * dot(L, normalize(N)) + 0.5;
float th = lerp(-1.0, 1.0, SelfShadowThreshold);
float k = lerp(7.0, th, th);
float ss = smoothstep(lerp(0.3, 1.0, SelfShadowHardness), 1.0, ndl * k);
float3 shadowCol = ShadowColor * tex * LV;
float3 litCol = tex * Highlight * HighlightPower * LC;
float3 direct = lerp(shadowCol, litCol, ss);
float3 gi = tex * AMB * EnvLightIntensity;
float m = Texture2DSample(SelfLitMask, SelfLitMaskSampler, UV).r;
gi = lerp(gi, lerp(gi, SelfLitColor * tex * tex * SelfLitPower, m), SelfLitIntensity);
direct = lerp(direct, lerp(direct, tex * TEXMCOLINT, m), SelfLitIntensity);
return direct + gi;
"""


# ---------------------------------------------------------------------------------------------------- graph helper

class Graph:
    """Builds a material (or material function) graph; nodes are laid out in a simple column grid."""

    def __init__(self, owner, is_function=False):
        self.owner = owner
        self.is_function = is_function
        self.y = {}

    def node(self, cls, col=-1, **props):
        y = self.y.get(col, 0)
        self.y[col] = y + 160
        x = col * 380
        if self.is_function:
            e = MEL.create_material_expression_in_function(self.owner, cls, x, y)
        else:
            e = MEL.create_material_expression(self.owner, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def scalar(self, name, default, group="", col=-3, **props):
        return self.node(unreal.MaterialExpressionScalarParameter, col, parameter_name=name, default_value=default,
                         group=group, **props)

    def vector(self, name, rgba, group="", col=-3):
        """rgba is the Unity value; colours are converted to linear (see NON_COLOR_VECTORS)."""
        return self.node(unreal.MaterialExpressionVectorParameter, col, parameter_name=name,
                         default_value=unreal.LinearColor(*param_value(name, rgba)), group=group)

    def texture(self, name, tex_path, group="", col=-3, sampler=None):
        props = dict(parameter_name=name, group=group, texture=load(tex_path))
        if sampler is not None:
            props["sampler_type"] = sampler
        return self.node(unreal.MaterialExpressionTextureObjectParameter, col, **props)

    def const3(self, xyz, col=-3):
        return self.node(unreal.MaterialExpressionConstant3Vector, col, constant=unreal.LinearColor(*xyz, 1.0))

    def mpc(self, name, col=-3):
        return self.node(unreal.MaterialExpressionCollectionParameter, col, collection=load(MPC_PATH),
                         parameter_name=name)

    def transform_local_to_world(self, source, col=-2):
        t = self.node(unreal.MaterialExpressionTransform, col,
                      transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                      transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        self.link(source, "", t, "")
        return t

    def transform_world_to_local(self, source, col=-2):
        t = self.node(unreal.MaterialExpressionTransform, col,
                      transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                      transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
        self.link(source, "", t, "")
        return t

    def custom(self, code, inputs, out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, desc="", col=-1):
        """inputs: ordered list of (name, expr[, output_name])."""
        c = self.node(unreal.MaterialExpressionCustom, col, code=code, output_type=out_type, description=desc)
        c.set_editor_property("inputs", [make(unreal.CustomInput, input_name=i[0]) for i in inputs])
        for i in inputs:
            self.link(i[1], i[2] if len(i) > 2 else "", c, i[0])
        return c

    def link(self, src, src_out, dst, dst_in):
        if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
            raise RuntimeError(f"cannot connect {src.get_name()}.{src_out} -> {dst.get_name()}.{dst_in}")

    def out(self, src, prop, src_out=""):
        if not MEL.connect_material_property(src, src_out, prop):
            raise RuntimeError(f"cannot connect {src.get_name()} to {prop}")


def _fresh(path, cls, factory):
    """Loads the asset at path (clearing a material's graph) or creates it."""
    folder, name = split_object_path(path)
    if asset_exists(path):
        asset = load(path)
        if isinstance(asset, unreal.Material):
            MEL.delete_all_material_expressions(asset)
        elif isinstance(asset, unreal.MaterialFunction):
            MEL.delete_all_material_expressions_in_function(asset)
        return asset
    return TOOLS.create_asset(name, folder, cls, factory)


# ---------------------------------------------------------------------------------------------------- shared pieces

def build_mpc(report):
    mpc = _fresh(MPC_PATH, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    mpc.set_editor_property("scalar_parameters", [
        make(unreal.CollectionScalarParameter, parameter_name=n, default_value=0.0) for n in REVEAL_PARAMS])
    mpc.set_editor_property("vector_parameters", [
        make(unreal.CollectionVectorParameter, parameter_name=n, default_value=unreal.LinearColor(*param_value(n, v)))
        for n, v in TOON_VECTORS.items()])
    save(MPC_PATH)
    report.add("BUILT", MPC_PATH, f"{len(REVEAL_PARAMS)} reveal scalars, {len(TOON_VECTORS)} toon vectors")


def build_reveal_function(report):
    mf = _fresh(MF_REVEAL_PATH, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    mf.set_editor_property("description", "Track reveal (TrackReveal.hlsl): world position offset for a stamped "
                                          "track piece. Per piece: custom primitive data 0 = stamp distance (m), "
                                          "1 = seed 0..1, 2 = baked flag. Globals in MPC_Runner.")
    mf.set_editor_property("expose_to_library", True)
    g = Graph(mf, is_function=True)
    inputs = _reveal_inputs(g)
    c = g.custom(TRACK_REVEAL_HLSL, inputs, desc="TrackReveal")
    o = g.node(unreal.MaterialExpressionFunctionOutput, 0, output_name="WPO")
    g.link(c, "", o, "")
    MEL.update_material_function(mf)
    save(MF_REVEAL_PATH)
    report.add("BUILT", MF_REVEAL_PATH)


def _reveal_inputs(g):
    wp = g.node(unreal.MaterialExpressionWorldPosition, -3,
                world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    pivot = g.node(unreal.MaterialExpressionObjectPositionWS, -3)
    up = g.transform_local_to_world(g.const3((0.0, 0.0, 1.0)))     # Unity local +Y
    axis = g.transform_local_to_world(g.const3((0.0, 1.0, 0.0)))   # Unity local +X
    data = g.node(unreal.MaterialExpressionAppendVector, -2)
    d01 = g.node(unreal.MaterialExpressionAppendVector, -2)
    for idx, name in enumerate(["RevealDistance", "RevealSeed", "RevealBaked"]):
        p = g.scalar(name, 0.0, "Track Reveal", use_custom_primitive_data=True, primitive_data_index=idx)
        if idx == 0:
            g.link(p, "", d01, "A")
        elif idx == 1:
            g.link(p, "", d01, "B")
        else:
            g.link(d01, "", data, "A")
            g.link(p, "", data, "B")
    ins = [("WP", wp), ("Pivot", pivot), ("Up", up), ("Axis", axis), ("Data", data)]
    for name in REVEAL_PARAMS:
        ins.append((name.replace("TrackReveal", ""), g.mpc(name)))
    return ins


def _reveal_wpo(g):
    """The reveal offset inside a material: calls MF_TrackReveal."""
    call = g.node(unreal.MaterialExpressionMaterialFunctionCall, -1)
    if not call.set_material_function(load(MF_REVEAL_PATH)):
        raise RuntimeError("cannot bind MF_TrackReveal")
    return call


def _new_material(path, shading=unreal.MaterialShadingModel.MSM_UNLIT, blend=unreal.BlendMode.BLEND_OPAQUE,
                  two_sided=False):
    mat = _fresh(path, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", shading)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("two_sided", two_sided)
    return mat, Graph(mat)


def _finish(mat, path, report, note=""):
    MEL.recompile_material(mat)
    save(path)
    report.add("BUILT", path, note)


# ---------------------------------------------------------------------------------------------------- masters

def build_neon(report):
    path = f"{M}/Track/M_Neon"
    mat, g = _new_material(path, two_sided=True)
    ins = [("LocalPos", g.node(unreal.MaterialExpressionLocalPosition, -3)),
           ("LocalNormal", g.transform_world_to_local(g.node(unreal.MaterialExpressionVertexNormalWS, -3))),
           ("Time", g.node(unreal.MaterialExpressionTime, -3))]
    vectors = {"BaseColor": (0.01, 0.01, 0.018, 1), "EdgeColor": (4, 1.1, 0.12, 1), "LineColor": (0.5, 2, 6, 1),
               # Unity _LateralAxis Z (0) = Unreal local X; local Y = Unreal local Z.
               "LateralAxis": (1, 0, 0, 0), "UpAxis": (0, 0, 1, 0)}
    scalars = {"EdgeWidth": 0.05, "EdgeGlow": 0.18, "EdgeGlowStrength": 0.3, "OuterOffset": 1.48, "OuterStrength": 0.0,
               "OuterWidth": 0.01, "LaneOffset": 0.34, "LineWidth": 0.012, "LineGlow": 0.1, "LineGlowStrength": 0.25,
               "CenterLine": 0.0, "PulseSpeed": 1.5, "PulseAmount": 0.12, "MeshCenter": 0.0, "MeshHalfWidth": 0.5,
               "WallTop": 0.0, "WallStrip": 0.0}
    for n, v in vectors.items():
        ins.append((n, g.vector(n, v, "Neon")))
    for n, v in scalars.items():
        ins.append((n, g.scalar(n, v, "Neon")))
    color = g.custom(NEON_HLSL, ins, desc="NeonRoad")
    g.out(color, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(_reveal_wpo(g), unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    _finish(mat, path, report, "unlit opaque two-sided, NeonRoad.shader + reveal")


def _scan_graph(g, with_road):
    uv = g.node(unreal.MaterialExpressionTextureCoordinate, -3, coordinate_index=0)
    time = g.node(unreal.MaterialExpressionTime, -3)
    tiling = g.vector("ScanLinesTiling", (1, 1, 0, 0), "Scan Lines")
    speed = g.vector("ScanLinesSpeed", (0, 0, 0, 0), "Scan Lines")
    tiling2 = g.node(unreal.MaterialExpressionComponentMask, -2, r=True, g=True, b=False, a=False)
    speed2 = g.node(unreal.MaterialExpressionComponentMask, -2, r=True, g=True, b=False, a=False)
    g.link(tiling, "", tiling2, "")
    g.link(speed, "", speed2, "")
    scan = g.texture("ScanLinesTexture", f"{T}/Track/T_ScanLines", "Scan Lines")
    flick = g.texture("FlickerTexture", f"{T}/Track/T_Noise", "Scan Lines")
    vo = g.scalar("VertexOffset", 0.05, "Scan Lines")

    local = g.node(unreal.MaterialExpressionLocalPosition, -3)
    vtx = g.custom(SCAN_VERTEX_HLSL, [("UV", uv), ("Time", time), ("Tiling", tiling2), ("Speed", speed2),
                                      ("Scan", scan), ("Flicker", flick), ("VertexOffset", vo), ("LocalPos", local)],
                   desc="ScanVertexOffset")
    wpo = g.transform_local_to_world(vtx, col=0)

    fres_color = g.vector("FresnelColor", (1, 1, 1, 1), "Fresnel")
    fres_power = g.scalar("FresnelPower", 3.0, "Fresnel")
    road = g.texture("ColorTexture", f"{T}/Track/T_RoadTexture_v2", "Road")
    use_overlay = g.node(unreal.MaterialExpressionConstant, -3, r=1.0 if with_road else 0.0)
    pix = g.custom(SCAN_PIXEL_HLSL, [("UV", uv), ("Time", time), ("Tiling", tiling2), ("Speed", speed2),
                                     ("Scan", scan), ("Flicker", flick),
                                     ("N", g.node(unreal.MaterialExpressionVertexNormalWS, -3)),
                                     ("V", g.node(unreal.MaterialExpressionCameraVectorWS, -3)),
                                     ("FresnelColor", fres_color), ("FresnelPower", fres_power),
                                     ("Road", road), ("UseOverlay", use_overlay)], desc="ScanFresnel")
    return pix, wpo


def build_road(report):
    path = f"{M}/Track/M_Road"
    mat, g = _new_material(path, blend=unreal.BlendMode.BLEND_ADDITIVE, two_sided=True)
    pix, wpo = _scan_graph(g, with_road=True)
    g.out(pix, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.scalar("ColorPower", 0.0, "Road"), unreal.MaterialProperty.MP_OPACITY)
    total = g.node(unreal.MaterialExpressionAdd, 0)
    g.link(wpo, "", total, "A")
    g.link(_reveal_wpo(g), "", total, "B")
    g.out(total, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    _finish(mat, path, report, "unlit additive two-sided, Road.shadergraph + reveal")


def build_hologram(report):
    path = f"{M}/Orbs/M_Hologram"
    mat, g = _new_material(path, blend=unreal.BlendMode.BLEND_TRANSLUCENT, two_sided=True)
    pix, wpo = _scan_graph(g, with_road=False)
    g.out(pix, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # hologram.mat culls front faces (_Cull 1): two-sided with the front side's opacity zeroed.
    sign = g.node(unreal.MaterialExpressionTwoSidedSign, -2)
    back_only = g.scalar("BackFacesOnly", 1.0, "Road")
    op = g.custom("float back = Sign < 0 ? 1.0 : 0.0; return ColorPower * lerp(1.0, back, BackFacesOnly);",
                  [("Sign", sign), ("ColorPower", g.scalar("ColorPower", 0.0, "Road")), ("BackFacesOnly", back_only)],
                  out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, desc="Opacity")
    g.out(op, unreal.MaterialProperty.MP_OPACITY)
    g.out(wpo, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    _finish(mat, path, report, "unlit translucent, hologram.shadergraph (back faces only)")


def build_laser(report):
    path = f"{M}/Laser/M_Laser"
    mat, g = _new_material(path, blend=unreal.BlendMode.BLEND_TRANSLUCENT)
    code = r"""
float4 main = Texture2DSample(MainTex, MainTexSampler, UV + Speed * Time);
float4 mask = Texture2DSample(Mask, MaskSampler, UV);
Opacity = mask.r * main.a * VCA;
return Color * (mask.rgb * main.rgb) * VC;
"""
    vc = g.node(unreal.MaterialExpressionVertexColor, -3)
    speed = g.node(unreal.MaterialExpressionComponentMask, -2, r=True, g=True, b=False, a=False)
    g.link(g.vector("MainSpeed", (-0.5, 0, 0, 0), "Laser"), "", speed, "")
    c = g.custom(code, [("UV", g.node(unreal.MaterialExpressionTextureCoordinate, -3, coordinate_index=0)),
                        ("Time", g.node(unreal.MaterialExpressionTime, -3)),
                        ("Speed", speed),
                        ("MainTex", g.texture("MainTexture", f"{T}/Track/T_MulticolorIndustrialGlitcho_Jpg", "Laser")),
                        ("Mask", g.texture("Mask", "/Engine/EngineResources/WhiteSquareTexture", "Laser")),
                        ("Color", g.vector("Color", (1, 1, 1, 1), "Laser")),
                        ("VC", vc), ("VCA", vc, "A")], desc="Laser")
    c.set_editor_property("additional_outputs", [
        make(unreal.CustomOutput, output_name="Opacity", output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
    g.out(c, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(c, unreal.MaterialProperty.MP_OPACITY, "Opacity")
    g.out(_reveal_wpo(g), unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    _finish(mat, path, report, "unlit translucent, laser.shadergraph + reveal")


def build_toon(report):
    path = f"{M}/Toon/M_Toon"
    mat, g = _new_material(path)
    ins = [("UV", g.node(unreal.MaterialExpressionTextureCoordinate, -3, coordinate_index=0)),
           ("VertexColor", g.node(unreal.MaterialExpressionVertexColor, -3)),
           ("N", g.node(unreal.MaterialExpressionVertexNormalWS, -3)),
           ("LightDir", g.mpc("ToonLightDirection")), ("LightColor", g.mpc("ToonLightColor")),
           ("Ambient", g.mpc("ToonAmbient")),
           ("MainTex", g.texture("MainTex", "/Engine/EngineResources/WhiteSquareTexture", "Base")),
           ("SelfLitMask", g.texture("SelfLitMask", "/Engine/EngineResources/WhiteSquareTexture", "Self Lit"))]
    vectors = {"MainColor": ("Base", (1, 1, 1, 1)), "Highlight": ("Light", (1, 1, 1, 1)),
               "ShadowColor": ("Light", (0, 0, 0, 1)), "SelfLitColor": ("Self Lit", (1, 1, 1, 1))}
    scalars = {"MaiColPo": ("Base", 0.8), "MVCOL": ("Base", 0.0), "HighlightPower": ("Light", 1.0),
               "SelfShadowThreshold": ("Light", 0.93), "SelfShadowHardness": ("Light", 1.0),
               "EnvLightIntensity": ("Light", 1.0), "SelfLitIntensity": ("Self Lit", 0.0),
               "SelfLitPower": ("Self Lit", 2.0), "TEXMCOLINT": ("Self Lit", 1.0)}
    for n, (grp, v) in vectors.items():
        ins.append((n, g.vector(n, v, grp)))
    for n, (grp, v) in scalars.items():
        ins.append((n, g.scalar(n, v, grp)))
    c = g.custom(TOON_HLSL, ins, desc="RealToon")
    g.out(c, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat, path, report, "unlit, RealToon look lit from MPC_Runner")

    # RealToon outline: inverted hull, black, width in Unity object units x 0.01. As an overlay material:
    # two-sided, front faces masked away, pushed out along the normal.
    opath = f"{M}/Toon/M_ToonOutline"
    omat, og = _new_material(opath, blend=unreal.BlendMode.BLEND_MASKED, two_sided=True)
    width = og.scalar("OutlineWidth", 0.5, "Outline")
    push = og.custom("return normalize(LocalNormal) * Width * 0.01 * 100.0;",
                     [("LocalNormal", og.transform_world_to_local(og.node(unreal.MaterialExpressionVertexNormalWS, -3))),
                      ("Width", width)], desc="OutlinePush")
    og.out(og.transform_local_to_world(push, col=0), unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mask = og.custom("return Sign < 0 ? 1.0 : 0.0;", [("Sign", og.node(unreal.MaterialExpressionTwoSidedSign, -2))],
                     out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, desc="BackFaces")
    og.out(mask, unreal.MaterialProperty.MP_OPACITY_MASK)
    og.out(og.vector("OutlineColor", (0, 0, 0, 1), "Outline"), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(omat, opath, report, "inverted-hull outline overlay")


def _flat_normal():
    for p in ("/Engine/EngineMaterials/DefaultNormal", "/Engine/EngineMaterials/FlatNormal",
              "/Engine/EngineMaterials/T_Default_Normal"):
        t = load(p)
        if isinstance(t, unreal.Texture2D):
            return t
        warn(f"flat normal candidate {p}: {type(t).__name__ if t else 'not found'}")
    raise RuntimeError("no engine flat normal Texture2D found")


def build_lit(report):
    for path, blend in ((f"{M}/Lit/M_Lit", unreal.BlendMode.BLEND_MASKED),
                        (f"{M}/Lit/M_LitTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT)):
        mat, g = _new_material(path, shading=unreal.MaterialShadingModel.MSM_DEFAULT_LIT, blend=blend)
        if blend == unreal.BlendMode.BLEND_TRANSLUCENT:
            mat.set_editor_property("translucency_lighting_mode",
                                    unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        uv = g.node(unreal.MaterialExpressionTextureCoordinate, -3, coordinate_index=0)
        st = g.vector("BaseMapTilingOffset", (1, 1, 0, 0), "Base")
        uvc = g.custom("return UV * ST.xy + float2(ST.z, STW);", [("UV", uv), ("ST", st), ("STW", st, "A")],
                       out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT2, desc="TilingOffset")
        white = "/Engine/EngineResources/WhiteSquareTexture"
        code = r"""
float4 b = Texture2DSample(BaseMap, BaseMapSampler, UV);
Emissive = Texture2DSample(EmissionMap, EmissionMapSampler, UV).rgb * EmissionColor;
Alpha = b.a * BaseColorA;
return b.rgb * BaseColor;
"""
        base_color = g.vector("BaseColor", (1, 1, 1, 1), "Base")
        c = g.custom(code, [("UV", uvc), ("BaseMap", g.texture("BaseMap", white, "Base")),
                            ("BaseColor", base_color), ("BaseColorA", base_color, "A"),
                            ("EmissionMap", g.texture("EmissionMap", white, "Emission")),
                            ("EmissionColor", g.vector("EmissionColor", (0, 0, 0, 1), "Emission"))], desc="Base")
        c.set_editor_property("additional_outputs", [
            make(unreal.CustomOutput, output_name="Emissive", output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3),
            make(unreal.CustomOutput, output_name="Alpha", output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
        g.out(c, unreal.MaterialProperty.MP_BASE_COLOR)
        g.out(c, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")
        g.out(g.scalar("Metallic", 0.0, "Surface"), unreal.MaterialProperty.MP_METALLIC)
        g.out(g.scalar("Roughness", 0.5, "Surface"), unreal.MaterialProperty.MP_ROUGHNESS)
        g.out(g.scalar("Specular", 0.5, "Surface"), unreal.MaterialProperty.MP_SPECULAR)
        normal = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -2, parameter_name="NormalMap", group="Surface",
                        texture=_flat_normal(),
                        sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        g.link(uvc, "", normal, "UVs")
        g.out(normal, unreal.MaterialProperty.MP_NORMAL, "RGB")
        if blend == unreal.BlendMode.BLEND_MASKED:
            # FUSELAJE renders back faces only (_Cull 1); its instance turns this on with two-sided.
            m = g.custom("return BackFacesOnly > 0.5 ? (Sign < 0 ? 1.0 : 0.0) : 1.0;",
                         [("Sign", g.node(unreal.MaterialExpressionTwoSidedSign, -2)),
                          ("BackFacesOnly", g.scalar("BackFacesOnly", 0.0, "Surface"))],
                         out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, desc="Faces")
            g.out(m, unreal.MaterialProperty.MP_OPACITY_MASK)
        else:
            op = g.node(unreal.MaterialExpressionMultiply, 0)
            g.link(c, "Alpha", op, "A")
            g.link(g.scalar("Opacity", 1.0, "Base"), "", op, "B")
            g.out(op, unreal.MaterialProperty.MP_OPACITY)
        _finish(mat, path, report, "default lit (URP Lit)")


# ---------------------------------------------------------------------------------------------------- instances

TX = lambda p: f"{T}/{p}"  # noqa: E731
GLOSS_BAR = "/Game/Runner/UI/Panels/T_GreyDefault_Bar_round_gloss_large"
GLOSS_BAR_DOUBLE = "/Game/Runner/UI/Panels/T_GreyDouble_Bar_round_gloss_large"


def _toon(color, **kw):
    v = {"MainColor": color}
    v.update(kw)
    return v


def _livery(rgba, hue, min_saturation=0.75, min_value=0.55):
    """
    A ship part's colour as a fresh install paints it: PF_Ship's VehicleLivery puts the RealToon materials
    on the model and VehiclePaintTarget.Tint sets _MainColor from the hue in VehicleColorProfile.asset
    (authored saturation and brightness kept, with floors). Gamma space, like the Unity value it replaces.
    """
    import colorsys
    _, s, v = colorsys.rgb_to_hsv(*rgba[:3])
    r, g, b = colorsys.hsv_to_rgb(hue % 1.0, max(s, min_saturation), max(v, min_value))
    return (r, g, b, rgba[3])


# VehicleColorProfile.asset, ship.nabucodonosor: slot id (the Unity material name) -> hue; all tinted.
SHIP_HUES = {"PiezasUNOmat": 0.014646806, "vidrio": 0.4746166, "ESCAPES": 0.086003944,
             "PROPULSORmat": 0.9952961, "PRINCIPALmat": 0.9977361, "PUNTAmat": 0.9813193}


def _selflit(color):
    return {"SelfLitIntensity": 0.6, "SelfLitColor": color}


# path -> (parent, {param: value}, extra) ; value types: float, tuple (color), str (texture path)
INSTANCES = {
    # Road.shadergraph
    f"{M}/Track/MI_RoadHologram": (f"{M}/Track/M_Road", {
        "FresnelColor": (0, 0.9212071, 2.932511, 0.12156863), "FresnelPower": -3.66,
        "ScanLinesTiling": (0, 37.48, 0, 0), "ScanLinesSpeed": (0, 0.1, 0, 0), "VertexOffset": 0.009,
        "ColorPower": 0.71, "ScanLinesTexture": TX("Track/T_ScanLines"), "FlickerTexture": TX("Track/T_Noise"),
        "ColorTexture": TX("Track/T_RoadTexture_v2")}, {}),
    f"{M}/Track/MI_NeonRoad_Mat": (f"{M}/Track/MI_RoadHologram", {"VertexOffset": -0.03, "FresnelPower": -2.06}, {}),
    # NeonRoad.shader
    f"{M}/Track/MI_NeonEdge_Mat": (f"{M}/Track/M_Neon", {
        "BaseColor": (0.008, 0.008, 0.014, 1), "EdgeColor": (2.2, 0.6, 0.06, 1), "EdgeWidth": 1.0, "EdgeGlow": 0.08,
        "EdgeGlowStrength": 0.15, "LineColor": (0.35, 1.2, 3.2, 1), "LineWidth": 0.007, "LineGlow": 0.05,
        "LineGlowStrength": 0.12, "PulseAmount": 0.1}, {}),
    f"{M}/Track/MI_NeonTube_Mat": (f"{M}/Track/M_Neon", {
        "BaseColor": (0.008, 0.008, 0.014, 1), "EdgeColor": (0.2, 0.7, 1.8, 1), "EdgeWidth": 0.01, "EdgeGlow": 0.05,
        "EdgeGlowStrength": 0.08, "LineColor": (0.35, 1.2, 3.2, 1), "LineWidth": 0.0, "LineGlow": 0.05,
        "LineGlowStrength": 0.0, "PulseAmount": 0.1, "WallTop": 0.08}, {}),
    f"{M}/Track/MI_NeonWall_Mat": (f"{M}/Track/M_Neon", {
        "BaseColor": (0, 0.3629689, 0.4745098, 0.74509805), "EdgeColor": (2.2, 0.6, 0.06, 1), "EdgeWidth": 1.0,
        "EdgeGlow": 0.08, "EdgeGlowStrength": 0.15, "LineColor": (0.35, 1.2, 3.2, 1), "LineWidth": 0.007,
        "LineGlow": 0.05, "LineGlowStrength": 0.12, "PulseAmount": 0.1, "WallTop": 0.5, "WallStrip": 0.03}, {}),
    # laser.shadergraph
    f"{M}/Laser/MI_LaserEnemy": (f"{M}/Laser/M_Laser", {
        "MainTexture": TX("Track/T_MulticolorIndustrialGlitcho"), "Mask": TX("Track/T_MASKandLaser"),
        "Color": (91.5012, 2.158047, 9.778024, 1), "MainSpeed": (-2.44, -0.49, 0, 0)}, {}),
    f"{M}/Laser/MI_NeonBarrier_Mat": (f"{M}/Laser/M_Laser", {
        "MainTexture": TX("Track/T_MulticolorIndustrialGlitcho_Jpg"), "Mask": TX("Track/T_ScanLines2"),
        "Color": (4.1929455, 16.144567, 21.112127, 0.94509804), "MainSpeed": (-2.42, 0, 0, 0)}, {}),
    # hologram.shadergraph
    f"{M}/Orbs/MI_Hologram": (f"{M}/Orbs/M_Hologram", {
        "ScanLinesTexture": TX("Track/T_ScanLines"), "FlickerTexture": TX("Track/T_Noise"), "ColorPower": 0.35,
        "VertexOffset": 0.13, "FresnelPower": 34.16, "ScanLinesSpeed": (0.22, 0.72, 0, 0),
        "ScanLinesTiling": (43.25, 5.02, 0, 0), "FresnelColor": (0, 46.02223, 244.63129, 0)}, {}),
    # URP Lit
    f"{M}/Track/MI_BoostPad_Mat": (f"{M}/Lit/M_Lit", {"BaseColor": (0, 1, 0, 0), "Metallic": 0.67, "Roughness": 1.0}, {}),
    f"{M}/Track/MI_RoadTexture_v2": (f"{M}/Lit/M_Lit", {
        "BaseMap": TX("Track/T_RoadTexture_v2"), "NormalMap": TX("Track/T_RoadTexture_normal"),
        "EmissionMap": TX("Track/T_RoadTexture_emission"), "EmissionColor": (1, 1, 1, 1),
        "Roughness": 0.583, "Metallic": 0.0}, {}),
    f"{M}/Vehicles/MI_PatrolBody_Mat": (f"{M}/Lit/M_Lit", {"BaseColor": (0.08, 0.09, 0.14, 1)}, {}),
    f"{M}/Vehicles/MI_PatrolTrim_Mat": (f"{M}/Lit/M_Lit", {}, {}),
    f"{M}/Vehicles/MI_PatrolRedLight_Mat": (f"{M}/Lit/M_Lit", {
        "BaseColor": (1, 0.1, 0.1, 1), "EmissionColor": (3, 0.3, 0.3, 1)}, {}),
    f"{M}/Vehicles/MI_PatrolBlueLight_Mat": (f"{M}/Lit/M_Lit", {
        "BaseColor": (0.25, 0.45, 1, 1), "EmissionColor": (0.75, 1.3499999, 3, 1)}, {}),
    f"{M}/Ship/MI_FUSELAJE": (f"{M}/Lit/M_Lit", {"Roughness": 0.0, "Specular": 0.2, "BackFacesOnly": 1.0},
                               {"two_sided": True}),
    f"{M}/Ship/MI_FUSELAJE3": (f"{M}/Lit/M_Lit", {
        "BaseColor": (0.2981131, 0.2981131, 0.2981131, 1), "Metallic": 0.81, "Roughness": 0.41}, {}),
    f"{M}/Ship/MI_Fuselaje2": (f"{M}/Lit/M_LitTranslucent", {
        "BaseColor": (0.65017444, 0.6514046, 0.690566, 1), "Opacity": 0.03529412,
        "EmissionColor": (0, 0.6133694, 0.8490566, 1)}, {}),
    # RealToon
    f"{M}/Orbs/MI_Mat_Powerup": (f"{M}/Toon/M_Toon", _toon(
        (0, 1, 0.06134969, 1), MainTex=GLOSS_BAR, MaiColPo=2.02, MVCOL=1.0, Highlight=(1024, 0, 0, 1),
        HighlightPower=2.91), {"outline": 1.0}),
    f"{M}/Orbs/MI_Mat_GreenPowerUp": (f"{M}/Toon/M_Toon", _toon(
        (1, 0.2657566, 0, 1), MainTex=GLOSS_BAR_DOUBLE, MaiColPo=4.47, MVCOL=1.0), {"outline": 1.0}),
    f"{M}/Orbs/MI_Mat_PowerUp_Blue": (f"{M}/Toon/M_Toon", _toon(
        (0.25, 0.55, 1, 1), MainTex=GLOSS_BAR, MVCOL=1.0, **_selflit((0.25, 0.55, 1, 1))), {"outline": 1.0}),
    f"{M}/Orbs/MI_Mat_PowerUp_Green": (f"{M}/Toon/M_Toon", _toon(
        (0.1, 1, 0.3, 1), MainTex=GLOSS_BAR, MVCOL=1.0, **_selflit((0.1, 1, 0.3, 1))), {"outline": 1.0}),
    f"{M}/Orbs/MI_Mat_PowerUp_Purple": (f"{M}/Toon/M_Toon", _toon(
        (0.75, 0.3, 1, 1), MainTex=GLOSS_BAR, MVCOL=1.0, **_selflit((0.75, 0.3, 1, 1))), {"outline": 1.0}),
    # PF_LaserSystem: LaserA / LaserB (RealToon, all defaults, vertex colour on)
    f"{M}/Laser/MI_Mat_CharacterColor": (f"{M}/Toon/M_Toon", _toon((1, 1, 1, 1), MVCOL=1.0), {"outline": 1.0}),
    # The ship as the runner shows it: PF_Ship's VehicleLivery swaps these RealToon materials in for the
    # prefab's lit FUSELAJE placeholders, painted with the shipped profile (_livery).
    f"{M}/Ship/MI_ESCAPES": (f"{M}/Toon/M_Toon", _toon(
        _livery((0.4037736, 0.4037736, 0.4037736, 1), SHIP_HUES["ESCAPES"]), MVCOL=1.0), {"outline": 0.5}),
    f"{M}/Ship/MI_PiezasUNOmat": (f"{M}/Toon/M_Toon", _toon(
        _livery((1, 0, 0, 1), SHIP_HUES["PiezasUNOmat"]), MVCOL=1.0), {"outline": 0.5}),
    f"{M}/Ship/MI_PRINCIPALmat": (f"{M}/Toon/M_Toon", _toon(
        _livery((1, 0, 0.092062354, 1), SHIP_HUES["PRINCIPALmat"]), MVCOL=1.0), {"outline": 0.5}),
    f"{M}/Ship/MI_PROPULSORmat": (f"{M}/Toon/M_Toon", _toon(
        _livery((1, 0.028301716, 0.071471736, 1), SHIP_HUES["PROPULSORmat"]), MVCOL=1.0), {"outline": 0.5}),
    f"{M}/Ship/MI_PUNTAmat": (f"{M}/Toon/M_Toon", _toon(
        _livery((1, 0, 0, 1), SHIP_HUES["PUNTAmat"]), MVCOL=1.0), {"outline": 0.5}),
    f"{M}/Ship/MI_Vidrio": (f"{M}/Toon/M_Toon", _toon(
        _livery((0, 0, 0, 1), SHIP_HUES["vidrio"]), MVCOL=1.0), {"outline": 0.5}),
    f"{M}/Vehicles/MI_CP_Air_Traffic": (f"{M}/Toon/M_Toon", _toon(
        (0.58679235, 0.70698786, 1, 1), MainTex=TX("Vehicles/T_CP_Air_Traffic_A"), MaiColPo=7.6,
        SelfLitIntensity=1.0, SelfLitColor=(1, 0, 0, 1), SelfLitMask=TX("Vehicles/T_CP_Air_Traffic_E"),
        TEXMCOLINT=-0.67), {"outline": 0.5}),
    f"{M}/Vehicles/MI_CP_Minivan": (f"{M}/Toon/M_Toon", _toon((1, 1, 1, 1), MainTex=TX("Vehicles/T_CP_Minivan_A")),
                                    {"outline": 2.9}),
    f"{M}/Vehicles/MI_CP_Quadron": (f"{M}/Toon/M_Toon", _toon((1, 1, 1, 1), MainTex=TX("Vehicles/T_CP_Quadron_A")),
                                    {"outline": 1.73}),
    f"{M}/Vehicles/MI_CP_Taxi": (f"{M}/Toon/M_Toon", _toon((1, 1, 1, 1), MainTex=TX("Vehicles/T_CP_Taxi_A")),
                                 {"outline": 0.5}),
}


def outline_instance_path(width):
    return f"{M}/Toon/MI_ToonOutline_{int(round(width * 100)):03d}"


def build_instances(report, overwrite):
    widths = sorted({e.get("outline") for _, _, e in INSTANCES.values() if e.get("outline")})
    todo = dict(INSTANCES)
    for w in widths:
        todo[outline_instance_path(w)] = (f"{M}/Toon/M_ToonOutline", {"OutlineWidth": float(w)}, {})

    for path, (parent, values, extra) in todo.items():
        if asset_exists(path) and not overwrite:
            report.add("SKIPPED", path, "exists")
            continue
        folder, name = split_object_path(path)
        mi = load(path) if asset_exists(path) else TOOLS.create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, load(parent))
        MEL.clear_all_material_instance_parameters(mi)
        for k, v in values.items():
            if isinstance(v, str):
                tex = load(v)
                if tex is None:
                    report.add("FAILED", path, f"texture {v} missing")
                    continue
                MEL.set_material_instance_texture_parameter_value(mi, k, tex)
            elif isinstance(v, tuple):
                MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*param_value(k, v)))
            else:
                MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
        if extra.get("two_sided"):
            ov = mi.get_editor_property("base_property_overrides")
            ov.set_editor_property("override_two_sided", True)
            ov.set_editor_property("two_sided", True)
            mi.set_editor_property("base_property_overrides", ov)
        MEL.update_material_instance(mi)
        save(path)
        report.add("BUILT", path, f"parent {parent.rsplit('/', 1)[1]}, {len(values)} values")


def run(report, overwrite=False):
    """Masters are always rebuilt (they are generated); instances follow the skip rule."""
    build_mpc(report)
    build_reveal_function(report)
    for build in (build_neon, build_road, build_hologram, build_laser, build_toon, build_lit):
        try:
            build(report)
        except Exception as e:
            report.add("FAILED", build.__name__, repr(e))
    build_instances(report, overwrite)
    log("materials done")
