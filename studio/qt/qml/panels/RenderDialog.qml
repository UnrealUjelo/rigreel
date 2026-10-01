import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// File ▸ Export ▸ Movie (SFM's movie export): what to render, how big, how fast, with or without the game's sound.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg
    Store { id: s }

    property string name: s.live && s.st.game.project ? s.st.game.project : "director_shot"
    property string format: "mp4"
    property int fps: 30
    property string res: "1080"
    property bool draft: false
    property bool keepPng: false
    property bool gameAudio: true
    property int blur: 1                  // motion blur samples per frame (1 = off)
    property int shutter: 180
    property string range: s.range ? "selection" : "all"
    property string error: ""
    readonly property var rp: studio.render
    readonly property bool running: !!rp.running
    readonly property int pct: running && rp.total > 0 ? Math.round(rp.frame / rp.total * 100) : 0
    readonly property var shots: s.seq.shots || []
    readonly property real filmFrames: shots.reduce((n, x) => n + Math.max(1, x.b - x.a), 0)
    readonly property var span: {
        if (range === "film") return [0, filmFrames]
        if (range === "selection" && s.range) return [s.range[0], s.range[1]]
        if (range === "shot" && s.shot) return [s.shot.a, s.shot.b]
        return [0, s.seq.length || 600]
    }

    function begin() {
        error = ""
        const wh = { "540": [960, 540], "720": [1280, 720], "1080": [1920, 1080], "1440": [2560, 1440], "2160": [3840, 2160] }[res]
        const ok = studio.startRender({ name: name, fps: fps, from: span[0], to: span[1], film: range === "film", width: wh[0], height: wh[1], quality: draft || res === "540" ? "draft" : "final",
                                        png_seq: keepPng, output_format: format, game_audio: format === "mp4" && gameAudio && !studio.standalone,
                                        motion_blur: studio.standalone ? blur : 1, shutter: shutter })
        if (!ok) error = "A render is already running. Wait for it to finish."
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Icon { name: "clapperboard"; size: 20; color: Theme.amber }
            Text { text: "Export Movie"; color: Theme.textBright; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { text: "frames " + root.span[0] + "–" + root.span[1] + " · " + ((root.span[1] - root.span[0]) / s.fps).toFixed(1) + " s"; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
        Field { label: "Range"; labelWidth: 110
            Chip { text: "Whole film"; checked: root.range === "all"; onClicked: root.range = "all" }
            Chip { text: "Time selection"; checked: root.range === "selection"; enabled: !!s.range; opacity: s.range ? 1 : 0.4; onClicked: if (s.range) root.range = "selection" }
            Chip { text: s.shot ? "Shot · " + s.shot.name : "Current shot"; checked: root.range === "shot"; opacity: s.shot ? 1 : 0.4; onClicked: if (s.shot) root.range = "shot" }
            Chip { visible: studio.standalone && root.shots.length > 0; text: "The film · " + root.shots.length + " shots in order"; checked: root.range === "film"; onClicked: root.range = "film" }
        }
        Field { label: "Format"; labelWidth: 110
            Choice { Layout.fillWidth: true; value: root.format; options: [{ v: "mp4", label: "Video (.mp4, H.264)" }].concat(studio.standalone ? [{ v: "prores", label: "Video for editing (.mov, ProRes 422 HQ)" }] : []).concat([{ v: "png", label: "Image sequence (PNG frames)" }]); onChosen: (v) => root.format = v }
        }
        Field { label: "File name"; labelWidth: 110
            TextBox { Layout.fillWidth: true; text: root.name; onTextChanged: root.name = text }
            Text { text: root.format === "mp4" ? ".mp4 in Downloads" : root.format === "prores" ? ".mov in Downloads" : "_frames folder in Downloads"; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
        Field { label: "Frame rate"; labelWidth: 110
            Choice { Layout.fillWidth: true; value: root.fps; options: [{ v: 24, label: "24 fps · film" }, { v: 25, label: "25 fps · PAL" }, { v: 30, label: "30 fps" }, { v: 60, label: "60 fps" }]; onChosen: (v) => root.fps = v }
        }
        Field { label: "Size"; labelWidth: 110
            Choice { Layout.fillWidth: true; value: root.res; options: [{ v: "540", label: "960 × 540 · quick preview" }, { v: "720", label: "1280 × 720" }, { v: "1080", label: "1920 × 1080 · Full HD" }, { v: "1440", label: "2560 × 1440" }, { v: "2160", label: "3840 × 2160 · 4K" }]; onChosen: (v) => root.res = v }
        }
        Field { label: "Motion blur"; labelWidth: 110; visible: studio.standalone
            Choice { Layout.fillWidth: true; value: root.blur; options: [{ v: 1, label: "Off" }, { v: 8, label: "8 samples per frame" }, { v: 16, label: "16 samples · smooth" }, { v: 32, label: "32 samples · fast motion" }]; onChosen: (v) => root.blur = v }
            Choice { visible: root.blur > 1; implicitWidth: 120; value: root.shutter; options: [{ v: 90, label: "90° shutter" }, { v: 180, label: "180° · film" }, { v: 270, label: "270°" }, { v: 360, label: "360° · open" }]; onChosen: (v) => root.shutter = v }
        }
        ColumnLayout {
            visible: root.format === "mp4" || root.format === "prores"
            spacing: 4
            Toggle { visible: !studio.standalone; text: "Game sound (music, voices, effects) — the range plays once in real time first"; checked: root.gameAudio; onToggled: (v) => root.gameAudio = v }
            Toggle { visible: root.format === "mp4"; text: "Draft quality (faster, bigger file)"; checked: root.draft; onToggled: (v) => root.draft = v }
            Toggle { text: "Also keep the PNG frames"; checked: root.keepPng; onToggled: (v) => root.keepPng = v }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: studio.standalone ? "Frame-accurate: the film is stepped one frame at a time and drawn at the output size, so the result never stutters, whatever your PC does. Motion blur draws each frame several times (8 samples: about 8x longer)."
                                    : "Frame-accurate: the game is stepped one frame at a time and grabbed exactly, so the result never stutters, whatever your PC does. The game window moves to the top-left corner at full size while it renders."
            color: Theme.faint; font.pixelSize: Theme.smallSize
        }
        Rectangle {
            visible: root.running
            Layout.fillWidth: true
            implicitHeight: 24; radius: 4; color: Theme.field; border.color: Theme.border
            Rectangle { width: parent.width * root.pct / 100; height: parent.height; radius: 4; color: "#3b5f8f" }
            Text { anchors.centerIn: parent; color: Theme.textBright; font.pixelSize: Theme.smallSize; text: root.rp.stage === "recording audio" ? "Recording the game's sound…" : root.rp.stage === "encoding" ? "Encoding…" : "Frame " + root.rp.frame + " / " + root.rp.total + (root.blur > 1 && root.rp.sub ? " (" + root.rp.sub + "/" + root.blur + ")" : "") + "  ·  " + root.pct + " %" }
        }
        Text { visible: !!(root.error || root.rp.error); Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.error || root.rp.error || ""; color: Theme.danger; font.pixelSize: Theme.smallSize }
        Text { visible: !root.running && root.rp.stage === "done" && !root.rp.error && !!root.rp.out; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; text: "Saved: " + root.rp.out; color: Theme.ok; font.pixelSize: Theme.smallSize }
        Text { visible: !studio.connected; text: "The game runtime is not connected."; color: Theme.danger; font.pixelSize: Theme.smallSize }
        Item { Layout.fillHeight: true }
        RowLayout {
            Btn { kind: "ghost"; icon: "folder-open"; text: "Open Downloads"; onClicked: studio.openFolder("renders") }
            Item { Layout.fillWidth: true }
            Btn { visible: root.running; kind: "danger"; text: "Cancel"; onClicked: studio.cancelRender() }
            Btn { kind: "ghost"; text: "Close"; onClicked: root.closeRequested() }
            Btn {
                kind: "primary"; small: false; icon: "clapperboard"
                text: root.running ? "Rendering " + root.pct + " %" : "Render"
                enabled: !root.running && studio.connected && (studio.standalone || !!studio.game.found)
                onClicked: root.begin()
            }
        }
    }
}
