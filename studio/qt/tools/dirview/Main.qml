import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    color: "#202124"
    title: "Director viewer"
    property alias stage: stage
    property alias camera: cam
    property string info: ""
    property vector3d target: Qt.vector3d(0, 1, 0)
    property real distance: 3.2
    property real yaw: 20
    property real pitch: -8

    View3D {
        id: view
        objectName: "view"
        anchors.fill: parent
        environment: ExtendedSceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#34373b"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            tonemapMode: SceneEnvironment.TonemapModeFilmic
            aoEnabled: true
            aoStrength: 50
            aoDistance: 0.3
            aoSoftness: 40
            exposure: 1.0
        }
        PerspectiveCamera {
            id: cam
            clipNear: 0.02
            clipFar: 500
            fieldOfView: 40
            position: {
                const r = Math.PI / 180
                return Qt.vector3d(win.target.x + win.distance * Math.cos(win.pitch * r) * Math.sin(win.yaw * r),
                                   win.target.y - win.distance * Math.sin(win.pitch * r),
                                   win.target.z + win.distance * Math.cos(win.pitch * r) * Math.cos(win.yaw * r))
            }
            eulerRotation: Qt.vector3d(win.pitch, win.yaw, 0)
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-40, 35, 0)
            brightness: 2.2
            castsShadow: true
            shadowMapQuality: Light.ShadowMapQualityVeryHigh
            shadowFactor: 70
            pcfFactor: 2
            csmNumSplits: 2
            shadowMapFar: 20
        }
        DirectionalLight { eulerRotation: Qt.vector3d(-15, -140, 0); brightness: 0.6; color: "#b8c8ff" }
        DirectionalLight { eulerRotation: Qt.vector3d(-60, 180, 0); brightness: 0.35 }
        Model {
            source: "#Rectangle"
            eulerRotation.x: -90
            scale: Qt.vector3d(0.2, 0.2, 1)
            materials: PrincipledMaterial { baseColor: "#5a5d61"; roughness: 0.9 }
        }
        Node { id: stage; objectName: "stage" }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property point last
        onPressed: (m) => last = Qt.point(m.x, m.y)
        onPositionChanged: (m) => {
            win.yaw -= (m.x - last.x) * 0.3
            win.pitch = Math.max(-85, Math.min(85, win.pitch - (m.y - last.y) * 0.3))
            last = Qt.point(m.x, m.y)
        }
        onWheel: (w) => win.distance = Math.max(0.3, win.distance * (w.angleDelta.y > 0 ? 0.9 : 1.1))
    }
    Text { x: 12; y: 10; color: "#e8e8e8"; text: win.info; font.pixelSize: 14 }
}
