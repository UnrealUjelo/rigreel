-- Director :: one-button self-tests (log + on-screen results; no click sequences needed)
local Log = require("Director.log")
local E = require("Director.engine")
local Catalog = require("Director.catalog")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Seq = require("Director.sequence")
local Pose = require("Director.pose")
local Lookat = require("Director.lookat")

local T = { results = {}, running = nil }

local ASHLEY_GENERAL = "_chainsaw/animation/ch/cha1/motlist/cha1_general.motlist"
local CUTSCENE_ASHLEY = "_chainsaw/event/cs/csa039/csa039_s00/chara/cha100_00/cha100_00.motlist"

local function report(name, ok, detail)
    T.results[name] = { ok = ok, detail = detail, time = os.date("%H:%M:%S") }
    Log.info("SELFTEST %s: %s  %s", name, ok and "PASS" or "FAIL", detail or "")
end

-- simple coroutine-free step machine: steps = list of {delay_frames, fn}; fn may return false to abort
-- each step = { frames_to_wait_BEFORE_running, fn }
local function run_steps(name, steps)
    if T.running then Log.warn("a self-test is already running"); return end
    T.running = { name = name, steps = steps, i = 1, wait = nil }
end

E.on_frame(function()
    local r = T.running
    if not r then return end
    local step = r.steps[r.i]
    if not step then T.running = nil; return end
    if type(step[1]) == "function" then          -- condition step: wait until it returns true
        if not step[1]() then return end
    else
        if r.wait == nil then r.wait = step[1] or 0 end
        if r.wait > 0 then r.wait = r.wait - 1; return end
    end
    r.i = r.i + 1
    r.wait = nil
    local ok, res = pcall(step[2])
    if not ok then report(r.name, false, "error: " .. tostring(res)); T.running = nil
    elseif res == false then T.running = nil end
end)

local function find_mot(a, bank_id, needle)
    for _, m in ipairs(a:motions(bank_id)) do
        if tostring(m.name):find(needle, 1, true) then return m end
    end
    return nil
end

local function nearest_npc()
    for _, c in ipairs(Actor.scan(nil)) do
        if c.has_fsm and not c.is_player and c.name:match("^ch%d") then return Actor.wrap(c.go) end
    end
    return nil
end

-------------------------------------------------------------------------------
-- Test 1: pause/step/scrub on the nearest NPC (fully automatic)
-------------------------------------------------------------------------------
function T.test_pause()
    local a, bank, f0, f1
    run_steps("pause", {
        { 0, function()
            a = nearest_npc()
            if not a then report("pause", false, "no NPC with an FSM nearby (stand near Ashley)"); return false end
            bank = a:load_motlist(ASHLEY_GENERAL)
            Log.info("selftest pause: using %s", a.name)
        end },
        { 120, function()
            if not a:bank_alive(bank) then report("pause", false, "motlist did not load in 2s"); return false end
            local m = find_mot(a, bank, "0160_stand_loop")
            if not m then report("pause", false, "stand_loop not found in bank"); return false end
            a:play(bank, m.id, { layer = 0, blend = 5 })
        end },
        { 60, function() a:set_paused(true) end },
        { 5, function() f0 = a:layer_info(0).frame end },
        { 60, function()
            f1 = a:layer_info(0).frame
            if math.abs(f1 - f0) > 0.01 then report("pause", false, string.format("frame advanced while paused (%.2f -> %.2f)", f0, f1)); a:release(); return false end
            for _ = 1, 5 do a:set_frame(0, a:layer_info(0).frame + 1) end
        end },
        { 5, function()
            local f2 = a:layer_info(0).frame
            local stepped = math.abs((f2 - f1) - 5) < 0.6
            a:set_paused(false)
            f0 = f2
            if not stepped then report("pause", false, string.format("frame step wrong (%.2f -> %.2f)", f1, f2)); a:release(); return false end
        end },
        { 60, function()
            local f3 = a:layer_info(0).frame
            local resumed = math.abs(f3 - f0) > 5
            a:release()
            report("pause", resumed, resumed and "pause holds, stepping works, resume works" or string.format("did not resume (%.2f -> %.2f)", f0, f3))
        end },
    })
end

