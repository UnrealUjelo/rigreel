# RigReel Studio — architecture (Qt, 2026-09)

The public product name is **RigReel Studio**. Some internal APIs, QML URIs, live-runtime folders, and legacy data paths retain the `Director` name so existing films and installations remain compatible.

RigReel Studio is an animation program: you make films with the characters, sets and motion of **games you
own**. Game plugins read the installed game's own files (the game does not run, nothing is copied or shared);
the Studio animates and renders the film itself with Qt Quick 3D. The first plugin covers RE Engine games
(Resident Evil 4 is the primary tested target). For RE4 there is also a **live** mode where the running game animates the film through
the Director mod, as before.

```
┌────────────────────────────── RigReelStudio.exe (Qt 6.10, C++ / QML / Qt Quick 3D) ───────────────────────────────┐
 │ MainWindow (QMainWindow + QDockWidgets)             Studio (QObject, context property `studio`)                   │
 │  ├ Animation Set Editor ┐                             │  runtimeMode "standalone" (default) | "live"               │
 │  ├ Element Viewer       │ QQuickWidgets sharing       ├─ standalone ─ GameLibrary ── plugins/games/*.dll          │
 │  ├ Asset Browser        │ ONE QQmlEngine              │               StageRuntime (film, ops, evaluation)        │
 │  ├ Properties           │                             │               StageScene  (Qt Quick 3D picture)           │
 │  ├ Timeline             │                             │               FilmRender  (export: step, grab, ffmpeg)    │
 │  └ Console              ┘                             └─ live ─────── Bridge (file IPC) · GameWindow · RenderJob  │
 │  centre: ViewportHeader | [Viewport3D.qml  or  GameHost] | ViewportBar                                            │
 │ ControlServer (127.0.0.1:47931) ── UiAutomation ── MCP server                                                     │
 └───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
      │ IGamePlugin / IGameSource (sdk/director)                  ▲ live mode only: reframework/data/director/bridge
      ▼                                                           ▼
 ┌──── reengine.dll (RE Engine plugin) ────────────────┐   ┌──── re4.exe + REFramework ── mod/Director/*.lua ────────┐
 │ paks (KPKA, encrypted index, zstd/deflate) · tex ·  │   │ bridge.lua ops/state · sequence · camera · actor · ...   │
 │ mesh · mdf2 · pfb/scn/user (RSZ) · motlist · looks  │   └───────────────────────────────────────────────────────────┘
 └─────────────────────────────────────────────────────┘
```

## One contract, two runtimes
Every panel talks to "the runtime" through the same ops and the same state / data shapes, so QML does not care
which one is active:
* **Commands**: `Studio::send(op, args)`. Standalone: `StageRuntime::send` runs the op at once (undo snapshot
  first unless the op is read-only). Live: `Bridge` appends `{op, ..., cid}` to `cmd.json`; the Lua runtime acks.
* **State** (selection, sequence, actors, cameras, lights, gizmo, history …) and **Data** (cast catalog, animation
  catalog, motions of open sets, joints, projects, poses, log). `StageRuntime::buildState/buildData` produce the
  exact shapes of `bridge.lua build_state/build_data`; `Bridge::normalizeState/normalizeData` apply to both.
