import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Element Viewer (SFM): the raw data of any element - Tree | Data. Pick what to inspect (the selection follows
// by default); open objects and lists with ▸; double-click a value to edit it (where the runtime has an editor
// for that field). Copy puts the element's JSON on the clipboard.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg
    Store { id: s }

    property string what: "selection"
    property var open: ({ "$": true })
    property string filter: ""
    KeyedModel { id: rows }

    readonly property var target: {
        if (!s.live) return { path: "state", obj: null }
        switch (what) {
        case "selection":
            if (s.clipSel) { const tr = s.track(s.clipSel.track); const list = tr ? (tr.kind === "camera" ? tr.cuts : tr.clips) : []; return { path: "clip", obj: N.find(list, c => c.id === s.clipSel.id), track: tr } }
            if (s.light && Ui.control === "light") return { path: "light", obj: s.light }
            if (s.camera && Ui.control.startsWith("cam:")) return { path: "camera", obj: s.camera }
            if (s.actor) return { path: "actor", obj: s.actor }
            if (s.camera) return { path: "camera", obj: s.camera }
            return { path: "sequence", obj: s.seq }
        case "sequence": return { path: "sequence", obj: s.seq }
        case "shots": return { path: "shots", obj: s.seq.shots }
        case "cameras": return { path: "cameras", obj: s.cameras }
        case "lights": return { path: "lights", obj: s.lights }
        case "actors": return { path: "actors", obj: s.actors }
        case "tracks": return { path: "tracks", obj: s.seq.tracks }
        case "state": return { path: "state", obj: s.st }
        case "data": return { path: "data", obj: s.dt ? Object.assign({}, s.dt, { cast: "(" + (s.dt.cast || []).length + " cast entries)" }) : null }
        }
        return { path: what, obj: null }
    }

    // fields the runtime can change, per element kind: path "a.b" -> function(value)
    function editor(kind, key, obj) {
        const num = (v) => parseFloat(v)
        if (kind === "camera") {
            if (["name"].indexOf(key) >= 0) return (v) => s.cmd("cam_update", { i: obj.i, fields: { name: v } })
            if (["fov", "roll", "radius", "height", "speed", "smooth"].indexOf(key) >= 0) return (v) => { const f = {}; f[key] = num(v); s.cmd("cam_update", { i: obj.i, fields: f }) }
        }
        if (kind === "light") {
            if (key === "name") return (v) => s.cmd("light_update", { id: obj.id, fields: { name: v } })
            if (["intensity", "radius", "cone", "spread", "temperature", "bounce", "volumetric", "specular", "shadow_bias", "shadow_soft"].indexOf(key) >= 0)
                return (v) => { const f = {}; f[key] = num(v); s.cmd("light_update", { id: obj.id, fields: f }) }
            if (key === "enabled" || key === "shadows" || key === "blackbody") return (v) => { const f = {}; f[key] = v === "true" || v === true; s.cmd("light_update", { id: obj.id, fields: f }) }
        }
        if (kind === "clip" && target.track) {
            const tr = target.track
            if (tr.kind === "camera") { if (key === "start") return (v) => s.cmd("update_cut", { track: tr.idx, id: obj.id, start: Math.round(num(v)) }) }
            else if (["start", "dur", "offset", "speed", "blend", "weight", "fade_in", "fade_out"].indexOf(key) >= 0)
                return (v) => { const f = {}; f[key] = num(v); s.cmd("update_clip", { track: tr.idx, id: obj.id, fields: f }) }
        }
        if (kind === "sequence") {
            if (key === "length") return (v) => s.cmd("seq_set", { length: Math.round(num(v)) })
            if (key === "name") return (v) => s.cmd("seq_set", { name: v })
            if (key === "t") return (v) => s.send("seq_seek", { t: num(v) })
        }
        if (kind === "shot") {
            if (key === "name") return (v) => s.cmd("shot_update", { id: obj.id, fields: { name: v } })
            if (key === "a" || key === "b") return (v) => { const f = {}; f[key] = Math.round(num(v)); s.cmd("shot_update", { id: obj.id, fields: f }) }
        }
        return null
    }

    function build() {
        const out = []
        const f = filter.trim().toLowerCase()
        const kindOf = (path, parentKind) => path === "shots" ? "shotlist" : parentKind === "shotlist" ? "shot" : parentKind
        const walk = (key, val, depth, path, kind, owner) => {
            const isObj = val !== null && typeof val === "object"
            const isArr = Array.isArray(val)
            const k = path
            const openNow = !!open[k]
            let shown = String(val)
            if (isArr) shown = "[" + val.length + "]"
            else if (isObj) shown = "{" + Object.keys(val).length + "}"
            else if (typeof val === "number") shown = Number.isInteger(val) ? String(val) : val.toFixed(4)
            const ed = !isObj && owner ? editor(kind, key, owner) : null
            if (!f || String(key).toLowerCase().indexOf(f) >= 0 || (!isObj && shown.toLowerCase().indexOf(f) >= 0) || isObj)
                out.push({ key: k, name: String(key), value: shown, depth: depth, expandable: isObj, open: openNow, editable: !!ed, type: isArr ? "list" : isObj ? "element" : typeof val })
            if (isObj && (openNow || !!f)) {
                const childKind = kindOf(key, kind)
                const keys = Object.keys(val)
                for (const ck of keys) walk(ck, val[ck], depth + 1, k + "." + ck, childKind, (childKind === "shot" && !isArr) ? val : isArr ? null : val)
            }
        }
        const t = target
        if (t.obj === null || t.obj === undefined) { rows.setItems([{ key: "$", name: "(nothing)", value: "", depth: 0 }]); return }
        const rootKind = t.path === "clip" ? "clip" : t.path === "camera" ? "camera" : t.path === "light" ? "light" : t.path === "sequence" ? "sequence" : t.path === "shots" ? "shotlist" : t.path
        walk(t.path, t.obj, 0, "$", rootKind, Array.isArray(t.obj) ? null : t.obj)
        rows.setItems(out)
    }
    Timer { id: rebuild; interval: 60; onTriggered: root.build() }
    Connections { target: studio; function onStateChanged() { rebuild.start() } }
    onWhatChanged: { open = { "$": true }; rebuild.start() }
    onOpenChanged: rebuild.start()
    onFilterChanged: rebuild.start()
    Component.onCompleted: build()

    // an edit on a row: find the owning element of that path again and call its editor
    function commit(row, text) {
        const parts = row.key.split(".").slice(1)
        let obj = target.obj, owner = Array.isArray(obj) ? null : obj, kind = target.path === "shots" ? "shotlist" : target.path
        for (let i = 0; i < parts.length - 1; ++i) {
            if (kind === "shotlist") kind = "shot"
            obj = obj[parts[i]]
            if (!Array.isArray(obj)) owner = obj
        }
        const ed = editor(kind === "shotlist" ? "shot" : kind, parts[parts.length - 1], owner)
        if (ed) ed(text)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.bg2
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent; anchors.margins: 4
                spacing: 4
                Choice {
                    implicitWidth: 130
                    value: root.what
                    options: [{ v: "selection", label: "Selection" }, { v: "sequence", label: "Film (sequence)" }, { v: "shots", label: "Shots" }, { v: "tracks", label: "Tracks" },
                              { v: "cameras", label: "Cameras" }, { v: "lights", label: "Lights" }, { v: "actors", label: "Animation sets" }, { v: "state", label: "Whole session" }, { v: "data", label: "Runtime data" }]
                    onChosen: (v) => root.what = v
                }
                SearchField { Layout.fillWidth: true; placeholderText: "Find a field…"; onTextChanged: root.filter = text }
                IconButton { icon: "copy"; tooltip: "Copy this element as JSON"; onClicked: { studio.copyText(JSON.stringify(root.target.obj, null, 2)); studio.setStatus("Element copied as JSON") } }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: "#2b2b2b"
            Row {
                anchors.fill: parent
                Text { width: parent.width * 0.5; leftPadding: 8; anchors.verticalCenter: parent.verticalCenter; text: "Tree"; color: Theme.muted; font.pixelSize: 10; font.bold: true }
                Rectangle { width: 1; height: parent.height; color: Theme.line }
                Text { leftPadding: 8; anchors.verticalCenter: parent.verticalCenter; text: "Data"; color: Theme.muted; font.pixelSize: 10; font.bold: true }
            }
        }
        ListView {
            id: lv
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: rows
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: er
                required property var item
                required property int index
                width: ListView.view.width
                height: 20
                color: index % 2 ? "#2e2e2e" : "#2b2b2b"
                Row {
                    anchors.fill: parent
                    Item {
                        width: parent.width * 0.5
                        height: parent.height
                        Row {
                            x: 6 + er.item.depth * 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Icon { visible: !!er.item.expandable; name: er.item.open ? "chevron-down" : "chevron-right"; size: 10; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                            Item { visible: !er.item.expandable; width: 10; height: 1 }
                            Text {
                                text: er.item.name
                                color: er.item.type === "element" ? "#9ecbff" : er.item.type === "list" ? "#c9a8ff" : Theme.text
                                font.pixelSize: Theme.smallSize; font.family: Theme.mono
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: if (er.item.expandable) { const o = Object.assign({}, root.open); o[er.item.key] = !er.item.open; root.open = o }
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: Theme.line }
                    Item {
                        width: parent.width * 0.5 - 1
                        height: parent.height
                        Text {
                            visible: !edit.visible
                            x: 8; width: parent.width - 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: er.item.value
                            color: er.item.editable ? Theme.amber : er.item.type === "string" ? "#b5d98a" : er.item.type === "number" ? "#e6c07b" : er.item.type === "boolean" ? "#7fc8ff" : Theme.muted
                            font.pixelSize: Theme.smallSize; font.family: Theme.mono
                            elide: Text.ElideRight
                        }
                        TextInput {
                            id: edit
                            visible: false
                            x: 8; width: parent.width - 12
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.textBright; font.pixelSize: Theme.smallSize; font.family: Theme.mono
                            selectByMouse: true
                            onAccepted: { root.commit(er.item, text); visible = false }
                            onActiveFocusChanged: if (!activeFocus) visible = false
                            Keys.onEscapePressed: visible = false
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !edit.visible
                            onDoubleClicked: if (er.item.editable) { edit.text = er.item.value; edit.visible = true; edit.forceActiveFocus(); edit.selectAll() }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.margins: 6
            wrapMode: Text.WordWrap
            text: "Amber values can be edited: double-click, type, Enter. Everything else is read-only."
            color: Theme.faint; font.pixelSize: 10
        }
    }
}
