pragma Singleton
import QtQuick

// One palette for every panel, after Blender's default theme (the widget side uses the same colours, see
// src/app/Theme.cpp).
QtObject {
    readonly property color bg: "#2b2b2b"          // region body (outliner, properties, asset browser)
    readonly property color bg2: "#303030"         // headers, toolbars
    readonly property color bg3: "#545454"         // buttons (Blender's regular widgets)
    readonly property color panel: "#353535"       // a properties panel's box
    readonly property color field: "#1d1d1d"       // text fields, dropdowns
    readonly property color num: "#545454"         // number fields and sliders
    readonly property color numHover: "#656565"
    readonly property color viewport: "#3f3f3f"    // the 3D view's background
    readonly property color line: "#161616"
    readonly property color border: "#3d3d3d"
    readonly property color text: "#e5e5e5"
    readonly property color textBright: "#ffffff"
    readonly property color muted: "#a5a5a5"
    readonly property color faint: "#6e6e6e"
    readonly property color accent: "#4772b3"      // selection, active tool, checked
    readonly property color accentHi: "#5b88cc"
    readonly property color rowSel: "#334d80"      // selected row (outliner)
    readonly property color rowHover: "#3a3a3a"
    readonly property color amber: "#e6a23a"       // keyed / live / current
    readonly property color danger: "#d55a5a"
    readonly property color ok: "#7fd18a"
    readonly property color anim: "#d08a3c"
    readonly property color pose: "#a278e0"
    readonly property color move: "#4fa8e0"
    readonly property color cam: "#e0c04a"
    readonly property color light: "#ffd48a"
    readonly property color audio: "#6cc24a"
    // axis colours of the navigation gizmo and manipulators (Blender)
    readonly property color axisX: "#f63652"
    readonly property color axisY: "#70a41c"
    readonly property color axisZ: "#2f84e3"
    readonly property int rowH: 22
    readonly property int pad: 8
    readonly property int radius: 4
    readonly property int fontSize: 12
    readonly property int smallSize: 11
    readonly property string mono: "Consolas"
}
