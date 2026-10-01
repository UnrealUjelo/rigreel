import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director
import "../Names.js" as N

// The Timeline: SFM's three editors over one time ruler.
//   Clip Editor (F2)   : shots on the Film track, camera cuts, clips, audio - editing the film
//   Motion Editor (F3) : keys only, plus a time selection with falloff: pose edits inside it blend in and out
//   Graph Editor (F4)  : curves of the keyed channels
// Tab toggles back to the last editor. Right-click anything for its menu.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg
    Store { id: s }

    readonly property string mode: studio.editor
    readonly property bool graph: mode === "graph"
    property int lastMode: 0

    // the Motion Editor is where edits spread over the time selection (SFM behaviour)
    onModeChanged: s.send("motion_edit", { value: mode === "motion" })
    Component.onCompleted: s.send("motion_edit", { value: mode === "motion" })

    function easeItems(current) {
        const opts = [["smooth", "Smooth (ease in and out)"], ["linear", "Linear"], ["hold", "Hold (step to the next key)"], ["in", "Ease in"], ["out", "Ease out"], ["cubic", "Strong ease"]]
        return opts.map(o => ({ text: o[1], value: "ease:" + o[0], checked: (current || "smooth") === o[0] }))
    }
    function cameraItems(prefix, current) {
        return s.cameras.map(c => ({ text: c.name, value: prefix + c.i, checked: c.i === current }))
    }
    function addTrackItems(addr) {
        const who = s.actorById(addr)
        const name = who ? N.actorLabel(who) : "the selected character"
        return [
            { header: name },
            { text: "Animation track · full body", value: "anim0" },
            { text: "Animation track · upper body (layer 1)", value: "anim1" },
            { text: "Pose keys", value: "pose" },
            { text: "Position keys", value: "xform" },
            { text: "Walk path — click points on the floor", value: "path" },
            { separator: true },
            { text: "Key position here   (K)", value: "keypos" },
            { text: "Browse animations for " + name + "…", value: "browse" },
        ]
    }
    function runAddTrack(r, addr) {
        if (!r) return
        if (r === "browse") { s.send("select_actor", { addr: addr }); Ui.assetTab = "animations"; Ui.focusAssets("animations"); return }
        s.send("select_actor", { addr: addr })
        if (r === "anim0") s.cmd("add_track", { addr: addr, kind: "anim", layer: 0 })
        else if (r === "anim1") s.cmd("add_track", { addr: addr, kind: "anim", layer: 1 })
        else if (r === "pose" || r === "xform") s.cmd("add_track", { addr: addr, kind: r })
        else if (r === "path") { s.cmd("path_new", { addr: addr, start: s.t }); studio.focusGame() }
        else if (r === "keypos") s.cmd("key_transform", { t: s.t })
    }

    function onMenu(kind, info) {
        let r
        if (kind === "key") {
            const items = info.items || []
            r = studio.popup([{ header: items.length + " key" + (items.length > 1 ? "s" : "") + " · interpolation to the next key" }].concat(easeItems(info.ease)).concat([
                { separator: true }, { text: "Go to key", value: "go" }, { text: "Copy   (Ctrl+C)", value: "copy" }, { text: "Delete", value: "del" }]))
            if (!r) return
            if (r.indexOf("ease:") === 0) s.cmd("set_ease", { items: items, ease: r.slice(5) })
            else if (r === "go") s.send("seq_seek", { t: items[0].t })
            else if (r === "copy") s.send("copy_keys", { items: items })
            else if (r === "del") { s.cmd("remove_keys", { items: items }); studio.clearKeySel() }
        } else if (kind === "clip") {
            const tr = s.track(info.track)
            const clip = tr ? N.find(tr.clips, c => c.id === info.id) : null
            if (!clip) return
            const anim = info.kind === "anim"
            r = studio.popup([
                { header: anim ? (clip.name ? N.motionLabel(clip.name).label : "Clip") : "Pose clip" },
                { text: "Split at playhead   (Ctrl+B)", value: "split", enabled: s.t > clip.start && s.t < clip.start + clip.dur },
                { text: "Trim start to playhead   ([)", value: "trimL" }, { text: "Trim end to playhead   (])", value: "trimR" },
                { text: "Duplicate   (Ctrl+D)", value: "dup" },
                { text: "Fit to the animation's length", value: "fit", enabled: anim && !!clip.endframe },
                { separator: true },
                { text: "Loop", value: "loop", checked: !!clip.loop },
                { text: "Speed", items: [0.25, 0.5, 1, 1.5, 2].map(v => ({ text: v + "×", value: "speed:" + v, checked: Math.abs((clip.speed || 1) - v) < 0.01 })) },
                { text: clip.rm ? "Re-bake travel (root motion) from here" : "Bake travel (root motion)", value: "bake", enabled: anim },
                { text: "Back to in place", value: "unbake", enabled: anim && !!clip.rm },
                { separator: true },
                { text: "Go to start", value: "go" },
                { text: "Delete", value: "del" },
            ])
            if (!r) return
            const ref = { track: info.track, id: info.id }
            if (r === "split") s.cmd("split_clip", Object.assign({ t: s.t }, ref))
            else if (r === "trimL" || r === "trimR") s.cmd("trim_clip", Object.assign({ t: s.t, side: r === "trimL" ? "left" : "right" }, ref))
            else if (r === "dup") s.cmd("duplicate_clip", ref)
            else if (r === "fit") s.cmd("update_clip", Object.assign({ fields: { dur: (clip.endframe || clip.dur) / Math.max(0.01, Math.abs(clip.speed || 1)) } }, ref))
            else if (r === "loop") s.cmd("update_clip", Object.assign({ fields: { loop: !clip.loop } }, ref))
            else if (r.indexOf("speed:") === 0) s.cmd("update_clip", Object.assign({ fields: { speed: parseFloat(r.slice(6)) } }, ref))
            else if (r === "bake") s.cmd("bake_root_motion", ref)
            else if (r === "unbake") s.cmd("bake_root_motion", Object.assign({ clear: true }, ref))
            else if (r === "go") s.send("seq_seek", { t: clip.start })
            else if (r === "del") { s.cmd("remove_clip", ref); s.send("select_clip", {}) }
        } else if (kind === "cut") {
            const tr = s.track(info.track)
            const cut = tr ? N.find(tr.cuts, c => c.id === info.id) : null
            if (!cut) return
            r = studio.popup([{ header: "Camera cut @ " + cut.start }, { text: "Camera", items: cameraItems("cam:", cut.cam) },
                              { text: "Go to cut", value: "go" }, { text: "Delete cut", value: "del" }])
            if (!r) return
            if (r.indexOf("cam:") === 0) s.cmd("update_cut", { track: info.track, id: info.id, cam: parseInt(r.slice(4)) })
            else if (r === "go") s.send("seq_seek", { t: cut.start })
            else if (r === "del") s.cmd("remove_cut", { track: info.track, id: info.id })
        } else if (kind === "shot") {
            const sh = N.find(s.seq.shots, x => x.id === info.id)
            if (!sh) return
            r = studio.popup([{ header: sh.name + " · " + sh.a + "–" + sh.b },
                              { text: "Enter shot (play only this, look through its camera)", value: "go" },
                              { text: "Rename…", value: "rename" },
                              { text: "Camera", items: cameraItems("cam:", sh.cam) },
                              { text: "Fit to the time selection", value: "fit", enabled: !!s.range },
                              { text: "Render this shot…", value: "render" },
                              { separator: true }, { text: "Delete shot", value: "del" }])
            if (!r) return
            if (r === "go") s.send("shot_go", { id: sh.id })
            else if (r === "rename") { const n = studio.prompt("Rename shot", "Shot name:", sh.name); if (n) s.cmd("shot_update", { id: sh.id, fields: { name: n } }) }
            else if (r.indexOf("cam:") === 0) s.cmd("shot_update", { id: sh.id, fields: { cam: parseInt(r.slice(4)) } })
            else if (r === "fit") s.cmd("shot_update", { id: sh.id, fields: { a: s.range[0], b: s.range[1] } })
            else if (r === "render") { s.send("shot_go", { id: sh.id }); studio.showRenderDialog() }
            else if (r === "del") s.cmd("shot_remove", { id: sh.id })
        } else if (kind === "film") {
            r = studio.popup([{ text: s.range ? "New shot from the time selection (" + s.range[0] + "–" + s.range[1] + ")" : "New shot from the whole sequence", value: "add" }])
            if (r === "add") s.cmd("shot_add", {})
        } else if (kind === "audio") {
            const a = s.seq.audio || {}
            r = studio.popup([{ header: a.name || "Audio" },
                              { text: "Volume", items: [1, 0.75, 0.5, 0.25].map(v => ({ text: Math.round(v * 100) + " %", value: "vol:" + v, checked: Math.abs((a.volume === undefined ? 1 : a.volume) - v) < 0.01 })) },
                              { text: "Start at the playhead", value: "here" }, { text: "Replace…", value: "replace" }, { separator: true }, { text: "Remove audio", value: "del" }])
            if (!r) return
            if (r.indexOf("vol:") === 0) s.cmd("seq_audio", { volume: parseFloat(r.slice(4)) })
            else if (r === "here") s.cmd("seq_audio", { offset: s.t })
            else if (r === "replace") addAudio()
            else if (r === "del") s.cmd("seq_audio", { clear: true })
        } else if (kind === "cameras") {
            const items = [{ text: "New camera from the view   (C)", value: "capture" }]
            if (s.cameras.length) items.push({ text: "Cut to camera here", items: cameraItems("cut:", -1) })
            items.push({ text: "Key the live camera here   (M)", value: "key" })
            r = studio.popup(items)
            if (!r) return
            if (r === "capture") s.cmd("cam_capture")
            else if (r === "key") s.cmd("key_camera", {})
            else if (r.indexOf("cut:") === 0) s.cmd("add_cut", { cam: parseInt(r.slice(4)), t: info.t !== undefined ? info.t : s.t })
        } else if (kind === "group") {
            if (!info.addr) return
            runAddTrack(studio.popup(addTrackItems(info.addr)), info.addr)
        } else if (kind === "track") {
            const tr = s.track(info.track)
            if (!tr) return
            const items = [{ header: tr.kind === "anim" ? "Animation · layer " + tr.layer : tr.kind }]
            if (tr.kind === "anim") items.push({ text: "Add a clip from the Asset Browser…", value: "browse" })
            if (tr.kind === "xform") items.push({ text: "Key position here   (K)", value: "keypos" })
            if (tr.kind === "pose") items.push({ text: "Key the pose here   (K)", value: "keypose" })
            if (tr.kind === "cammove") items.push({ text: "Key the camera here   (M)", value: "keycam" })
            items.push({ separator: true }, { text: "Remove track", value: "del" })
            r = studio.popup(items)
            if (r === "browse") { if (tr.actor) s.send("select_actor", { addr: tr.actor }); Ui.layer = tr.layer || 0; Ui.assetTab = "animations"; Ui.focusAssets("animations") }
            else if (r === "keypos") s.cmd("key_transform", { t: info.t })
            else if (r === "keypose") s.cmd("keyframe_pose", { t: info.t })
            else if (r === "keycam") s.cmd("key_camera", { i: tr.cam, t: info.t })
            else if (r === "del") s.cmd("remove_track", { idx: tr.idx })
        } else if (kind === "graph" || kind === "lane") {
            r = studio.popup([{ text: "Frame all   (F)", value: "frame" }, { text: "Go here", value: "go" },
                              { text: "Paste keys here", value: "paste", enabled: s.live && s.st.key_clipboard > 0 }])
            if (r === "frame") { tl.frameAll(); gv.frameValues() }
            else if (r === "go") s.send("seq_seek", { t: info.t })
            else if (r === "paste") s.cmd("paste_keys", { t: info.t })
        }
    }
    function addAudio() {
        const p = studio.pickFile("audio")
        if (p) s.cmd("seq_audio", { path: p, name: p.split(/[\\/]/).pop(), offset: s.t })
    }

    component MenuWord: Rectangle {
        property string text: ""
        signal clicked()
        implicitWidth: mwl.implicitWidth + 16
        implicitHeight: 22
        radius: Theme.radius
        color: mwm.containsMouse ? "#454545" : "transparent"
        Text { id: mwl; anchors.centerIn: parent; text: parent.text; color: Theme.text; font.pixelSize: Theme.fontSize }
        MouseArea { id: mwm; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }
    component Seg: Rectangle {
        property string icon: ""
        property string tooltip: ""
        property bool checked: false
        property bool first: false
        property bool last: false
        signal clicked()
        implicitWidth: 28
        implicitHeight: 22
        color: checked ? Theme.accent : sgm.containsMouse ? Theme.numHover : Theme.num
        radius: first || last ? Theme.radius : 0
        Rectangle { visible: !parent.first; width: parent.radius; height: parent.height; color: parent.color }
        Rectangle { visible: !parent.last; anchors.right: parent.right; width: parent.radius; height: parent.height; color: parent.color }
        Icon { anchors.centerIn: parent; name: parent.icon; size: 14; color: Theme.text }
        MouseArea { id: sgm; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
        ToolTip.visible: tooltip !== "" && sgm.containsMouse
        ToolTip.text: tooltip
        ToolTip.delay: 500
    }
    function seekKey(dir) {
        // keys of the selected character (camera moves always count), else every key and clip edge
        const times = []
        for (const tr of s.seq.tracks || []) {
            if (s.actor && tr.actor !== s.actor.id && tr.kind !== "cammove") continue
            for (const k of tr.keys || []) times.push(k.t)
            for (const c of tr.clips || []) { times.push(c.start); for (const k of c.keys || []) times.push(c.start + k) }
            for (const c of tr.cuts || []) times.push(c.start)
        }
        times.sort((a, b) => a - b)
        const now = s.seq.t
        let target = -1
        if (dir > 0) { for (const x of times) if (x > now + 0.5) { target = x; break } }
        else { for (let i = times.length - 1; i >= 0; --i) if (times[i] < now - 0.5) { target = times[i]; break } }
        if (target >= 0) s.send("seq_seek", { t: target })
    }
    function playbackMenu() {
        const r = studio.popup([
            { text: "Loop   (L)", value: "loop", checked: !!s.seq.loop },
            { header: "Speed (preview only)" },
            { text: "¼ speed", value: 0.25, checked: s.seq.speed === 0.25 }, { text: "Half speed", value: 0.5, checked: s.seq.speed === 0.5 },
            { text: "Normal", value: 1, checked: !s.seq.speed || s.seq.speed === 1 }, { text: "Double", value: 2, checked: s.seq.speed === 2 },
            { separator: true },
            { text: "Play the film (the shots in order)", value: "film", enabled: (s.seq.shots || []).length > 0 },
        ])
        if (r === "loop") s.send("seq_set", { loop: !s.seq.loop })
        else if (r === "film") s.send("film_play", {})
        else if (typeof r === "number") s.send("seq_speed", { value: r })
    }
    function keyingMenu() {
        const r = studio.popup([
            { text: "Key the Selection   (K)", value: "key" },
            { text: "Key the Camera   (M)", value: "cam" },
            { text: "Auto-Keying   (Shift+K)", value: "auto", checked: !!s.st.autokey },
            { separator: true },
            { text: "Copy Keys   (Ctrl+C)", value: "copy", enabled: studio.keyCount > 0 },
            { text: "Paste Keys   (Ctrl+V)", value: "paste" },
            { text: "Delete Keys   (Del)", value: "del", enabled: studio.keyCount > 0 },
        ])
        if (r === "key") s.cmd("key_selection", { t: s.t })
        else if (r === "cam") s.cmd("key_camera", {})
        else if (r === "auto") s.send("autokey", { value: !s.st.autokey })
        else if (r === "copy") s.send("copy_keys", { items: studio.keySel })
        else if (r === "paste") s.cmd("paste_keys", { t: s.t })
        else if (r === "del") { s.cmd("remove_keys", { items: studio.keySel }); studio.clearKeySel() }
    }
    function viewMenu() {
        const r = studio.popup([
            { text: "Frame All   (F)", value: "all" },
            { text: "Zoom In", value: "in" },
            { text: "Zoom Out", value: "out" },
        ])
        if (r === "all") { tl.frameAll(); if (root.graph) gv.frameValues() }
        else if (r === "in") (root.graph ? gv : tl).zoomBy(1.3)
        else if (r === "out") (root.graph ? gv : tl).zoomBy(1 / 1.3)
    }
    function addMenu() {
        const items = []
        if (s.actor) items.push({ header: s.actor.display_name || s.actor.name }), items.push(...root.addTrackItems(s.actor.id))
        items.push({ separator: true })
        items.push({ text: "Camera cuts track", value: "cut" })
        items.push({ text: "Shot from the time selection", value: "shot" })
        items.push({ text: "Audio guide track…", value: "audio" })
        const r = studio.popup(items)
        if (r === "cut") s.cmd("add_track", { kind: "camera" })
        else if (r === "shot") s.cmd("shot_add", {})
        else if (r === "audio") root.addAudio()
        else if (s.actor && r) root.runAddTrack(r, s.actor.id)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header, after Blender's timeline: the editor, its menus, auto-key and the transport in the middle,
        // the current frame and the frame range on the right
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.bg2
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6; anchors.rightMargin: 6
                spacing: 3
                // editor type
                Rectangle {
                    implicitWidth: edRow.implicitWidth + 16; implicitHeight: 22; radius: Theme.radius
                    color: edMa.containsMouse ? "#2f2f2f" : "#282828"; border.color: "#3d3d3d"
                    Row {
                        id: edRow
                        anchors.centerIn: parent
                        spacing: 6
                        Icon { name: root.mode === "graph" ? "chart-spline" : root.mode === "motion" ? "diamond" : "film"; size: 13; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: root.mode === "graph" ? "Graph Editor" : root.mode === "motion" ? "Dope Sheet" : "Timeline"; color: Theme.text; font.pixelSize: Theme.fontSize; anchors.verticalCenter: parent.verticalCenter }
                        Icon { name: "chevron-down"; size: 11; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea {
                        id: edMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: {
                            const r = studio.popup([
                                { header: "Editor" },
                                { text: "Timeline — shots, cuts, clips and sound   (F2)", value: "clip", checked: root.mode === "clip" },
                                { text: "Dope Sheet — keys, time selection with falloff   (F3)", value: "motion", checked: root.mode === "motion" },
                                { text: "Graph Editor — the curves of keyed values   (F4)", value: "graph", checked: root.mode === "graph" },
                            ])
                            if (r) studio.editor = r
                        }
                    }
                }
                Item { width: 4 }
                MenuWord { text: "Playback"; onClicked: root.playbackMenu() }
                MenuWord { text: "Keying"; onClicked: root.keyingMenu() }
                MenuWord { text: "View"; onClicked: root.viewMenu() }
                MenuWord { text: "Add"; onClicked: root.addMenu() }
                // the Dope Sheet's time selection with falloff
                RowLayout {
                    visible: root.mode === "motion"
                    spacing: 3
                    Item { width: 6 }
                    Chip { text: s.range ? "In " + s.range[0] : "In"; checked: !!s.range; tooltip: "Time selection start at the playhead (I)"; onClicked: s.send("seq_range", { a: s.t }) }
                    Chip { text: s.range ? "Out " + s.range[1] : "Out"; checked: !!s.range; tooltip: "Time selection end at the playhead (O)"; onClicked: s.send("seq_range", { b: s.t }) }
                    Text { text: "Falloff"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                    NumField {
                        implicitWidth: 44; step: 1; from: 0; to: 600
                        value: s.seq.falloff ? s.seq.falloff[0] : 0
                        tooltip: "Frames over which an edit blends in before the time selection (or Alt+drag the In handle)"
                        onEdited: (v) => s.send("seq_range", { fin: Math.round(v) })
                    }
                    NumField {
                        implicitWidth: 44; step: 1; from: 0; to: 600
                        value: s.seq.falloff ? s.seq.falloff[1] : 0
                        tooltip: "Frames over which an edit blends out after the time selection (or Alt+drag the Out handle)"
                        onEdited: (v) => s.send("seq_range", { fout: Math.round(v) })
                    }
                    Chip {
                        visible: studio.standalone
                        readonly property bool rel: !(s.live && s.st.relative_edits === false)
                        text: rel ? "Offset" : "Hold"
                        checked: true
                        tooltip: rel ? "K adds your change to the motion across the time selection (the bones keep moving). Click: hold the pose instead"
                                     : "K holds your pose across the time selection. Click: offset the motion instead"
                        onClicked: s.send("motion_edit", { relative: !rel })
                    }
                }
                IconButton { visible: root.graph; icon: "ratio"; text: gv.normalized ? "Normalized" : "Shared axis"; tooltip: "Each curve in its own range, or all on one value axis (Alt+wheel zooms it)"; onClicked: gv.normalized = !gv.normalized }
                Item { Layout.fillWidth: true }

                // auto-key and the transport
                Rectangle {
                    implicitWidth: 24; implicitHeight: 22; radius: 11
                    color: s.st.autokey ? "#c83a3a" : akMa.containsMouse ? Theme.numHover : Theme.num
                    Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: s.st.autokey ? "#ffffff" : "#d0d0d0" }
                    MouseArea { id: akMa; anchors.fill: parent; hoverEnabled: true; onClicked: s.send("autokey", { value: !s.st.autokey }) }
                    ToolTip.visible: akMa.containsMouse; ToolTip.delay: 500
                    ToolTip.text: "Auto-keying (Shift+K): edits at the playhead create keys by themselves"
                }
                IconButton { icon: "diamond"; size: 24; tooltip: "Key the selection here (K)"; onClicked: s.cmd("key_selection", { t: s.t }) }
                Item { width: 6 }
                Row {
                    spacing: 0
                    Seg { first: true; icon: "chevron-first"; tooltip: "Jump to the start (Home)"; onClicked: s.send("seq_seek", { t: s.range ? s.range[0] : 0 }) }
                    Seg { icon: "skip-back"; tooltip: "Previous key (↑)"; onClicked: root.seekKey(-1) }
                    Seg { icon: "step-back"; tooltip: "Previous frame (←, Shift: 10)"; onClicked: s.send("seq_seek", { t: Math.max(0, s.t - 1) }) }
                    Seg { icon: s.seq.playing ? "pause" : "play"; checked: !!s.seq.playing; tooltip: "Play / pause (Space)"; onClicked: s.send("seq_toggle") }
                    Seg { icon: "step-forward"; tooltip: "Next frame (→, Shift: 10)"; onClicked: s.send("seq_seek", { t: s.t + 1 }) }
                    Seg { icon: "skip-forward"; tooltip: "Next key (↓)"; onClicked: root.seekKey(1) }
                    Seg { last: true; icon: "chevron-last"; tooltip: "Jump to the end (End)"; onClicked: s.send("seq_seek", { t: s.range ? s.range[1] : s.seq.length }) }
                }
                Item { Layout.fillWidth: true }

                // current frame, start / end (the time selection, else the whole film)
                NumField {
                    implicitWidth: 64; step: 1; from: 0; to: 1e6; decimals: 0
                    value: s.t
                    tooltip: "Current frame (" + studio.timecode(s.seq.t || 0) + ")"
                    onEdited: (v) => s.send("seq_seek", { t: Math.round(v) })
                }
                Item { width: 4 }
                Text { text: "Start"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                NumField {
                    implicitWidth: 52; step: 1; from: 0; to: 1e6; decimals: 0
                    value: s.range ? s.range[0] : 0
                    tooltip: "The first frame played and rendered (a time selection, I)"
                    onEdited: (v) => s.send("seq_range", { a: Math.round(v), b: s.range ? s.range[1] : (s.seq.length || 600) })
                }
                Text { text: "End"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                NumField {
                    implicitWidth: 56; step: 1; from: 1; to: 1e6; decimals: 0
                    value: s.range ? s.range[1] : (s.seq.length || 600)
                    tooltip: s.range ? "The last frame played and rendered (the time selection, O)" : "The film's length in frames (" + ((s.seq.length || 600) / s.fps).toFixed(1) + " s)"
                    onEdited: (v) => { if (s.range) s.send("seq_range", { b: Math.round(v) }); else s.cmd("seq_set", { length: Math.round(v) }) }
                }
                IconButton { visible: !!s.range; icon: "x"; size: 22; iconSize: 12; tooltip: "Clear the time selection: play the whole film (Alt+O)"; onClicked: s.send("seq_range", { clear: true }) }
            }
        }
        Text {
            visible: text !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 8
            text: (root.graph ? gv.hoverText : tl.hoverText) || (studio.keyCount ? studio.keyCount + " key" + (studio.keyCount > 1 ? "s" : "") + " selected · drag to retime · Alt+drag to scale · Del" : "")
            color: Theme.muted; font.pixelSize: Theme.smallSize
            elide: Text.ElideRight
        }

        // ---- editors
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TimelineView {
                id: tl
                anchors.fill: parent
                anchors.rightMargin: 10
                visible: !root.graph
                core: studio
                mode: root.mode === "motion" ? "motion" : "clip"
                onMenuRequested: (kind, info, x, y) => root.onMenu(kind, info)
                onSelectedShotChanged: Ui.shotSel = selectedShot
            }
            GraphView {
                id: gv
                anchors.fill: parent
                anchors.rightMargin: 10
                visible: root.graph
                core: studio
                onMenuRequested: (kind, info, x, y) => root.onMenu(kind, info)
            }
            ScrollBar {
                id: vbar
                readonly property var view: root.graph ? gv : tl
                anchors.right: parent.right
                anchors.top: parent.top; anchors.topMargin: 30
                anchors.bottom: parent.bottom
                width: 10
                orientation: Qt.Vertical
                size: Math.min(1, (view.height - 30) / Math.max(1, view.contentHeight))
                position: view.vscroll / Math.max(1, view.contentHeight)
                active: true
                policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                onPositionChanged: if (pressed) view.vscroll = position * view.contentHeight
                contentItem: Rectangle { implicitWidth: 6; radius: 3; color: vbar.pressed ? "#6a6a74" : "#4a4a52" }
                background: Rectangle { color: "#18181b" }
            }
        }
    }
    Shortcut { sequence: "F"; context: Qt.WindowShortcut; onActivated: { tl.frameAll(); if (root.graph) gv.frameValues() } }
}
