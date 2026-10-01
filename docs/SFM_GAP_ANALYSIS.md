# What stands between RigReel Studio and Source Filmmaker (2026-09-23)

Reviewed as a whole: the Qt Studio, the Lua runtime and the render pipeline, against how SFM is built and used.
Each gap says what SFM does, what we have, and whether RE4 lets us close it.

## Update 2026-09-24: the Studio renderer (standalone)
The Studio now owns the film and draws it itself (P0-1 below is solved for standalone). What it still lacks:
* **Now in the Studio renderer too** (2026-09-24, later): props in a hand, Look at, walk paths, constraints,
  Drive + record, Place, clip speed / frame / pause, animation import / export, a move / rotate / scale
  manipulator in the picture. **Still game-only**: hair / strap physics and damage skin, ragdoll, sounds (the
  game's audio banks are not read yet).
* **Resident Evil 2** (second game): characters (survivor costumes, zombies assembled from their clothes,
  creatures), animations (clips adapt across characters), props, and 164 rooms as maps. A whole place at once
  (the police station) needs ~14 GB, so only rooms are listed; RE2's map lights are not read.
* **Picture**: the maps' own lights (about a hundred per area) and sky / time of day, foliage (.fol), water,
  fog and the game's post-processing, particles; facial blend shapes and wrinkle maps (facial clips carry them;
  bones play, shapes do not); hair and cloth physics.
* **Film tools** (still open from the list below): animate any property (P0-2), shots as clips (P0-3), relative
  edits and IK rigs (P1-4, P1-5), Graph Editor depth (P1-6), motion blur and output formats (P1-9, P1-11).
* **Games**: Resident Evil 4 and Resident Evil 2 are tested; other RE Engine games need their profiles checked.

## Update 2026-09-24 (night): several games, channels, picture
* **Several games in one film**: the Asset Browser's *From* picker lists any installed game; characters, clips,
  props and maps from different games share a film; clips retarget across games (RE2's Leon plays RE4 clips and
  the reverse, checked against the native pose within 1 cm).
* **P0-2 done (standalone)**: channel tracks for lights, camera lens / aperture / focus, look-at weight, size,
  visibility, constraint weights, sun, sky, map lights and the look; key diamonds beside the sliders; curves in the
  Graph Editor, keys in the Motion Editor.
* **P1-9 / P1-11 done (standalone)**: motion blur (8-32 samples, shutter angle), ProRes 422 HQ; the Render button
  works without the game.
* **P0-1 (standalone)**: autosave every minute and crash recovery.
* **P1-10 done (standalone)**: exposure, tone mapping, contrast, saturation, brightness, bloom, vignette, sharpen.
* **Picture**: the maps' own lights per chapter lighting (RE4), sun & sky controls, hair / cloth / strap secondary
  motion.
* **P0-3 done (standalone)**: shots keep the film's order (reorder in Output > Shots); *Play the film* plays each
  shot's own stretch of the scene through its own camera, so one performance can be covered from several cameras;
  the render's *The film* range assembles the shots in order. Renders never use the Work Camera.
* **P1-4 done (standalone)**: Motion Editor edits are offsets by default (the Offset / Hold chip): K adds the change
  to the motion across the time selection as an additive clip on the character's offset layer, fading over the
  falloff.
* **Still open**: procedural presets (P1-7), keyed IK rigs (P1-5),
  Graph Editor tangents (P1-6), facial blend shapes, sounds from the game's banks, ragdoll, RE4's sky / fog / the
  game's own post-processing zones, foliage and water.

## Done in this pass (SFM alignment)
* **Layout and manners of SFM**: dockable panels (Animation Set Editor · Element Viewer · Asset Browser ·
  Properties · Timeline · Console), menus File / Edit / Scene / View / Windows / Help, timecode header above the
  picture (movie time left, shot / camera centre, shot time right), manipulators + transport + viewport camera
  under it, status bar.
