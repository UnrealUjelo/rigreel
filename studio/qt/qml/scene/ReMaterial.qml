import QtQuick
import QtQuick3D

// RE Engine surface: base colour with metalness packed in alpha (ALBD/ALBM), normal with roughness in alpha
// (NRMR), and an ATOC map (alpha, translucency, occlusion, cavity). Lighting, shadows and IBL come from Qt.
CustomMaterial {
    id: m
    // game textures arrive as GPU texture providers (every mip, native block format); the sources are fallbacks
    property QtObject baseProvider: null
    property QtObject normalProvider: null
    property QtObject packedProvider: null
    property QtObject emissiveProvider: null
    property url baseSource
    property url normalSource
    property url packedSource
    property url emissiveSource
    property QtObject detailProvider: null
    property QtObject maskProvider: null
    property url detailSource
    property url maskSource
    property bool twoSided: false
    property real glass: 0
    // hair strands: soft alpha over a depth pre-pass of the solid parts (RE Engine dithers hair and resolves it with TAA)
    property real hair: 0

    property TextureInput baseMap: TextureInput { texture: Texture { textureProvider: m.baseProvider; source: m.baseProvider ? "" : m.baseSource; mipFilter: Texture.Linear; generateMipmaps: false } }
    property TextureInput normalMap: TextureInput { texture: Texture { textureProvider: m.normalProvider; source: m.normalProvider ? "" : m.normalSource; mipFilter: Texture.Linear; generateMipmaps: false } }
    property TextureInput packedMap: TextureInput { texture: Texture { textureProvider: m.packedProvider; source: m.packedProvider ? "" : m.packedSource; mipFilter: Texture.Linear; generateMipmaps: false } }
    property TextureInput emissiveMap: TextureInput { texture: Texture { textureProvider: m.emissiveProvider; source: m.emissiveProvider ? "" : m.emissiveSource; mipFilter: Texture.Linear; generateMipmaps: false } }

    property TextureInput detailMap: TextureInput { texture: Texture { textureProvider: m.detailProvider; source: m.detailProvider ? "" : m.detailSource; mipFilter: Texture.Linear; generateMipmaps: false } }
    property TextureInput maskMap: TextureInput { texture: Texture { textureProvider: m.maskProvider; source: m.maskProvider ? "" : m.maskSource; mipFilter: Texture.Linear; generateMipmaps: false } }
    property real hasDetail: 0
    property real hasMask: 0
    property real shading: 0              // 0 plain, 1 layered, 2 terrain
    property vector4d detailTint: Qt.vector4d(1, 1, 1, 1)
    property vector4d detailUv: Qt.vector4d(1, 1, 0, 0)
    property vector4d dirtColor: Qt.vector4d(0, 0, 0, 0)  // rgb linear, w strength
    property real maskUvSet: 0
    property real atlasColumns: 1
    property real groundTile: 5
    property real hasBase: 1
    property real hasNormal: 1
    property real hasPacked: 0
    property real hasEmissive: 0
    property real baseAlphaMode: 1        // 0 none, 1 dielectric, 2 metal, 3 alpha
    property real normalRough: 1
    property real normalLayout: 0         // 0 rgb normal (+ alpha roughness), 1 NRRC (r roughness, g/a normal, b cavity)
    property vector4d baseUv: Qt.vector4d(1, 1, 0, 0)       // tiling (xy) and offset (zw)
    property vector4d normalUv: Qt.vector4d(1, 1, 0, 0)
    property vector4d tint: Qt.vector4d(1, 1, 1, 1)
    property real roughnessValue: 0.6
    property real metalValue: 0
    property real alphaCutoff: 0.5
    property real alphaTest: 0
    property vector3d emissiveColor: Qt.vector3d(0, 0, 0)
    property real emissiveIntensity: 0
    property real highlight: 0
    property real clay: 0                 // Solid shading: light grey, the shapes only (normal maps kept)
    property real debugView: 0            // 1 base rgb, 2 base alpha, 3 normal map, 4 packed map, 5 uv

    shadingMode: CustomMaterial.Shaded
    cullMode: twoSided ? Material.NoCulling : Material.BackFaceCulling
    sourceBlend: glass > 0.5 || hair > 0.5 ? CustomMaterial.SrcAlpha : CustomMaterial.NoBlend
    destinationBlend: glass > 0.5 || hair > 0.5 ? CustomMaterial.OneMinusSrcAlpha : CustomMaterial.NoBlend
    depthDrawMode: hair > 0.5 ? Material.OpaquePrePassDepthDraw : Material.OpaqueOnlyDepthDraw
    fragmentShader: "re_standard.frag"
}
