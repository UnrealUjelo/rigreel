import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// Console: the runtime's log, and a Lua prompt that runs inside the game (the `eval` command; the result comes
// back through data.eval). For power users and bug reports.
Rectangle {
    id: root
    signal closeRequested()
    color: "#141416"
    Store { id: s }
    property var logLines: []
    property var repl: []                 // your Lua lines and their results, kept below the log
    readonly property var lines: logLines.concat(repl)
    property var history: []
    property int hpos: -1
    property var pending: ({})

    function refresh() {
        const log = studio.runtimeLog(300)
        if (log.length === logLines.length && log[log.length - 1] === logLines[logLines.length - 1]) return
        logLines = log
        Qt.callLater(() => view.positionViewAtEnd())
    }
    Timer { interval: 1500; running: root.visible; repeat: true; triggeredOnStart: true; onTriggered: root.refresh() }
    Connections {
        target: studio
        function onDataChanged() {
            // the runtime keeps the last result: { id = cid, ok, result | error }
            const ev = s.dt ? s.dt.eval : null
            if (!ev || !root.pending[String(ev.id)]) return
            const r = ev.ok ? ev.result : "error: " + ev.error
            root.repl = root.repl.concat(["= " + (typeof r === "object" ? JSON.stringify(r) : String(r))])
            const p = Object.assign({}, root.pending); delete p[String(ev.id)]; root.pending = p
            Qt.callLater(() => view.positionViewAtEnd())
        }
    }
    function run(code) {
        if (!code.trim()) return
        history = history.concat([code]); hpos = -1
        repl = repl.concat(["> " + code])
        Qt.callLater(() => view.positionViewAtEnd())
        const cid = studio.send("eval", { code: code })
        const p = Object.assign({}, pending); p[String(cid)] = true; pending = p
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.lines
            ScrollBar.vertical: ScrollBar {}
            delegate: Text {
                required property string modelData
                width: ListView.view.width
                leftPadding: 8
                text: modelData
                wrapMode: Text.WrapAnywhere
                font.family: Theme.mono; font.pixelSize: 11
                color: modelData.indexOf("[E]") >= 0 || modelData.indexOf("ERROR") >= 0 || modelData.indexOf("error") >= 0 ? "#ff8a8a"
                     : modelData.indexOf("[W]") >= 0 || modelData.indexOf("WARN") >= 0 ? "#e6c07b" : modelData.indexOf("> ") === 0 ? "#9ecbff" : modelData.indexOf("= ") === 0 ? "#b5d98a" : "#b8b8c0"
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#232323"
            Rectangle { width: parent.width; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent; anchors.margins: 3
                Text { text: "Lua"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 11; leftPadding: 4 }
                TextField {
                    id: input
                    Layout.fillWidth: true
                    color: Theme.textBright; font.family: Theme.mono; font.pixelSize: 11
                    placeholderText: "runs in the game, e.g.  return #require('Director.actor').all()"
                    placeholderTextColor: Theme.faint
                    selectByMouse: true
                    background: Rectangle { color: Theme.field; radius: 3; border.color: input.activeFocus ? Theme.accent : Theme.border }
                    onAccepted: { root.run(text); text = "" }
                    Keys.onUpPressed: { if (root.history.length) { root.hpos = root.hpos < 0 ? root.history.length - 1 : Math.max(0, root.hpos - 1); text = root.history[root.hpos] } }
                    Keys.onDownPressed: { if (root.hpos >= 0) { root.hpos = Math.min(root.history.length - 1, root.hpos + 1); text = root.history[root.hpos] } }
                }
                IconButton { icon: "folder-open"; tooltip: "Open the runtime folder"; onClicked: studio.openFolder("bridge") }
            }
        }
    }
}