* **Work Camera vs Scene Camera**: a free camera that is never rendered, keyed or saved; the Scene Camera follows
  the film (cuts, else each shot's camera), also while playing. `C` turns the view into a scene camera.
* **Three editors over one ruler**: Clip Editor (F2: film strip of shots, cuts, clips, audio), Motion Editor
  (F3: keys, time selection with falloff), Graph Editor (F4: curves), Tab toggles.
* **Time selection with falloff**: an edit holds from In to Out and blends back into the motion over the falloff.
* **Animation Set Editor**: sets → rootTransform, look-at, constraints, pose presets, bones grouped by body part;
  the selected control's sliders below.
* **Element Viewer**: the raw data of any element, editable where the runtime has an editor.
* **SFM keymap**: Q W E R, Space, ← → ↑ ↓, I / O, K, M, C, F2–F4, Tab, Ctrl+B, [ ] …

## The gaps, by priority

### P0 — structural
1. **The document lives in the game, not in the editor.** SFM's session (DME) is the editor's; ours is the Lua
   runtime's memory. If the game crashes, unsaved work is gone; nothing can be edited while the game loads; undo
   is a game-side snapshot. *Fix*: make the Studio own the film (sequence, cameras, lights, shots, constraints,
   poses) as the source of truth, autosave it, and stream it (or diffs) to the runtime, which only evaluates.
   This also unlocks editor-side undo, project versioning and crash recovery. Feasible, large.
2. **Not everything is animatable.** In SFM every value is a channel with a curve. We key positions, rotations,
   poses, camera FOV and roll only. Lights (intensity, colour, temperature, cone), depth of field (aperture,
   focus), look-at / constraint / pose-layer weights, visibility and sound volume cannot change over time.
   *Fix*: a generic channel track (`{target, field, keys}`) evaluated by the runtime for any settable property;
   the Graph Editor already draws generic channels. Feasible, medium — the most-felt gap for film work.
3. **Shots share one timeline.** In SFM each shot is its own clip with its own time, so shots can be reordered,
   slipped and trimmed without touching animation. Ours are named ranges on one global timeline. *Fix*: shot =
   {source range, film position, camera}; playback maps film time → shot time; the render assembles shots in
   film order. Feasible, medium-large.

### P1 — animation quality
4. **Edits are absolute, SFM's are relative.** SFM's motion editor offsets the existing motion inside the time
   selection (with falloff shapes: linear, ease, spline). Ours key absolute rotations (the motion underneath is
   replaced on edited bones). *Fix*: additive pose layers + per-sample offsets; falloff interpolation choices.
5. **Rigs.** SFM's `rig_biped` gives animatable IK hand / foot targets, pole vectors, FK/IK switch. Ours: a
   two-bone IK tool in edit mode and IK constraints. *Fix*: persistent IK target controls keyed over time.
6. **Graph Editor depth.** No Bezier tangents / handles, no rotation curves per bone, one track at a time, no
   key reduction / smoothing. *Fix*: tangent data in keys (runtime interpolation must follow), euler channel
   views of pose keys, multi-track display.
7. **Procedural presets** (SFM: zero, default, smooth, jitter, stagger, "head" / "in" / "out" over the time
   selection). Needs sample-level editing of pose clips.
8. **Faces.** RE4's faces are bone-driven with facial animation clips; there are no flex sliders. *Fix*: a face
   rig of grouped face bones + an expression library captured from the facial motlists; lip-sync from dialogue
   audio (jaw / lip bones from the amplitude or phonemes). Feasible, medium.

### P1 — picture and output
9. **Motion blur and depth-of-field samples at render** (SFM's progressive refinement). The render clock already
   steps sub-frames: accumulate N sub-frames per output frame (shutter angle setting). Feasible, small-medium,
   big visual gain.
10. **Post-processing controls**: exposure, colour grading / LUT, bloom, vignette, grain — the RE engine has these
    components; expose and key them. Research needed per component.
11. **Output options**: ProRes / EXR / PNG-16 via ffmpeg, a render queue (all shots, overnight), guide audio and
    game audio mixed together (today it is one or the other). Small.

### P2 — assets and world
12. **Thumbnails in the Asset Browser** for props (pipeline exists: 517 / 5,267 captured), characters and
    animations. Small once captured.
13. **Sound**: many tracks and clips with volume envelopes and fades; any Wwise event playable (needs bank
    loading — unsolved). Multi-track audio is feasible; arbitrary events need research.
14. **Particles / VFX, weather, sky and time of day**: not exposed yet; research in the engine's effect and
    light-scene systems.
15. **Stages**: Island areas have no collision when reached by travel; lighting cannot change region without a
    save (a prefecture swap crashes the game).
16. **Ragdoll / physics authoring** does not work (the game re-drives the body; spawned cast has no ragdoll).

### Limits that come from running inside a game
* **No live secondary viewport**: the game renders one camera. A still "scene camera" preview (grab through the
  scene camera on demand) is the realistic substitute.
* **Manipulator handles** are drawn by REFramework in the game (Insert cursor) or by edit mode; they cannot be
  drawn by the Studio over the picture (the game is a separate window above it).

## Suggested order
1. Channel tracks for any property (P0-2) — immediate value, contained change.
2. Motion-blur / DoF accumulation + output options (P1-9, P1-11) — cheap, very visible.
3. Editor-owned document with autosave (P0-1) — the foundation for everything after.
4. Shots as clips (P0-3), then relative edits + rigs (P1-4, P1-5), then the Graph Editor depth (P1-6).