-------------------------------------------------------------------------------
-- Test 2: layering on Leon. Plays Ashley's gesture on layer 1 (no puppet), then samples
-- layer 0 for 8 seconds: close the REFramework menu and WALK during that time.
-------------------------------------------------------------------------------
function T.test_layering()
    local a, bank, samples = nil, nil, {}
    run_steps("layering", {
        { 0, function()
            a = Actor.ensure_player()
            if not a then report("layering", false, "player not found"); return false end
            a:release()
            bank = a:load_motlist(ASHLEY_GENERAL)
        end },
        { 120, function()
            if not a:bank_alive(bank) then report("layering", false, "motlist did not load in 2s"); return false end
            local m = find_mot(a, bank, "0090_hand_VerA")
            if not m then report("layering", false, "hand_VerA not found"); return false end
            a:play(bank, m.id, { layer = 1, blend = 10, puppet = false })
            T.walk_prompt_until = E.frame_count + 8 * 60
            Log.info("selftest layering: gesture on layer 1 - CLOSE THE MENU (Insert) AND WALK for 8 seconds")
        end },
        { 30, function() samples[#samples + 1] = { l0 = a:layer_info(0), l1 = a:layer_info(1) } end },
        { 120, function() samples[#samples + 1] = { l0 = a:layer_info(0), l1 = a:layer_info(1) } end },
        { 120, function() samples[#samples + 1] = { l0 = a:layer_info(0), l1 = a:layer_info(1) } end },
        { 120, function() samples[#samples + 1] = { l0 = a:layer_info(0), l1 = a:layer_info(1) } end },
        { 90, function()
            samples[#samples + 1] = { l0 = a:layer_info(0), l1 = a:layer_info(1) }
            T.walk_prompt_until = nil
            local l1_held, l0_changed = true, false
            local first_l0 = samples[1].l0.mot
            for _, s in ipairs(samples) do
                if s.l1.bank ~= bank then l1_held = false end
                if s.l0.mot ~= first_l0 then l0_changed = true end
            end
            local detail = string.format("layer1 kept the gesture: %s | layer0 (legs) changed motion while you moved: %s",
                l1_held and "yes" or "NO", l0_changed and "yes" or "no (did you walk?)")
            report("layering", l1_held, detail)
            a:release()
        end },
    })
end

-------------------------------------------------------------------------------
-- Test 3: cutscene clip on nearest NPC with root lock (automatic; watch her act in place)
-------------------------------------------------------------------------------
function T.test_cutscene()
    local a, bank, p0
    run_steps("cutscene", {
        { 0, function()
            a = nearest_npc()
            if not a then report("cutscene", false, "no NPC nearby"); return false end
            a:set_root_lock(true)
            p0 = E.v3(a.xform:call("get_Position"))
            bank = a:load_motlist(CUTSCENE_ASHLEY)
        end },
        { 120, function()
            if not a:bank_alive(bank) then report("cutscene", false, "cutscene motlist did not load"); return false end
            local mots = a:motions(bank)
            if #mots == 0 then report("cutscene", false, "no motions"); return false end
            a:play(bank, mots[1].id, { layer = 0, blend = 5 })
            Log.info("selftest cutscene: playing %s on %s (10s)", tostring(mots[1].name), a.name)
        end },
        { 600, function()
            local p1 = E.v3(a.xform:call("get_Position"))
            local drift = (p1 - p0):length()
            a:release()
            report("cutscene", drift < 0.05, string.format("root lock drift %.3f m over 10s (should be ~0)", drift))
        end },
    })
end

-------------------------------------------------------------------------------
-- Test 4: camera cut + orbit (automatic, 6 seconds of orbit around nearest NPC/player)
-------------------------------------------------------------------------------
function T.test_camera()
    local before, idx
    run_steps("camera", {
        { 0, function()
            local a = nearest_npc() or Actor.ensure_player()
            before = Cam.game_pose()
            idx = Cam.add_orbit("selftest orbit", a and a.addr or nil)
            Cam.cams[idx].radius = 1.8; Cam.cams[idx].height = 1.4; Cam.cams[idx].speed = 0.6
            Cam.set_active(idx)
        end },
        { 360, function()
            local now = Cam.game_pose()
            local moved = before and now and (Vector3f.new(now.pos[1], now.pos[2], now.pos[3]) - Vector3f.new(before.pos[1], before.pos[2], before.pos[3])):length() > 0.2
            Cam.stop()
            Cam.remove(idx)
            report("camera", moved, moved and "override moved the camera; orbit ran 6s; restored" or "camera did not move")
        end },
    })
end

-------------------------------------------------------------------------------
-- Test 5: sequencer. Builds a demo sequence (Ashley idle -> cutscene, Leon layer-1 gesture,
-- two camera cuts), plays it and checks the evaluator at several points. Leaves it loaded.
-------------------------------------------------------------------------------
function T.test_sequence()
    local ash, leon, bank_gen, bank_cs, bank_leon, cam1, cam2, ash_tr, chk = nil, nil, nil, nil, nil, nil, nil, nil, {}
    run_steps("sequence", {
        { 0, function()
            ash = nearest_npc(); leon = Actor.ensure_player()
            if not ash or not leon then report("sequence", false, "need Leon + a nearby NPC"); return false end
            Seq.release()
            bank_gen = ash:load_motlist(ASHLEY_GENERAL)
            bank_cs = ash:load_motlist(CUTSCENE_ASHLEY)
            bank_leon = leon:load_motlist(ASHLEY_GENERAL)
        end },
        { 150, function()
            if not (ash:bank_alive(bank_gen) and ash:bank_alive(bank_cs) and leon:bank_alive(bank_leon)) then
                report("sequence", false, "motlists did not load"); return false
            end
            local idle = find_mot(ash, bank_gen, "0160_stand_loop")
            local cs = ash:motions(bank_cs)[1]
            local gesture = find_mot(leon, bank_leon, "0090_hand_VerA")
            if not idle or not cs or not gesture then report("sequence", false, "motions missing"); return false end
            -- sequence
            local sq = Seq.new("demo")
            sq.length = 600; sq.loop = false
            ash_tr = Seq.add_anim_track(ash, 0)
            Seq.add_clip(ash_tr, { start = 0, dur = 180, path = ASHLEY_GENERAL, mot = idle.id, name = idle.name, endframe = idle.endframe, loop = true, blend = 10 })
            Seq.add_clip(ash_tr, { start = 180, dur = 420, path = CUTSCENE_ASHLEY, mot = cs.id, name = cs.name, endframe = cs.endframe, blend = 15 })
            local leon_tr = Seq.add_anim_track(leon, 1)
            Seq.add_clip(leon_tr, { start = 60, dur = 360, path = ASHLEY_GENERAL, mot = gesture.id, name = gesture.name, endframe = gesture.endframe, loop = true, blend = 10 })
            ash:set_root_lock(true)
            cam1 = Cam.capture("demo wide")
            cam2 = Cam.add_orbit("demo orbit", ash.addr); Cam.cams[cam2].radius = 1.8; Cam.cams[cam2].height = 1.5; Cam.cams[cam2].speed = 0.4
            local ctr = Seq.add_camera_track()
            Seq.add_cut(ctr, 0, cam1); Seq.add_cut(ctr, 240, cam2)
            sq.length = 600
            Seq.stop(); Seq.play()
            Log.info("selftest sequence: playing demo (10s)")
        end },
        { function() return Seq.t >= 100 end, function()
            local li = ash:layer_info(0)
            chk.idle = li and li.bank == bank_gen and math.abs((li.frame or 0) - Seq.t) < 12
            chk.idle_detail = string.format("t=%.0f ashley bank=%s frame=%.0f", Seq.t, tostring(li and li.bank), li and li.frame or -1)
            local ll = leon:layer_info(1)
            chk.leon = ll and ll.bank == bank_leon
        end },
        { function() return Seq.t >= 300 end, function()
            local li = ash:layer_info(0)
            chk.cs = li and li.bank == bank_cs and math.abs((li.frame or 0) - (Seq.t - 180)) < 12
            chk.cs_detail = string.format("t=%.0f ashley bank=%s frame=%.0f cam=%s", Seq.t, tostring(li and li.bank), li and li.frame or -1, tostring(Cam.active))
            chk.cam = (Cam.active == cam2)
            Seq.pause()
        end },
        { 60, function()
            local li = ash:layer_info(0)
            chk.pause_frame = li and li.frame
            Seq.seek(50)
        end },
        { 30, function()
            local li = ash:layer_info(0)
            chk.seek = li and li.bank == bank_gen and math.abs((li.frame or 0) - 50) < 3
            chk.seek_detail = string.format("after seek(50): bank=%s frame=%.1f", tostring(li and li.bank), li and li.frame or -1)
            local ok = chk.idle and chk.leon and chk.cs and chk.cam and chk.seek
            report("sequence", ok and true or false, string.format("idle:%s leon-layer1:%s cutscene:%s cam-cut:%s seek:%s | %s | %s | %s",
                tostring(chk.idle), tostring(chk.leon), tostring(chk.cs), tostring(chk.cam), tostring(chk.seek), chk.idle_detail or "", chk.cs_detail or "", chk.seek_detail or ""))
            Seq.stop()
            Log.info("selftest sequence: demo left on the timeline - press Play in the Timeline section")
        end },
    })
end

-------------------------------------------------------------------------------
-- Test 6: pose keyframes. Keys Leon's left upper arm from its animated rotation (frame 0) to +80deg
-- (frame 60), holds until 120, then releases. Checks the interpolated, held and released rotations.
-------------------------------------------------------------------------------
local function qdot(a, b) return math.abs(a:dot(b)) end

function T.test_pose()
    local a, base, target, mid, chk = nil, nil, nil, nil, {}
    run_steps("pose", {
        { 0, function()
            a = Actor.ensure_player()
            if not a then report("pose", false, "player not found"); return false end
            Seq.release(); a:release()
            base = Pose.read_local(a, "L_UpperArm")
            if not base then report("pose", false, "joint L_UpperArm not found"); return false end
            target = (base * E.quat_from_euler_deg(0, 0, 80)):normalized()
            mid = Pose.slerp(base, target, 0.5)
            local sq = Seq.new("pose test"); sq.length = 200
            local tr, clip = Seq.keyframe(a, 0, { L_UpperArm = base })
            Seq.keyframe(a, 60, { L_UpperArm = target })
            clip.dur = 120; clip.fade_in = 0; clip.fade_out = 0
            Seq.stop(); Seq.play()
            Log.info("selftest pose: arm keyframes playing (watch Leon's left arm swing up over 1s, hold 1s, then return)")
        end },
        { function() return Seq.t >= 30 end, function()
            local q = Pose.read_local(a, "L_UpperArm")
            chk.mid = q and qdot(q, mid) > 0.97
            chk.mid_detail = string.format("t=%.0f dot(mid)=%.3f", Seq.t, q and qdot(q, mid) or -1)
        end },
        { function() return Seq.t >= 70 end, function()
            local q = Pose.read_local(a, "L_UpperArm")
            chk.hold = q and qdot(q, target) > 0.995
            chk.hold_detail = string.format("t=%.0f dot(target)=%.3f", Seq.t, q and qdot(q, target) or -1)
        end },
        { function() return Seq.t >= 135 end, function()
            local q = Pose.read_local(a, "L_UpperArm")
            chk.released = q and qdot(q, target) < 0.97
            chk.rel_detail = string.format("t=%.0f dot(target)=%.3f (expect < 0.97)", Seq.t, q and qdot(q, target) or -1)
            local ok = chk.mid and chk.hold and chk.released
            report("pose", ok and true or false, string.format("interp:%s hold:%s release:%s | %s | %s | %s",
                tostring(chk.mid), tostring(chk.hold), tostring(chk.released), chk.mid_detail, chk.hold_detail, chk.rel_detail))
            Seq.release()
        end },
    })
end

-------------------------------------------------------------------------------
-- Test 7: placement (hold against AI), keyframed move (A -> B over 2s), look-at camera.
-------------------------------------------------------------------------------
function T.test_placement()
    local a, p0, pA, pB, head0, chk = nil, nil, nil, nil, nil, {}
    local function dist(x, y) return (x - y):length() end
    run_steps("placement", {
        { 0, function()
            a = nearest_npc()
            if not a then report("placement", false, "no NPC nearby"); return false end
            Seq.release(); a:release()
            p0 = E.v3(a.xform:call("get_Position"))
            pA = Vector3f.new(p0.x + 0.6, p0.y, p0.z)
            a:set_puppet(true)
            a:set_root_lock(true)
            a:set_root_pose(pA, nil)
            Log.info("selftest placement: moved %s 0.6m, holding for 1s", a.name)
        end },
        { 60, function()
            local p = E.v3(a.xform:call("get_Position"))
            chk.hold = dist(p, pA) < 0.05
            chk.hold_detail = string.format("hold drift %.3f m", dist(p, pA))
            -- keyframed move A -> B
            pB = Vector3f.new(pA.x, pA.y, pA.z + 1.0)
            local sq = Seq.new("move test"); sq.length = 200
            Seq.key_transform(a, 0, pA, a.xform:call("get_Rotation"))
            Seq.key_transform(a, 120, pB, a.xform:call("get_Rotation"))
            Seq.stop(); Seq.play()
        end },
        { function() return Seq.t >= 60 end, function()
            local p = E.v3(a.xform:call("get_Position"))
            local mid = Vector3f.new(pA.x, pA.y, pA.z + 0.5)
            chk.mid = dist(p, mid) < 0.12
            chk.mid_detail = string.format("t=%.0f mid error %.3f m", Seq.t, dist(p, mid))
        end },
        { function() return Seq.t >= 125 end, function()
            local p = E.v3(a.xform:call("get_Position"))
            chk.endp = dist(p, pB) < 0.05
            chk.end_detail = string.format("t=%.0f end error %.3f m", Seq.t, dist(p, pB))
            Seq.pause()
            -- look-at camera
            local hj = a:joint("Head")
            head0 = hj and hj:call("get_Rotation")
            local la = Lookat.setup(a)
            la.enabled = true; la.kind = "camera"; la.weight = 1; la.smooth = 0.3
            Log.info("selftest placement: look-at camera enabled on %s (watch her head)", a.name)
        end },
        { 90, function()
            local hj = a:joint("Head")
            local h1 = hj and hj:call("get_Rotation")
            local d = (head0 and h1) and math.abs(head0:dot(h1)) or 1
            chk.look = d < 0.999
            chk.look_detail = string.format("head rotation change dot=%.4f (<0.999 = turned)", d)
            a.lookat.enabled = false
            Seq.release()
            a:set_root_lock(true); a:set_root_pose(p0, nil)   -- put her back
            E.defer(function() a:release() end, 30)
            local ok = chk.hold and chk.mid and chk.endp and chk.look
            report("placement", ok and true or false, string.format("hold:%s move-mid:%s move-end:%s lookat:%s | %s | %s | %s | %s",
                tostring(chk.hold), tostring(chk.mid), tostring(chk.endp), tostring(chk.look), chk.hold_detail, chk.mid_detail, chk.end_detail, chk.look_detail))
        end },
    })
end

-- big on-screen prompt (visible even with the REFramework menu closed)
E.on_frame(function()
    if T.walk_prompt_until and E.frame_count < T.walk_prompt_until then
        local left = math.ceil((T.walk_prompt_until - E.frame_count) / 60)
        draw.text(string.format("DIRECTOR SELF-TEST: close the menu (Insert) and WALK with WASD  (%ds)", left), 40, 60, 0xFF00FFFF)
    end
end)

function T.release_all()
    for _, a in ipairs(Actor.all()) do a:release() end
    Cam.stop()
    E.set_player_frozen(false)
    Log.info("released all actors, camera override off")
end

function T.draw()
    imgui.text("Self-tests (one click each). Stand near Ashley for the NPC tests.")
    local busy = T.running ~= nil
    if busy then imgui.text(string.format("running: %s ...", T.running.name)) end
    if imgui.button("1  Pause / step / resume (auto, 4s)") and not busy then T.test_pause() end
    imgui.same_line()
    if imgui.button("2  Layering: gesture on layer 1 while you WALK (8s)") and not busy then T.test_layering() end
    if imgui.button("3  Cutscene clip + root lock (auto, 12s)") and not busy then T.test_cutscene() end
    imgui.same_line()
    if imgui.button("4  Camera orbit override (auto, 6s)") and not busy then T.test_camera() end
    imgui.same_line()
    if imgui.button("5  Build + play demo SEQUENCE (auto, 12s)") and not busy then T.test_sequence() end
    imgui.same_line()
    if imgui.button("6  Pose keyframes on Leon's arm (auto, 3s)") and not busy then T.test_pose() end
    imgui.same_line()
    if imgui.button("7  Placement + move keys + look-at (auto, 6s)") and not busy then T.test_placement() end
    imgui.same_line()
    if imgui.button("Release all") then Seq.release(); T.release_all() end
    if T.walk_prompt_until and E.frame_count < T.walk_prompt_until then
        imgui.text_colored(">>> CLOSE THE MENU (Insert) AND WALK WITH WASD NOW <<<", 0xFF00FFFF)
    end
    for _, name in ipairs({ "pause", "layering", "cutscene", "camera", "sequence", "pose", "placement" }) do
        local r = T.results[name]
        if r then
            imgui.text_colored(string.format("%s %-9s %s  (%s)", r.ok and "PASS" or "FAIL", name, r.detail or "", r.time), r.ok and 0xFF00FF00 or 0xFF0000FF)
        end
    end
end

return T
