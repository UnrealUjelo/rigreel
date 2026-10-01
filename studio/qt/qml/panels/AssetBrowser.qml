import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Asset Browser: everything you can bring into the film. Hover anything to preview it in the game (characters
// stand in front of the camera, props turn on a turntable, animations play on the selected character, sounds play);
// click to add it.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg
    Store { id: s }

    readonly property string tab: Ui.assetTab
    property string q: ""
    Connections { target: Ui; function onFocusAssets(t) { studio.requestMenu("raise:assets") } }

    // ------------------------------------------------------------------ previews (debounced)
    Timer { id: previewTimer; interval: 300; property var run: null; onTriggered: if (run) run() }
    function preview(fn, delay) { previewTimer.interval = delay || 300; previewTimer.run = fn; previewTimer.restart() }
    function cancelPreview() { previewTimer.stop(); previewTimer.run = null }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // which game's characters, animations, props and maps are listed. A film mixes games freely: RE2's Leon
        // can stand on an RE4 map and play RE4 clips (they are mapped onto his skeleton)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            visible: studio.standalone && !!studio.games && studio.games.games.length > 1
            color: Theme.bg2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 6
                spacing: 6
                Icon { name: "gamepad-2"; size: 13; color: Theme.muted }
                Text { text: "From"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                Choice {
                    objectName: "assetGame"
                    Layout.fillWidth: true
                    tooltip: "The game whose assets are listed. Characters, clips and maps from different games can share a film."
                    value: studio.games ? studio.games.activeKey : ""
                    options: studio.games ? studio.games.games.map(g => ({ v: g.key, label: g.title })) : []
                    onChosen: (v) => { if (v !== studio.games.activeKey) studio.games.activate(v) }
                }
                Text {
                    visible: !!studio.games && (studio.games.opening || !studio.games.ready)
                    text: "opening…"
                    color: Theme.amber
                    font.pixelSize: Theme.smallSize
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        }
        // tabs
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: tabsFlow.implicitHeight + 6
            color: Theme.bg2
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            Flow {
                id: tabsFlow
                x: 4; y: 3
                width: parent.width - 8
                spacing: 1
                Repeater {
                    model: [["characters", "user", "Characters"], ["animations", "film", "Animations"], ["props", "box", "Props"], ["sounds", "volume-2", "Sounds"], ["stages", "map", "Stages"]]
                    IconButton {
                        required property var modelData
                        icon: modelData[1]; text: modelData[2]
                        checked: root.tab === modelData[0]
                        onClicked: { Ui.assetTab = modelData[0]; Ui.drill = null; search.text = "" }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            visible: !(root.tab === "animations" && Ui.drill)
            SearchField {
                id: search
                Layout.fillWidth: true
                placeholderText: root.tab === "characters" ? "Search characters…" : root.tab === "props" ? "Search props…" : root.tab === "sounds" ? "Search music, voices, effects…" : root.tab === "stages" ? "Search areas…" : "Search animations…"
                onTextChanged: root.q = text
            }
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.tab === "characters" ? charactersTab : root.tab === "animations" ? (Ui.drill ? clipsTab : animationsTab) : root.tab === "props" ? propsTab : root.tab === "sounds" ? soundsTab : stagesTab
        }
    }

    // ================================================================== characters
    Component {
        id: charactersTab
        RowLayout {
            id: ct
            spacing: 0
            property string shelf: "cast"
            property bool showDlc: false
            property bool damaged: false
            property bool idle: false
            property var openSet: ({})
            function shelfOf(c) {
                const n = (c.name + " " + (c.group || "")).toLowerCase()
                if (c.group === "Creatures & bosses" || /(el gigante|mendez|méndez|salazar|saddler|verdugo|u-3|del lago|regenerador|iron maiden|mutated)/.test(n)) return "boss"
                if (/(chainsaw|bella|garrador|brute|armadura|novistador)/.test(n)) return "mini"
                if (/(leon|ada|ashley|luis|wesker|merchant|krauser|hunk)/.test(n)) return "cast"
                return "extras"
            }
            readonly property var rows: {
                if (shelf === "scene") {
                    return (s.dt ? s.dt.scene : []).filter(c => !root.q || N.actorName(c.name, c.is_player).toLowerCase().indexOf(root.q.toLowerCase()) >= 0 || c.name.indexOf(root.q) >= 0)
                        .map(c => ({ kind: "scene", c: c }))
                }
                const nq = root.q.trim().toLowerCase()
                let chars = []
                for (const c of (s.dt ? s.dt.cast : [])) {
                    if (!c.skel || (!showDlc && c.dlc)) continue
                    const presets = c.presets.filter(p => (damaged || !p.variant) && (showDlc || !p.dlc))
                    if (c.name !== "Villagers") chars.push(Object.assign({}, c, { presets: presets }))
                    else {
                        const mini = presets.filter(p => /salvador|chainsaw|brute/i.test(p.name))
                        chars.push(Object.assign({}, c, { presets: presets.filter(p => mini.indexOf(p) < 0) }))
                        chars.push(Object.assign({}, c, { name: "Chainsaw villagers", group: "Mini bosses", presets: mini }))
                    }
                }
                chars = chars.filter(c => c.presets.length > 0 && shelfOf(c) === shelf)
                    .filter(c => !nq || c.name.toLowerCase().indexOf(nq) >= 0 || c.code.indexOf(nq) >= 0 || c.id.indexOf(nq) >= 0 || c.presets.some(p => p.name.toLowerCase().indexOf(nq) >= 0))
                const famOf = (c) => c.group || N.castFamily(c.code, c.dlc)
                const order = ["Main cast", "People", "Villagers", "Cultists", "Soldiers", "Enemies", "Creatures & bosses", "Animals"]
                const fams = Array.from(new Set(chars.map(famOf))).sort((a, b) => (order.indexOf(a) < 0 ? 99 : order.indexOf(a)) - (order.indexOf(b) < 0 ? 99 : order.indexOf(b)))
                const out = []
                for (const f of fams) {
                    out.push({ kind: "family", label: f })
                    for (const c of chars.filter(x => famOf(x) === f)) {
                        const key = c.tree + "/" + c.id + "/" + c.name
                        const open = !!openSet[key] || (!!nq && c.presets.some(p => p.name.toLowerCase().indexOf(nq) >= 0) && c.name.toLowerCase().indexOf(nq) < 0)
                        out.push({ kind: "char", c: c, key: key, open: open })
                        if (open && c.presets.length > 1) for (const p of c.presets) out.push({ kind: "look", c: c, p: p })
                    }
                }
                return out
            }
            function spawn(c, p) { root.cancelPreview(); s.send("cast_preview_clear"); s.cmd("spawn_cast", { id: c.id, tree: c.tree, preset: p.name, no_idle: !idle }) }
            function place(c, p) { root.cancelPreview(); s.send("cast_preview_clear"); s.cmd("cast_place", { id: c.id, tree: c.tree, preset: p.name, no_idle: !idle }); studio.focusGame() }
            function hover(c, p) { root.preview(() => s.send("cast_preview", { id: c.id, tree: c.tree, preset: p.name })) }
            function leave() { root.cancelPreview(); s.send("cast_preview_clear") }
            Component.onDestruction: s.send("cast_preview_clear")

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 88
                color: "#262626"
                Column {
                    width: parent.width
                    Repeater {
                        model: [["cast", "Cast"], ["mini", "Mini bosses"], ["boss", "Bosses"], ["extras", "Extras"], ["scene", "In the world"]]
                        RailButton { required property var modelData; text: modelData[1]; checked: ct.shelf === modelData[0]; onClicked: { ct.leave(); ct.shelf = modelData[0] } }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 0
                Flow {
                    visible: ct.shelf !== "scene"
                    Layout.fillWidth: true
                    Layout.margins: 6
                    spacing: 4
                    Chip { text: "Idle"; checked: ct.idle; tooltip: "Off: spawn in the rest pose, ready for posing. On: spawn playing an idle clip."; onClicked: ct.idle = !ct.idle }
                    Chip { text: "Damaged"; checked: ct.damaged; tooltip: "Also list the damaged / bloodied versions of a look"; onClicked: ct.damaged = !ct.damaged }
                    Chip { text: "DLC"; checked: ct.showDlc; tooltip: "Separate Ways / Mercenaries characters (need that content installed)"; onClicked: ct.showDlc = !ct.showDlc }
                }
                Text {
                    visible: ct.shelf === "scene"
                    Layout.fillWidth: true; Layout.margins: 8
                    wrapMode: Text.WordWrap
                    text: "Characters the game put here. Click one to direct it (it joins the Animation Set Editor)."
                    color: Theme.muted; font.pixelSize: Theme.smallSize
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: ct.rows
                    ScrollBar.vertical: ScrollBar {}
                    onMovementStarted: ct.leave()
                    delegate: Loader {
                        required property var modelData
                        width: ListView.view.width
                        sourceComponent: modelData.kind === "family" ? famRow : modelData.kind === "scene" ? sceneRow : charRow
                        property var d: modelData
                    }
                    Text { anchors.centerIn: parent; visible: parent.count === 0; text: s.dt ? "Nothing matches." : "Waiting for the game…"; color: Theme.faint; font.pixelSize: Theme.smallSize }
                }
            }
            Component {
                id: famRow
                Rectangle {
                    height: 22; color: "transparent"
                    Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: parent.parent.d.label; color: Theme.amber; font.pixelSize: 10; font.bold: true; font.capitalization: Font.AllUppercase }
                }
            }
            Component {
                id: sceneRow
                ListRow {
                    readonly property var c: parent.d.c
                    icon: "user"
                    title: N.actorName(c.name, c.is_player)
                    subtitle: (c.dist >= 0 ? c.dist.toFixed(1) + " m" : "") + (c.added ? " · directed" : "") + (c.is_player ? " · player" : "")
                    tooltip: c.name
                    selected: s.sel.actor === c.addr
                    onClicked: s.send("select_actor", { addr: c.addr })
                }
            }
            Component {
                id: charRow
                ListRow {
                    id: crow
                    readonly property var d: parent.d
                    readonly property var c: d.c
                    readonly property var p: d.kind === "look" ? d.p : c.presets[0]
                    indent: d.kind === "look" ? 26 : 0
                    icon: d.kind === "look" ? "" : "user-plus"
                    image: studio.standalone && p && p.model ? "image://thumb/" + encodeURIComponent(p.model) : ""
                    title: d.kind === "look" ? p.name : c.name + (c.dlc ? "  · DLC" : "")
                    subtitle: d.kind === "look" ? "" : (c.presets.length === 1 ? c.presets[0].name : c.presets.length + " looks · " + c.presets[0].name + " first") + (c.general ? "" : " · no idle")
                    selected: d.kind === "char" && d.open
                    tooltip: c.id + (studio.standalone ? "" : " · hover to preview in the game")
                    onEntered: ct.hover(c, p)
                    onExited: ct.leave()
                    onClicked: {
                        if (d.kind === "look") { ct.spawn(c, p); return }
                        const o = Object.assign({}, ct.openSet); o[d.key] = !d.open; ct.openSet = o
                    }
                    IconButton { icon: "map-pin"; size: 22; iconSize: 13; tooltip: "Place: click a spot in the picture to drop them there, facing the camera"; onClicked: ct.place(crow.c, crow.p) }
                    Btn { kind: "primary"; icon: "plus"; text: "Spawn"; onClicked: ct.spawn(crow.c, crow.p) }
                }
            }
        }
    }

    // ================================================================== animations (sets)
    Component {
        id: animationsTab
        RowLayout {
            id: at
            spacing: 0
            property string group: ""
            readonly property string actorOwner: s.actor ? (s.actor.cast ? s.actor.cast.code : (N.characterInfo(s.actor.name) || {}).code || "") : ""
            // is the selected character from the game listed here? (RE2's Leon while RE4 is listed: not)
            readonly property bool actorHere: !s.actor || !s.actor.game || !s.st.game || !s.st.game.listed || s.actor.game === s.st.game.listed
            // the character's own lists; one without any (Ada appears only in cutscenes) gets the same person's
            // other rig (Separate Ways); a character from another game gets the same person here (RE2's Leon:
            // RE4's Leon), else every list
            function defaultOwner() {
                const own = actorOwner, owners = s.dt ? s.dt.owners || [] : [], chars = s.dt ? s.dt.chars || {} : {}
                if (actorHere && (!own || owners.some(o => o.code === own))) return own
                const first = (n) => (n || "").split(" (")[0].split(" ")[0].toLowerCase()
                const person = first(actorHere ? chars[own] : (s.actor.cast ? s.actor.cast.name : s.actor.display_name).replace(/ \d+$/, ""))
                const twin = person ? owners.find(o => first(chars[o.code]) === person) : null
                return twin ? twin.code : ""
            }
            property string owner: defaultOwner()
            Timer { id: searchTimer; interval: 250; onTriggered: s.send("catalog_search", { q: root.q, group: at.group, owner: at.owner }) }
            Connections { target: root; function onQChanged() { searchTimer.restart() } }
            onGroupChanged: searchTimer.restart()
            onOwnerChanged: searchTimer.restart()
            Component.onCompleted: searchTimer.restart()
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 96
                color: "#262626"
                ListView {
                    anchors.fill: parent
                    clip: true
                    model: [""].concat((s.dt ? s.dt.groups : []).filter(g => g.indexOf("(") < 0))
                    delegate: RailButton { required property string modelData; text: modelData ? N.groupLabel(modelData) : "All"; checked: at.group === modelData; onClicked: at.group = modelData }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 6
                    Icon { name: "user"; size: 13; color: Theme.muted }
                    Choice {
                        Layout.fillWidth: true
                        value: at.owner
                        options: [{ v: "", label: "All characters" }].concat((s.dt ? s.dt.owners : []).map(o => {
                            const c = s.dt.cast ? N.find(s.dt.cast, x => x.code === o.code) : null
                            return { v: o.code, label: (c ? c.name : (s.dt.chars && s.dt.chars[o.code]) || N.humanize(o.code)) + " · " + o.count }
                        }))
                        onChosen: (v) => at.owner = v
                    }
                }
                // external animation import
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 6
                    readonly property var pr: studio.importProgress
                    Btn {
                        kind: "primary"; icon: "download"; text: "Import animation…"
                        enabled: !!s.actor && !parent.pr.running
                        tooltip: "Mixamo / mocap (.fbx .bvh .glb .gltf .dae) onto the selected character at the playhead. Blender converts it, the retargeter maps it onto the RE4 skeleton."
                        onClicked: { const p = studio.pickFile("anim"); if (p) studio.importAnim(p, s.actor.id, s.t) }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: parent.pr.running ? parent.pr.stage + "…" : parent.pr.error ? parent.pr.error : parent.pr.result ? parent.pr.result.clip + ": " + parent.pr.result.keys + " keys, " + parent.pr.result.mapped + " joints" : ""
                        color: parent.pr.error ? Theme.danger : Theme.muted
                        font.pixelSize: Theme.smallSize; elide: Text.ElideRight
                    }
                }
                Flow {
                    visible: (s.dt && s.dt.imports ? s.dt.imports.length : 0) > 0
                    Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 6
                    spacing: 3
                    Text { text: "Imported for " + (s.dt ? s.dt.rig_code : "") + ":"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                    Repeater {
                        model: s.dt ? s.dt.imports : []
                        Chip { required property string modelData; text: modelData; tooltip: "Add at the playhead"; onClicked: s.cmd("import_anim", { file: "director/import/" + s.dt.rig_code + "/" + modelData + ".json", start: s.t }) }
                    }
                }
                Text {
                    visible: !s.actor
                    Layout.fillWidth: true; Layout.margins: 8; wrapMode: Text.WordWrap
                    text: "Select a character first (Animation Set Editor or the picture), then open a set."
                    color: Theme.amber; font.pixelSize: Theme.smallSize
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: s.dt && s.dt.catalog ? s.dt.catalog.results : []
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ListRow {
                        id: arow
                        required property var modelData
                        readonly property var e: modelData
                        readonly property var loaded: s.actor ? N.find(s.actor.banks, b => b.id === e.bank) : null
                        // in the game a character can only play its own rig's lists; the Studio adapts any list
                        readonly property bool fromOther: !!s.actor && !!at.actorOwner && !!e.c && e.c !== at.actorOwner
                        readonly property bool otherRig: !studio.standalone && fromOther && /^cha\d$/.test(e.c)
                        icon: "film"
                        title: N.setLabel(e.n, e.c, e.g)
                        subtitle: (otherRig ? "for " + ((s.dt.chars && s.dt.chars[e.c]) || e.c) : N.groupLabel(e.g) + (fromOther && studio.standalone ? " · from " + ((s.dt.chars && s.dt.chars[e.c]) || e.c) : "")) + (loaded ? " · " + (loaded.ready ? loaded.count + " clips" : "loading…") : "")
                        dim: !s.actor || otherRig
                        selected: !!loaded && loaded.ready
                        tooltip: otherRig ? "Select that character to use this animation" : "Open the set: hover a clip to preview it on the character"
                        onClicked: {
                            if (!s.actor || otherRig) return
                            s.send("load_motlist", { path: e.p })
                            Ui.drill = { bank: e.bank, path: e.p, label: title }
                        }
                        Btn { visible: !!s.actor && !arow.otherRig; text: "Open" }
                    }
                }
            }
        }
    }

    // ================================================================== clips of an open set
    Component {
        id: clipsTab
        ColumnLayout {
            id: cl
            spacing: 4
            property string filter: ""
            property bool hoverPlay: true
            readonly property var bank: s.actor && Ui.drill ? N.find(s.actor.banks, b => b.id === Ui.drill.bank) : null
            readonly property var motions: Ui.drill && s.dt && s.dt.motions ? (s.dt.motions[String(Ui.drill.bank)] || []) : []
            readonly property var list: {
                const norm = (t) => String(t).toLowerCase().replace(/[_\s]+/g, " ").trim()
                const nq = norm(filter)
                return motions.filter(m => !nq || norm(m.name).indexOf(nq) >= 0 || norm(N.motionLabel(m.name).label).indexOf(nq) >= 0 || String(m.id) === nq)
            }
            readonly property var playing: s.actor ? N.find(s.actor.layers, l => l.idx === Ui.layer) : null
            // hover: the clip plays while the mouse rests on it (standalone: a preview that ends when the mouse
            // leaves; in the game: the clip is played on the layer as before)
            Timer {
                id: hoverTimer; interval: 350; property var m: null
                onTriggered: if (m && Ui.drill) s.send(studio.standalone ? "play_preview" : "play", { bank: Ui.drill.bank, mot: m.id, layer: Ui.layer, blend: 6, speed: Ui.speed })
            }
            Timer { id: hoverEnd; interval: 150; onTriggered: if (studio.standalone) s.send("play_preview_end") }
            function hoverOn(m) { hoverEnd.stop(); if (hoverPlay) { hoverTimer.m = m; hoverTimer.restart() } }
            function hoverOff() { hoverTimer.stop(); hoverEnd.restart() }
            Component.onDestruction: if (studio.standalone) s.send("play_preview_end")
            onHoverPlayChanged: if (!hoverPlay) hoverOff()
            RowLayout {
                Layout.fillWidth: true; Layout.margins: 6
                IconButton { icon: "arrow-left"; tooltip: "Back to the sets"; onClicked: Ui.drill = null }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { Layout.fillWidth: true; text: Ui.drill ? Ui.drill.label : ""; color: Theme.textBright; font.pixelSize: Theme.fontSize; font.bold: true; elide: Text.ElideRight }
                    Text { text: (cl.bank && cl.bank.ready ? cl.motions.length + " clips" : "loading…") + " · on " + (s.actor ? N.actorLabel(s.actor) : "—") + " · layer " + Ui.layer; color: Theme.muted; font.pixelSize: Theme.smallSize }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 6
                SearchField { Layout.fillWidth: true; placeholderText: "Filter clips…"; onTextChanged: cl.filter = text }
                Chip { text: "Hover plays"; checked: cl.hoverPlay; tooltip: "Play a clip on the character while the mouse rests on it"; onClicked: cl.hoverPlay = !cl.hoverPlay }
                Btn { visible: studio.standalone && !!cl.playing; icon: "square"; text: "Stop"; tooltip: "Stop the clip you clicked (the timeline and the idle take over)"; onClicked: s.send("play_stop") }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: cl.list
                ScrollBar.vertical: ScrollBar {}
                delegate: ListRow {
                    id: clrow
                    required property var modelData
                    readonly property var m: modelData
                    readonly property var ml: N.motionLabel(m.name)
                    icon: "play"
                    title: ml.label
                    subtitle: (ml.id ? "#" + ml.id + " · " : "") + (m.endframe / s.fps).toFixed(1) + " s"
                    tooltip: m.name
                    selected: !!cl.playing && cl.playing.bank === Ui.drill.bank && cl.playing.mot === m.id
                    onEntered: cl.hoverOn(m)
                    onExited: cl.hoverOff()
                    onClicked: {
                        // clicking the clip that plays stops it; another clip plays until Stop
                        if (studio.standalone && selected) { s.send("play_stop"); return }
                        s.send("play", { bank: Ui.drill.bank, mot: m.id, layer: Ui.layer, blend: Ui.blend, speed: Ui.speed })
                    }
                    Btn { icon: "plus"; text: "Timeline"; tooltip: "Add to the timeline at the playhead"; onClicked: s.cmd("add_clip", { bank: Ui.drill.bank, mot: clrow.m.id, layer: Ui.layer, blend: Ui.blend, speed: Ui.speed, name: clrow.m.name, endframe: clrow.m.endframe }) }
                }
            }
        }
    }

    // ================================================================== props
    Component {
        id: propsTab
        RowLayout {
            id: pt
            spacing: 0
            property string cat: "prop"
            Timer { id: meshTimer; interval: 250; onTriggered: if (pt.cat !== "nearby") s.send("mesh_search", { q: root.q, cat: pt.cat === "all" ? "" : pt.cat }) }
            Connections { target: root; function onQChanged() { meshTimer.restart() } }
            onCatChanged: { s.send("want_objects", { value: cat === "nearby" }); meshTimer.restart(); leave() }
            Component.onCompleted: { s.send("want_objects", { value: cat === "nearby" }); meshTimer.restart() }
            Component.onDestruction: { s.send("want_objects", { value: false }); s.send("mesh_preview_clear") }
            function leave() { root.cancelPreview(); s.send("mesh_preview_clear") }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 88
                color: "#262626"
                Column {
                    width: parent.width
                    Repeater {
                        model: [["prop", "Props"], ["weapon", "Weapons"], ["environment", "World"], ["nearby", "Nearby"]]
                        RailButton { required property var modelData; text: modelData[1]; checked: pt.cat === modelData[0]; onClicked: pt.cat = modelData[0] }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 0
                Text {
                    Layout.fillWidth: true; Layout.margins: 6; wrapMode: Text.WordWrap
                    text: pt.cat === "nearby" ? "Objects around you in the level: click one to take control of it (move it, key it)."
                        : studio.standalone ? "Hover a prop to see it turning in front of the camera · click to add it" : "Hover a prop to see it turning in the game · click to drop it in front of the camera"
                    color: Theme.muted; font.pixelSize: Theme.smallSize
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    onMovementStarted: pt.leave()
                    model: pt.cat === "nearby" ? (s.dt ? s.dt.objects : []).filter(o => !root.q || N.objectName(o.name).toLowerCase().indexOf(root.q.toLowerCase()) >= 0) : (s.dt ? s.dt.meshes : [])
                    delegate: ListRow {
                        id: prow
                        required property var modelData
                        readonly property var m: modelData
                        readonly property bool nearby: pt.cat === "nearby"
                        readonly property var prev: s.dt ? s.dt.mesh_preview : null
                        readonly property bool showing: !nearby && !!prev && prev.name === m.n
                        icon: nearby ? "box" : "sparkles"
                        image: !nearby && studio.standalone ? "image://thumb/" + encodeURIComponent(m.id || ("mesh:" + m.p + (m.mdf ? "|" + m.mdf : ""))) : ""
                        title: nearby ? N.objectName(m.name) : (m.name || N.objectName(m.n))
                        subtitle: nearby ? m.dist.toFixed(1) + " m" + (m.added ? " · directed" : "")
                                         : showing && prev.size ? prev.size.map(v => v >= 1 ? v.toFixed(1) : v.toFixed(2)).join(" × ") + " m" : (m.name ? m.n : m.c + (m.mdf || studio.standalone ? "" : " · no material"))
                        tooltip: nearby ? m.name : m.p
                        selected: nearby ? s.sel.actor === m.addr : showing
                        onEntered: if (!nearby) root.preview(() => s.send("mesh_preview", { mesh: m.p, mdf: m.mdf || undefined, name: m.n }), 220)
                        onExited: if (!nearby) pt.leave()
                        onClicked: {
                            if (nearby) s.send("select_actor", { addr: m.addr })
                            else { pt.leave(); s.cmd("spawn_mesh", { mesh: m.p, mdf: m.mdf || undefined, name: m.n }) }
                        }
                        Btn { visible: !prow.nearby; icon: "plus"; text: "Add" }
                    }
                }
            }
        }
    }

    // ================================================================== sounds
    Component {
        id: soundsTab
        RowLayout {
            id: sn
            spacing: 0
            property string cat: ""
            property var results: []
            property int total: 0
            property string err: ""
            property var pinned: null
            readonly property var playing: s.live && s.st.sound_preview ? s.st.sound_preview.event : null
            function search() {
                const r = studio.searchSounds(root.q, cat, 240)
                results = r.results || []; total = r.total || 0; err = r.error || ""
            }
            Timer { id: soundSearch; interval: 220; onTriggered: sn.search() }
            Timer { id: soundHover; interval: 300; property var snd: null; onTriggered: if (snd) s.send("sound_preview", { event: snd.event, trigger: snd.triggers && snd.triggers.length ? snd.triggers[0] : undefined }) }
            Connections { target: root; function onQChanged() { soundSearch.restart() } }
            onCatChanged: soundSearch.restart()
            Component.onCompleted: soundSearch.restart()
            Component.onDestruction: s.send("sound_stop")
            function stop() { soundHover.stop(); pinned = null; s.send("sound_stop") }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 88
                color: "#262626"
                Column {
                    width: parent.width
                    Repeater {
                        model: [["", "All"], ["music", "Music"], ["voice", "Voices"], ["ambience", "World"], ["sfx", "Effects"], ["cutscene", "Scenes"], ["ui", "UI"]]
                        RailButton { required property var modelData; text: modelData[1]; checked: sn.cat === modelData[0]; onClicked: sn.cat = modelData[0] }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 0
                Section {
                    Layout.fillWidth: true
                    title: "Game sound mix"; icon: "sliders-horizontal"; open: false
                    note: "what a render records"
                    Repeater {
                        model: [["master", "Everything"], ["music", "Music"], ["sfx", "Effects"], ["voice", "Voices"], ["cutscene", "Cutscenes"]]
                        SliderRow {
                            required property var modelData
                            label: modelData[1]; step: 0.05
                            value: s.live && s.st.mix ? (s.st.mix[modelData[0]] === undefined ? 1 : s.st.mix[modelData[0]]) : 1
                            format: (v) => Math.round(v * 100) + " %"
                            onEdited: (v) => { const f = {}; f[modelData[0]] = v; s.send("sound_mix", { fields: f }) }
                        }
                    }
                    Btn { kind: "ghost"; text: "Reset"; onClicked: s.send("sound_mix", { reset: true }) }
                }
                Text { visible: sn.err !== "" || (s.live && !!s.st.sound_preview_error); Layout.fillWidth: true; Layout.margins: 6; wrapMode: Text.WordWrap; text: sn.err || ("Preview unavailable: " + (s.live ? s.st.sound_preview_error : "")); color: Theme.danger; font.pixelSize: Theme.smallSize }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sn.results
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ListRow {
                        required property var modelData
                        readonly property var snd: modelData
                        icon: sn.playing === snd.event ? "square" : "volume-2"
                        iconColor: sn.playing === snd.event ? Theme.ok : "#aab6c8"
                        title: N.humanize(snd.name)
                        subtitle: N.humanize(snd.category) + " · " + snd.bank
                        tooltip: (snd.path || snd.bank) + " · hover to hear it"
                        selected: sn.playing === snd.event
                        onEntered: if (sn.pinned === null) { soundHover.snd = snd; soundHover.restart() }
                        onExited: { soundHover.stop(); if (sn.pinned === null && sn.playing !== null) s.send("sound_stop") }
                        onClicked: { if (sn.pinned === snd.event) sn.stop(); else { sn.pinned = snd.event; s.send("sound_preview", { event: snd.event, trigger: snd.triggers && snd.triggers.length ? snd.triggers[0] : undefined }) } }
                    }
                    footer: Text {
                        width: ListView.view.width
                        visible: sn.results.length > 0
                        padding: 8
                        wrapMode: Text.WordWrap
                        text: "Showing " + sn.results.length + " of " + sn.total.toLocaleString() + " sounds · a sound only plays when the game has its bank loaded"
                        color: Theme.faint; font.pixelSize: Theme.smallSize
                    }
                }
            }
        }
    }

    // ================================================================== stages
    Component {
        id: stagesTab
        RowLayout {
            id: sg
            spacing: 0
            property string pick: ""
            readonly property var maps: {
                const out = []
                for (const e of (s.dt ? s.dt.stages : [])) if (e.map && out.indexOf(e.map) < 0) out.push(e.map)
                return out
            }
            readonly property string map: maps.indexOf(pick) >= 0 ? pick : (maps.length ? maps[0] : "")
            property bool allAreas: false
            readonly property var stage: s.live ? s.st.stage : null
            readonly property var status: stage ? stage.status : null
            // standalone: the map is loaded into the film from the game's files; live: the game travels there
            readonly property bool own: !!stage && !!stage.standalone
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 88
                color: "#262626"
                Column {
                    width: parent.width
                    Repeater {
                        model: sg.maps
                        RailButton { required property string modelData; text: modelData; checked: sg.map === modelData; onClicked: sg.pick = modelData }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 0
                Rectangle {
                    visible: !!sg.status
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: sg.status && sg.status.phase === "failed" ? "#4a2222" : sg.status && sg.status.phase === "done" ? "#23402a" : "#2a3140"
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 6
                        Icon { name: sg.stage && sg.stage.busy ? "loader-circle" : "map"; size: 14; color: Theme.text }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Theme.text; font.pixelSize: Theme.smallSize
                            text: !sg.status ? "" : sg.status.phase === "loading" ? "Loading " + sg.status.name + "…" + (sg.status.total ? "  " + sg.status.done + " / " + sg.status.total + " meshes" : "")
                                : sg.status.phase === "warping" ? "Moving to " + sg.status.name + "…"
                                : sg.status.phase === "done" ? (sg.own ? sg.status.name + " is the film's set" : "You are at " + sg.status.name + (sg.status.flat_light ? " · lit flat (other region)" : ""))
                                : (sg.own ? "Could not load " : "Could not go to ") + sg.status.name + (sg.status.error ? " — " + sg.status.error : "")
                        }
                        Btn { visible: !!sg.stage && sg.stage.busy; kind: "ghost"; text: "Cancel"; onClicked: s.send("stage_cancel") }
                        Btn { visible: sg.own && !!sg.stage.current && !sg.stage.busy; kind: "ghost"; text: "Remove"; tooltip: "Back to the empty studio stage"; onClicked: s.cmd("stage_clear") }
                    }
                }
                Text {
                    visible: (s.dt ? s.dt.stages.length : 0) === 0
                    Layout.fillWidth: true; Layout.margins: 8; wrapMode: Text.WordWrap
                    text: sg.own ? "This game has no maps Director can read yet." : "No areas listed yet. Be in the game world (load a save once), then read the map."
                    color: Theme.muted; font.pixelSize: Theme.smallSize
                }
                Btn { visible: !sg.own && (s.dt ? s.dt.stages.length : 0) === 0; Layout.leftMargin: 8; text: "Read the map"; onClicked: s.send("stage_list") }
                Flow {
                    visible: sg.own
                    Layout.fillWidth: true
                    Layout.margins: 6
                    spacing: 4
                    Chip { text: "All areas"; checked: sg.allAreas; tooltip: "Also list the areas without a name on the in-game map (by code)"; onClicked: sg.allAreas = !sg.allAreas }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    model: (s.dt ? s.dt.stages : []).filter(e => e.map === sg.map && (sg.allAreas || !e.unnamed) && (!root.q || e.name.toLowerCase().indexOf(root.q.toLowerCase()) >= 0))
                    delegate: ListRow {
                        id: srow
                        required property var modelData
                        readonly property var e: modelData
                        readonly property bool here: !!sg.stage && ((sg.stage.current && sg.stage.current.stage === e.stage) || (!!sg.status && sg.status.phase === "done" && sg.status.stage === e.stage))
                        readonly property bool cross: !sg.own && !!sg.stage && !!sg.stage.current && !!sg.stage.current.stage && Math.floor(e.stage / 10000) !== Math.floor(sg.stage.current.stage / 10000)
                        icon: "map"
                        title: e.name
                        subtitle: here ? (sg.own ? "the film's set" : "you are here") : (e.unnamed ? e.map + " · unnamed area" : e.pos && e.pos.length ? e.map + " · loc" + Math.floor(e.loc / 1000) : e.map) + (cross ? " · other region: loads, but keeps this region's lighting" : "")
                        tooltip: (e.code || "st" + e.stage) + (e.pos && e.pos.length ? " · " + e.pos.map(v => v.toFixed(0)).join(", ") : "")
                        selected: here
                        always: true
                        function go() { if (!(sg.stage && sg.stage.busy)) s.send("stage_go", { stage: e.stage, name: e.name, pos: e.pos, id: e.id }) }
                        onClicked: go()
                        Btn { kind: "primary"; text: sg.own ? "Load" : "Go"; enabled: !(sg.stage && sg.stage.busy); onClicked: srow.go() }
                    }
                }
            }
        }
    }
}