* Op names and semantics follow `mod/Director/bridge.lua`. Ops that only exist inside the game (freeze the
  world, HUD, AI, ragdolls, the game's stage list) are no-ops or "needs the game" in standalone; their menu
  entries are hidden (`MainWindow::m_liveOnly`).

## Game plugins (`sdk/director`, `plugins/`)
* `GamePlugin.h`: `IGamePlugin` (id, known games, `detect(steamLibraries)`, `probe(folder)`, `open`) and
  `IGameSource` (catalog per kind, `loadModel`, `loadAnimations`, `animationsFor`, `loadTexture`, `loadStage`,
  `thumbnail`, files).
  Plugins are Qt plugins (`plugins/games/*.dll`, IID `studio.director.GamePlugin/1`), loaded by `GameLibrary`.
* `Assets.h`: engine-neutral assets — `ModelAsset` (skeleton + mesh pieces + materials), `TextureAsset` (BCn or
  RGBA mips), `AnimationSet/Clip` (per-bone curves keyed by bone-name hash), catalog entries.
* **RE Engine plugin** (`plugins/reengine`): `Pak` (KPKA index, RSA-wrapped entry keys, murmur3 path hashes,
  patch priority, loose `natives` override), `Tex` (resident + `streaming/` mips merged, capped at 2048), `Bcn`
  (BC1-7 decode/encode), `Mesh`, `Mdf` (material roles and RE4 packing: ALBD/NRMR/ATOC), `Rsz` (pfb/scn/user
  with the `rsz<game>.json` type dump), `Mot` (motlist 663, all compression modes), `Profiles` (13 RE Engine
  games), `Re4Cast` (characters and looks from CostumePreset data), `Thumbnail` (software rasteriser for browser
  pictures). Name lists and type dumps come from the plugin data dir, `DIRECTOR_REASY`, or `tools/REasy`;
  `data/re4/{cast,stage,prop}_names.json` carry the human names.
* **Maps** (RE4): each area chunk has a root `appsystem/scene/optimized/stage/stNN/stNN_NNN.scn` linking its
  scenes through `via.Folder` (`ScenePath` + `UniversalOffset`, a double vec3): static terrain (+ the enhanced or
  legacy ground next to `static.scn`), props, gimmicks, lights (occluders, sound, events, navigation are skipped).
  The game streams a block of chunks together (`st40_2xx`: the square, each house, yards) plus the region's
  landscape (`stNN_000`), so a named area loads that block. Objects become placements (mesh + mdf + world matrix);
  streaming stand-ins and switched-off meshes are left out. World materials: NRRC normals (r roughness, g/a
  normal, b cavity), `UV_Tiling` / `BasicMap_Tiling`, layered `Env_*Layer*_Dirt` (second layer by LYMO red, dirt
  by blue, occlusion alpha, on UV1), decals blended by ATOS red, colour alpha is not metalness. Ground is
  `NewGroundShader`: a 16-layer texture array (handed out as a 4x4 atlas) chosen per spot by a map-wide splat map
  (R8, value / 16, addressed by UV0 = world / 1000 m), tiled every `Tiling` metres.
* `tools/recli` exercises the plugin without the UI (`info`, `ls`, `extract`, `tex`, `mesh`, `mot --clip`,
  `cast`, `model [--thumb]`, `catalog`, `stage [--load]`, `rsz`, `rest <model> <motlist> [clip]` (a clip against a
  skeleton, `RECLI_TOPS=1` top bones only), `rigdiff <modelA> <modelB>` (`RECLI_PARENTS=1`); `RECLI_RAW=1` lists every material slot and
  parameter, `RECLI_UV=1` UV sets); `tools/dirview` is a bare 3D viewer (`--look`, `--anim`, `--clip`, `--shot`).

## Standalone engine (`src/engine`)
| File | Role |
|---|---|
| `GameLibrary` | plugins, installed games (Steam libraries + added folders); the active game (what the Asset Browser lists) and a pool of every game opened so far (`sourceForId`, opened on worker threads, never closed while the Studio runs) |
| `StageGames` | several games in one film: `game::local` references (`refGame` / `refLocal` / `qualify` / `sourceOf`), each game's cast (`ensureGameCast`), films from before tagged on load (`qualifyDocument`) |
| `StageRuntime` (+ `StageOps`, `StageEval`) | the film document, ops, undo, transport clock, asset loading (models / motion lists on worker threads), catalogs, evaluation of every actor, camera and light at time t |
| `Film` | document model and JSON (`*.film.json` in Documents/Director Studio/films): actors, cameras, lights, sequence (anim / pose / xform / camera tracks, cuts, shots) |
| `Pose` | local poses, clip binding by bone hash, sampling, root track |
| `ModelPrep` / `TextureStore` / `GpuTexture` | vertex buffers (top-4 skin weights), KTX cache of converted textures, textures uploaded as native BCn by a render extension (Qt's own loader cannot) |
| `SceneActor` / `StageScene` | Qt Quick 3D nodes per actor (skinned models, `ReMaterial.qml` / `ReMultiply.qml`), lights, both cameras, navigation (look / pan / dolly / orbit / fly / frame), picking |
| `StageSet` | the film's map: one geometry + material set per unique mesh, drawn at all placements by instancing; CPU ray casts for "ground below" (spawning, placing the camera) and along any ray (clicks in the picture) |
| `StageRig` | the rig pass after posing: walk paths place characters (Catmull-Rom curve, the walk clip kept in step), look-at (head + neck share), constraints (two-bone IK hold / plant, look), props riding on joints; their ops and the viewport pen (path points, Place) |
| `StageDrive` | Drive & Record: WASD relative to the view with the character's own stand / walk / jog loops; R records position keys + clips |
| `StageImport` | export_rig / import_anim / export_anim for the Blender + retarget.py pipeline (files under Documents/Director Studio) |
| `StageGizmo` | the viewport manipulator: where the handles sit (`gizmo.frame`) and begin / drag / end with snapping; bones turn into the working pose |
| `StageChannels` | channel tracks (SFM's animatable values): `light:<id>`, `cam:<i>`, `actor:<id>`, `constraint:<id>`, `world` + a field; applied each evaluation; a value with a channel that an op changes is keyed at the current frame (`keyChangedChannels` in `send`) |
| `StageAutosave` | unsaved changes every minute to Documents/Director Studio/autosave; `session/running` flag; `state.recovery` + `recover_autosave` / `discard_autosave` |
| `StagePhysics` | secondary motion: `*_chain*` bones (hair, cloth, straps, sleeves, collars) as damped spring particles towards the animated pose, kept at bone length, bones re-aimed; steps with film time (play / render), resets on a scrub |
| `ThumbnailProvider` | `image://thumb/<game::model id>`: plugin thumbnails on worker threads, cached as PNG per game |
| `Maths` | glm-compatible Euler conversions (plus `toEulerUpright` for display), easing, look-at |

Evaluation rules worth knowing:
* **Root motion**: RE clips move the skeleton's top bone in model space. Travel is measured from each clip's
  first frame; loops and consecutive base-layer clips carry the character on; overlay layers never move it;
  previews and idles play in place; *Lock root* keeps everything in place.
* Timeline clips sample at film frames (60 fps); a clip blends from the previous one over its blend frames; a
  gap holds the last clip's final frame.
* **Clips from other characters** (`Retarget` in `Pose`): a list's owner is its `/animation/ch/<code>/` folder;
  when that is not the actor's code, the owner's skeleton (its first look) loads in the background and the clip
  plays as offsets from it: positions always (target bind + key - source bind), rotations only on face bones
  (below `Head` or a `Facial*` bone), since body bones share one joint convention. Without a known owner the
  file's own rest positions stand in. Facial lists are partial skeletons whose top bone (Spine_2) is stored in
  model space: such tops are skipped (played locally they lifted the upper body by a metre). A mot's bone list
  rest pose is the first frame of the list's first clip, not the bind pose (`recli rest`, `recli rigdiff`).
* Joint names differ between games (RE4 `L_UpperArm`, `R_Wep`; RE2 `l_arm_humerus`, `r_weapon`): the engine asks
  for RE4 names through `findJoint`, which tries the other spelling.
* **Several games in one film.** Every asset reference names its game: `re2rt::look:pl0000/0`,
  `re4::natives/stm/_chainsaw/animation/ch/cha0/motlist/cha0_general.motlist.663`, `re4::<stage id>`. A reference
  without one is the film's game (`Document.gameKey`, older films: `plugin:game:folder`); `restore()` tags them.
  Models, motion lists, maps, rigs and thumbnails load from their own game (`sourceOf`), which opens in the
  background if needed (`GameLibrary::sourceOpened` retries what waited). The Asset Browser's **From** picker
  switches the listed game (`GameLibrary::activate`, instant for an open one) without touching the film; catalog
  rows come out qualified with the listed game.
* **Channels**: a `channel` track holds `XKey {t, v, ease}` so every key tool (move, scale, ease, copy / paste,
  Graph Editor) works on it; `KeyButton.qml` beside a slider keys it (hollow / grey animated / amber key here).
* **World** (`Document.world`): the Studio's sun (auto / on / off, strength, direction, height, colour
  temperature), sky light, the map's lighting variant and strength, and the look (exposure EV, tone mapping,
  contrast, saturation, brightness, bloom, vignette, sharpen: ExtendedSceneEnvironment on both views). All are
  `world/<field>` channels.
* **Map lights**: the plugin tags every light with its lighting variant (RE4: one light scene per chapter / time
  of day, `_cp10_chp1_3`, switched on by the game); lumens (Unit 1 = candela x 4 pi), reach
  (`ReferenceEffectiveRange`), fill lights (`UseCustomAttenuation`: even to `AttenuationStartDistance`). Spots
  shine along local -Y. Qt draws at most 15 lights, so `StageScene::syncMapLights` keeps the most relevant near
  the camera (re-picked after a metre of travel); "fog lights" (reach > 150 m) are skipped.
* **The film** (shots as clips): `seq.shots` keep the film's order (`shot_move`); `film_play` runs a film clock over
  them (`filmAt`: film frame -> shot, scene frame), each shot's camera forced through `m_shotCam`; a render with
  `film: true` builds its frame list shot by shot with a camera per frame. Renders switch off the Work Camera and
  give the view back afterwards.
* **Relative edits** (`commitRelativeEdit`): with the Motion Editor and a time selection, K stores
  `under^-1 * edit` per joint in an additive pose clip on pose layer 1 (the "offsets" track) spanning the selection
  plus falloff (fade in / out); pose tracks are told apart by layer.
* **Render** (`FilmRender`): motion blur = N sub-frames per output frame over the open shutter (from the frame's
  time, shutter angle), averaged; H.264 MP4, ProRes 422 HQ MOV, PNG.
* **Clips from another game** (cross-rig `ClipBinding`): chosen when fewer than half the clip's tracks bind by
  hash and the owner's skeleton (`game::owner`, first look of that owner in its game) is known. The clip plays on
  its own skeleton with the top bones at rest; bones map through a canonical humanoid table (each game's names)
  plus exact names; each mapped bone takes the source bone's world rotation times a rest correction (source rest⁻¹
  x stance fix x target rest, the stance fix turning each target limb to where the source limb points); the hips
  follow the source hips scaled by hip height; travel comes through `rootAt`, scaled the same. Checked: RE2 Leon on
  RE4 `walk_F_loop`, RE4 Leon on RE2 `Damage_Knee_FaceDown` against the native RE2 pose (hips, spine, hands, head
  within 1 cm). Face bones only map by exact name.
* Film data added for the rig pass: `Actor.lookat`, `Actor.attached`, `Document.constraints`, `path` tracks
  (`Track.path`); all saved in the film and undoable.
* Without timeline clips a character plays its idle, then the clip Play started over it (`play` / `play_stop`);
  the Asset Browser's hover preview (`play_preview` / `play_preview_end`) plays over everything and is never part
  of the film. Removing an actor removes its tracks too (undoable).
* Materials (`qml/scene/re_standard.frag`): sRGB textures are sampled linear; characters: metal = 1 - ALBD.a,
  roughness = NRMR.a, ATOC r = alpha, b = occlusion; world: see Maps above; shading modes plain / layered /
  terrain / multiply (eyelid occlusion shells, `ReMultiply.qml`, DstColor blend; unshaded materials get no UV0 in
  the fragment shader, so `re_multiply.vert` hands it over); hair cards blend by their own
  alpha over a depth pre-pass with volume occlusion from ATOC blue on UV1; back faces light with flipped normals;
  eye covers (ReflectiveTransparent on a Null base) are hidden.
* The film's map is `Document.stageId` (saved in the film, undoable); `publish()` keeps the picture in step. The
  Work Camera settles on the surface nearest the named place's marker height once the map is in.
* Hover previews (`cast_preview` / `mesh_preview`) show a temporary actor (`kPreviewId`) turning in front of the
  camera; it is not part of the film and never exported.

## Studio (C++, `src/core`, `src/app`)
| File | Role |
|---|---|
| `Studio` | facade for QML: `state` / `data` as JS objects, `cmd()`, runtime mode, host services (files, render, import, sounds, popups), editor state shared with the painted views |
| `FilmRender` | standalone export: `renderView` (a second View3D importing the same scene at output size, SSAA) stepped frame by frame, grabbed, piped into ffmpeg (x264) or PNGs, guide audio muxed |
| `Bridge`, `GameWindow`, `RenderJob`, `AnimImporter` | live mode: file IPC, the docked game window, frame-stepped in-game render |
| `ControlServer` + `UiAutomation` | HTTP API for the MCP server; `/eval` runs JS in the QML engine; screenshots of the 3D view in standalone |
| `src/views/*` | Clip / Motion / Graph editors (painted) |
| `src/app/MainWindow` | docks, menus (Game menu: Games…, Open Game, Add a Game Folder, live mode), status bar, centre stack (3D viewport or game host), F1 hotkey only in live mode |

## QML
* `qml/scene/Viewport3D.qml` — the Primary Viewport: shared scene node, the viewport View3D (MSAA + TAA,
  filmic, AO, studio sky), the hidden export View3D, floor grid, overlays, SFM navigation (LMB look + WASD/QE
  fly, MMB pan, RMB / wheel dolly, Alt+LMB orbit, F frame, click pick).
* `qml/panels/GamesDialog.qml` — Game ▸ Games…
* `qml/Store.qml`, `qml/Ui.qml`, `qml/panels/*`, `qml/sections/*`, `qml/controls/*` as before. Menus and
  dropdowns are native `QMenu`s (`studio.popup([...])`).
* QML pitfalls met: a property named like the context property shadows it (`core: studio` in views); inline
  components cannot see the file's ids; boolean bindings must not produce `undefined` (`!!(...)`).

## Build, install, run
* Toolchain: Qt 6.10.3 MinGW (+ Qt Quick 3D, Quick Timeline) and MinGW 13.1 in `C:\Qt` (aqtinstall); CMake +
  Ninja from `tools/venv`. `bash studio/qt/build.sh` builds `studio/qt/build` (Studio, `reengine.dll`,
  `recli`, `dirview`); `run.sh` = rebuild + restart; `package.sh` installs the Release build, the game plugins
  and `libzstd.dll` with the Qt runtime into `RE4Director/app`.
* Start: `RigReelStudio.exe` from the packaged `app` directory. Standalone needs no game running. Game ▸ Direct the Running Game
  switches to live mode (and starts RE4 through Steam). The mode is remembered.
* Caches: converted textures and RSZ type tables under `%LOCALAPPDATA%/Director/Director Studio/cache`.
* Log: `studio/qt/studio.log`. Runtime log: the Console dock.

## Extending
1. A new op: implement it in `StageOps.cpp` (standalone) and, for live mode, `ops.my_op` in `bridge.lua`; add
   it to the read-only set if it does not change the film.
2. A new game family: a Qt plugin implementing `IGamePlugin` / `IGameSource` that fills the neutral assets;
   drop it in `plugins/games`.
3. If a new state list may be empty, add it to `Bridge::normalizeState`.

The retired React UI and Python host are in `archive/react-ui-and-python-host-2026-09-23.zip`.
