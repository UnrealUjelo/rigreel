import QtQuick
import QtQuick3D

// A darkening layer multiplied over what is behind it (RE's eyelid occlusion shells: white, occlusion in alpha).
CustomMaterial {
    id: m
    property QtObject baseProvider: null
    property url baseSource
    property TextureInput baseMap: TextureInput { texture: Texture { textureProvider: m.baseProvider; source: m.baseProvider ? "" : m.baseSource; mipFilter: Texture.Linear; generateMipmaps: false } }
    property real strength: 0.85
    property real highlight: 0          // the actor sets it on every material (selection); unused here
    property real clay: 0               // Solid shading: no darkening layers (the shader outputs white)

    shadingMode: CustomMaterial.Unshaded
    sourceBlend: CustomMaterial.DstColor
    destinationBlend: CustomMaterial.Zero
    depthDrawMode: Material.NeverDepthDraw
    cullMode: Material.BackFaceCulling
    vertexShader: "re_multiply.vert"
    fragmentShader: "re_multiply.frag"
}
