import QtQuick
import QtQuick3D

// diagnostic: the same providers through Qt's built-in material
PrincipledMaterial {
    id: m
    property QtObject baseProvider: null
    property QtObject normalProvider: null
    property QtObject packedProvider: null
    property QtObject emissiveProvider: null
    property url baseSource
    property url normalSource
    property url packedSource
    property url emissiveSource
    property bool twoSided: false
    property bool glass: false
    property real hasBase: 1
    property real hasNormal: 1
    property real hasPacked: 0
    property real hasEmissive: 0
    property real baseAlphaMode: 1
    property real normalRough: 1
    property vector4d tint: Qt.vector4d(1, 1, 1, 1)
    property real roughnessValue: 0.6
    property real metalValue: 0
    property real alphaCutoff: 0.5
    property real alphaTest: 0
    property vector3d emissiveColor: Qt.vector3d(0, 0, 0)
    property real emissiveIntensity: 0
    property real highlight: 0
    baseColorMap: Texture { textureProvider: m.baseProvider }
    Component.onCompleted: console.log("provider", baseProvider, "->", baseColorMap.textureProvider)
    roughness: 0.6
}
