pragma Singleton
import QtQuick

// UI state shared between panels (not part of the film): which Properties tab is open, where the Asset Browser
// is, the animation layer / blend / speed used when adding clips, the selected control in the Animation Set Editor.
QtObject {
    property string propTab: "tool"
    property string assetTab: "characters"
    property var drill: null              // { bank, path, label } open animation set in the Asset Browser
    property int layer: 0
    property real blend: 10
    property real speed: 1
    property string control: ""           // selected control in the Animation Set Editor: "root" | "lookat" | "joint" | "lens" | ...
    property var expanded: ({})           // animation set rows the user opened
    property int shotSel: -1
    signal focusAssets(string tab)
}
