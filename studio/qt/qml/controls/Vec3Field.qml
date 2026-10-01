import QtQuick
import QtQuick.Layouts
import Director

// X / Y / Z number boxes (red / green / blue tags). `edited(v)` gives the whole [x, y, z].
Field {
    id: root
    property var value: [0, 0, 0]
    property real step: 0.01
    property string suffix: ""
    signal edited(var v)
    function withAxis(i, x) { const n = [value[0], value[1], value[2]]; n[i] = x; return n }
    NumField { Layout.fillWidth: true; axis: "X"; axisColor: "#e05a5a"; value: root.value ? root.value[0] : 0; step: root.step; suffix: root.suffix; onEdited: (v) => root.edited(root.withAxis(0, v)) }
    NumField { Layout.fillWidth: true; axis: "Y"; axisColor: "#6cc24a"; value: root.value ? root.value[1] : 0; step: root.step; suffix: root.suffix; onEdited: (v) => root.edited(root.withAxis(1, v)) }
    NumField { Layout.fillWidth: true; axis: "Z"; axisColor: "#4f8fe0"; value: root.value ? root.value[2] : 0; step: root.step; suffix: root.suffix; onEdited: (v) => root.edited(root.withAxis(2, v)) }
}
