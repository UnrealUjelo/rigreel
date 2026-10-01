-- Director :: virtual cameras + primary camera override (applied at pre-LockScene)
local Log = require("Director.log")
local E = require("Director.engine")

local Cam = {
    cams = {},        -- list of camera tables
    active = nil,     -- index into cams
    enabled = false,  -- master switch for the override
    saved_fov = nil,
    t = 0,
    -- the Work Camera (SFM): a free camera for looking around that is never part of the film. It lives at
    -- cams[0], which ipairs / # skip, so it never shows up in lists, cuts, shots, renders or saved projects.
    work = nil,
    work_on = false,
}

-- camera table:
-- { name=, mode="static"|"orbit"|"lookat", pos={x,y,z}, rot={w,x,y,z}, fov=,
--   target=actor_addr|nil, height=, radius=, speed=, angle=, offset={x,y,z} }
local function new_cam(name)
    return {
        name = name or ("Cam " .. tostring(#Cam.cams + 1)),
        mode = "static",
        pos = { 0, 0, 0 }, rot = { 1, 0, 0, 0 }, fov = 50.0,
        target = nil, height = 1.5, radius = 2.0, speed = 0.2, angle = 0.0, offset = { 0, 1.4, 0 },
        dof = true,
        roll = 0,            -- degrees, Dutch tilt (keyed on camera paths)
        shake = nil,         -- { amp = metres, rot = degrees, freq = Hz } handheld noise
    }
end

-------------------------------------------------------------------------------
-- roll + handheld shake (applied after evaluate, never baked into keys) · framing overlay
-------------------------------------------------------------------------------
Cam.time_source = nil -- function() -> seconds (the sequencer installs its clock so renders are repeatable)
Cam.overlay = { letterbox = 0, thirds = false, safe = false, center = false } -- letterbox = 0 | 1.85 | 2.0 | 2.39

local function noise1(t, seed)
    return math.sin(t + seed) * 0.5 + math.sin(t * 2.3 + seed * 1.7) * 0.3 + math.sin(t * 4.1 + seed * 2.9) * 0.2
end

function Cam.apply_extras(c, pos, rot)
    local roll = c.roll or 0
    local s = c.shake
    if s and ((s.amp or 0) > 0 or (s.rot or 0) > 0) then
        local time = Cam.time_source and Cam.time_source() or os.clock()
        local f = (s.freq or 1.5) * 2 * math.pi
        local amp, rd = s.amp or 0, math.rad(s.rot or 0)
        local fwd, right, up = E.basis(rot)
        local dx, dy, dz = noise1(time * f, 1) * amp, noise1(time * f * 1.13, 7) * amp, noise1(time * f * 0.7, 13) * amp * 0.4
        pos = Vector3f.new(pos.x + right.x * dx + up.x * dy + fwd.x * dz, pos.y + right.y * dx + up.y * dy + fwd.y * dz, pos.z + right.z * dx + up.z * dy + fwd.z * dz)
        local ry, rp = noise1(time * f * 0.9, 21) * rd, noise1(time * f * 1.07, 33) * rd
        roll = roll + math.deg(noise1(time * f * 0.8, 45) * rd * 0.5)
        rot = (rot * E.quat_from_euler_deg(math.deg(rp), math.deg(ry), 0)):normalized()
    end
    if roll ~= 0 then
        local h = math.rad(roll) * 0.5
        rot = (rot * E.quat(math.cos(h), 0, 0, math.sin(h))):normalized()
    end
    return pos, rot
end

-- letterbox bars are always drawn (they are part of the picture); framing guides only while the editor overlays are on
function Cam.draw_overlay()
    local o = Cam.overlay
    if not o then return end
    local ok, w, h = pcall(function() local d = imgui.get_display_size(); return d.x, d.y end)
    if not ok or not w or w < 1 then return end
    if o.letterbox and o.letterbox > 0 then
        local th = w / o.letterbox
        local bar = (h - th) / 2
        if bar > 0 then
            draw.filled_rect(0, 0, w, bar, 0xFF000000)
            draw.filled_rect(0, h - bar, w, bar, 0xFF000000)
        end
    end
    local Gizmo = package.loaded["Director.gizmo"]
    if not (Gizmo and Gizmo.enabled and Cam.enabled) then return end
    local top, bot = 0, h
    if o.letterbox and o.letterbox > 0 then local bar = math.max(0, (h - w / o.letterbox) / 2); top, bot = bar, h - bar end
    local col = 0x66FFFFFF
    if o.thirds then
        for i = 1, 2 do
            draw.line(w * i / 3, top, w * i / 3, bot, col)
            draw.line(0, top + (bot - top) * i / 3, w, top + (bot - top) * i / 3, col)
        end
    end
    if o.center then draw.line(w / 2 - 12, h / 2, w / 2 + 12, h / 2, col); draw.line(w / 2, h / 2 - 12, w / 2, h / 2 + 12, col) end
    if o.safe then
        local function box(f, c2) local mw, mh = w * (1 - f) / 2, (bot - top) * (1 - f) / 2; draw.outline_rect(mw, top + mh, w - 2 * mw, (bot - top) - 2 * mh, c2) end
        box(0.9, 0x55FFFFFF); box(0.8, 0x33FFFFFF)
    end
end
E.on_frame(function() pcall(Cam.draw_overlay) end)

function Cam.game_pose()
    local cam = E.primary_camera()
    if not cam then return nil end
    local go = cam:call("get_GameObject")
    local xf = E.transform(go)
    local p = xf:call("get_Position")
    local r = xf:call("get_Rotation")
    return { pos = { p.x, p.y, p.z }, rot = { r.w, r.x, r.y, r.z }, fov = cam:call("get_FOV") }
end

-- add a static camera copying the current game view
function Cam.capture(name)
    local pose = Cam.game_pose()
    if not pose then Log.err("no primary camera"); return nil end
    local c = new_cam(name)
    c.pos, c.rot, c.fov = pose.pos, pose.rot, pose.fov
    Cam.cams[#Cam.cams + 1] = c
    Log.info("camera captured: %s (fov %.1f)", c.name, c.fov)
    return #Cam.cams
end

function Cam.add_orbit(name, actor_addr)
    local c = new_cam(name)
    c.mode = "orbit"; c.target = actor_addr
    Cam.cams[#Cam.cams + 1] = c
    return #Cam.cams
end

function Cam.add_lookat(name, actor_addr)
    local pose = Cam.game_pose()
    local c = new_cam(name)
    c.mode = "lookat"; c.target = actor_addr
    if pose then c.pos = pose.pos; c.fov = pose.fov end
    Cam.cams[#Cam.cams + 1] = c
    return #Cam.cams
end

function Cam.remove(idx)
    if Cam.active == idx and Cam.fly.on then Cam.set_fly(false) end
    table.remove(Cam.cams, idx)
    if Cam.active == idx then Cam.active = nil elseif Cam.active and Cam.active > idx then Cam.active = Cam.active - 1 end
    if not Cam.active then Cam.enabled = false end
    local ok, Seq = pcall(require, "Director.sequence")
    if ok and Seq and Seq.on_camera_removed then Seq.on_camera_removed(idx) end
end

function Cam.set_active(idx)
    if idx ~= 0 then Cam.work_on = false end   -- looking through a scene camera leaves the work camera
    if idx and Cam.cams[idx] then Cam.active = idx; Cam.enabled = true; Cam.cams[idx].auto = nil
    else Cam.active = nil end
    if not Cam.active then Cam.enabled = false end
end

-- the Work Camera on / off. On: it starts where the view is now (the first time), or where you left it.
function Cam.set_work(on)
    if on then
        if not Cam.work then
            local pose = Cam.game_pose()
            local c = new_cam("Work Camera")
            c.dof = false
            if pose then c.pos, c.rot, c.fov = pose.pos, pose.rot, pose.fov end
            Cam.work = c
        end
        Cam.cams[0] = Cam.work
        Cam.set_active(0)
        Cam.work_on = true
    else
        Cam.work_on = false
        if Cam.active == 0 then Cam.stop() end
    end
    return Cam.work_on
end

-- put the work camera where the view is (e.g. after looking through a scene camera)
function Cam.work_from_view()
    local pose = Cam.game_pose()
    if Cam.work and pose then Cam.work.pos, Cam.work.rot, Cam.work.fov = pose.pos, pose.rot, pose.fov end
end

function Cam.stop()
    if Cam.fly.on then Cam.set_fly(false) end
    Cam.enabled = false
    Cam.active = nil
    Cam.work_on = false
    if Cam.dof_state == false then
        if Cam.dof_comp then pcall(function() Cam.dof_comp:call("set_Enabled", true) end) end
        if Cam.dof_blender then pcall(function() Cam.dof_blender:call("set_Enabled", true) end) end
    end
    Cam.dof_state = nil
end

-- compute the world pose for a camera this frame; returns pos(Vector3f), rot(Quaternion), fov
local Actor  -- late require to avoid cycles
local function actor_anchor(addr)
    Actor = Actor or require("Director.actor")
    local a = addr and Actor.get(addr)
    if not a or not a:valid() then return nil end
    return E.v3(a.xform:call("get_Position"))
end

-- Change subjects without snapping the aim point. The blend is evaluated by the
-- camera clock, so look-at, follow and orbit cameras all use the same transition.
function Cam.set_target(c, addr, duration)
    if not c then return end
    local old = actor_anchor(c.target)
    c.target = addr
    local new = actor_anchor(addr)
    if old and new and (duration or 0) > 0 then c._target_blend = { from = old, t = 0, dur = duration } else c._target_blend = nil end
end

function Cam.evaluate(c, dt)
    if c.mode == "static" then
        return Vector3f.new(c.pos[1], c.pos[2], c.pos[3]), E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4]), c.fov
    end
    local anchor = actor_anchor(c.target)
    if not anchor then
        return Vector3f.new(c.pos[1], c.pos[2], c.pos[3]), E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4]), c.fov
    end
    if c._target_blend then
        local b = c._target_blend
        b.t = math.min(b.dur, b.t + math.max(0, dt or 0))
        local u = b.dur > 0 and b.t / b.dur or 1; u = u * u * (3 - 2 * u)
        anchor = Vector3f.new(b.from.x + (anchor.x - b.from.x) * u, b.from.y + (anchor.y - b.from.y) * u, b.from.z + (anchor.z - b.from.z) * u)
        if b.t >= b.dur then c._target_blend = nil end
    end
    local look = Vector3f.new(anchor.x + c.offset[1], anchor.y + c.offset[2], anchor.z + c.offset[3])
    if c.mode == "orbit" then
        c.angle = c.angle + (c.speed or 0) * dt
        local eye = Vector3f.new(anchor.x + math.cos(c.angle) * c.radius, anchor.y + c.height, anchor.z + math.sin(c.angle) * c.radius)
        return eye, E.look_rotation(eye, look), c.fov
    elseif c.mode == "lookat" then
        local eye = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
        return eye, E.look_rotation(eye, look), c.fov
    elseif c.mode == "follow" then
        -- eye = character + offset in the character's own frame (side, up, back), smoothed; always looking at `look`
        local a = Actor.get(c.target)
        local yaw = 0
        if a then local r = a.xform:call("get_Rotation"); local _, y = E.euler_deg(r); yaw = math.rad(y) end
        local f = c.follow or { 0, 1.6, -2.5 }
        local sx, cy = math.sin(yaw), math.cos(yaw)
        -- character forward is +Z in its frame: back = -Z
        local ox, oz = f[1] * cy + f[3] * sx, -f[1] * sx + f[3] * cy
        local want = Vector3f.new(anchor.x + ox, anchor.y + f[2], anchor.z + oz)
        local sm = c.smooth or 0.15
        if c._eye and sm > 0 then
            local k = 1 - sm
            c._eye = Vector3f.new(c._eye.x + (want.x - c._eye.x) * k, c._eye.y + (want.y - c._eye.y) * k, c._eye.z + (want.z - c._eye.z) * k)
        else c._eye = want end
        return c._eye, E.look_rotation(c._eye, look), c.fov
    end
end

function Cam.add_follow(name, actor_addr)
    local c = new_cam(name)
    c.mode = "follow"; c.target = actor_addr; c.follow = { 0.9, 1.5, -2.2 }; c.offset = { 0, 1.35, 0 }; c.smooth = 0.12; c.fov = 45
    Cam.cams[#Cam.cams + 1] = c
    return #Cam.cams
end

-- Beginner presets: place a complete camera relative to the selected actor,
-- aim it, enable target-focus DoF, and return its index ready for live viewing.
function Cam.add_preset(kind, actor_addr, name)
    Actor = Actor or require("Director.actor")
    local a = actor_addr and Actor.get(actor_addr)
    if not a or not a:valid() then return nil end
    if kind == "track" then
        local i = Cam.add_follow(name or "Tracking", actor_addr)
        local c = Cam.cams[i]
        c.follow = { 0.85, 1.55, -2.6 }; c.offset = { 0, 1.35, 0 }
        c.smooth = 0.1; c.fov = 40; c.dof = true; c.dof_f = 2.8; c.dof_focus = "target"
        return i
    end
    local specs = {
        close =  { side = 0.28, up = 1.65, front = 1.45, look = 1.48, fov = 30, aperture = 1.8, label = "Close-up" },
        medium = { side = 0.65, up = 1.55, front = 2.8,  look = 1.25, fov = 40, aperture = 2.8, label = "Medium" },
        hero =   { side = 0.35, up = 0.65, front = 2.55, look = 1.45, fov = 34, aperture = 2.8, label = "Low hero" },
        wide =   { side = 1.15, up = 2.2,  front = 6.4,  look = 1.15, fov = 48, aperture = 5.6, label = "Wide" },
    }
    local s = specs[kind]
    if not s then return nil end
    local anchor = E.v3(a.xform:call("get_Position"))
    local _, yaw_deg = E.euler_deg(a.xform:call("get_Rotation"))
    local yaw = math.rad(yaw_deg)
    local sy, cy = math.sin(yaw), math.cos(yaw)
    local eye = Vector3f.new(anchor.x + s.side * cy + s.front * sy, anchor.y + s.up, anchor.z - s.side * sy + s.front * cy)
    local target = Vector3f.new(anchor.x, anchor.y + s.look, anchor.z)
    local rot = E.look_rotation(eye, target)
    local c = new_cam(name or s.label)
    c.mode = "static"; c.target = actor_addr
    c.pos = { eye.x, eye.y, eye.z }; c.rot = { rot.w, rot.x, rot.y, rot.z }; c.fov = s.fov
    c.offset = { 0, s.look, 0 }; c.dof = true; c.dof_f = s.aperture; c.dof_focus = "target"
    Cam.cams[#Cam.cams + 1] = c
    return #Cam.cams
end


-------------------------------------------------------------------------------
-- fly mode: WASD / Q E / mouse look / Shift fast / Ctrl slow / wheel = speed, edits the live camera in place
-------------------------------------------------------------------------------
Cam.fly = { on = false, speed = 2.5, sens = 0.12, yaw = 0, pitch = 0 }
local VK = { W = 0x57, A = 0x41, S = 0x53, D = 0x44, Q = 0x51, E = 0x45, SHIFT = 0x10, CTRL = 0x11, UP = 0x26, DOWN = 0x28, LEFT = 0x25, RIGHT = 0x27,
    LBRACKET = 0xDB, RBRACKET = 0xDD, NUM_ADD = 0x6B, NUM_SUB = 0x6D }

local function fly_forward()
    local cp, sp = math.cos(Cam.fly.pitch), math.sin(Cam.fly.pitch)
    return Vector3f.new(math.sin(Cam.fly.yaw) * cp, sp, math.cos(Cam.fly.yaw) * cp)
end

function Cam.set_fly(on, opts)
    opts = opts or {}
    if opts.speed then Cam.fly.speed = math.max(0.05, math.min(50, opts.speed)) end
    if opts.sens then Cam.fly.sens = math.max(0.01, math.min(1, opts.sens)) end
    on = on and true or false
    if on == Cam.fly.on then return Cam.fly.on end
    if on then
        local c = Cam.active and Cam.cams[Cam.active]
        if not c then Log.warn("fly: no live camera"); return false end
        if c.mode == "orbit" then c.mode = "static" end
        -- start from the camera's current pose
        local pos, rot = Cam.evaluate(c, 0)
        c.pos = { pos.x, pos.y, pos.z }; c.rot = { rot.w, rot.x, rot.y, rot.z }
        local f = E.basis(rot)
        Cam.fly.pitch = math.asin(math.max(-1, math.min(1, f.y)))
        Cam.fly.yaw = math.atan(f.x, f.z)
        Cam.fly.prev_player_frozen = E.player_frozen
        E.set_player_frozen(true) -- WASD must not walk the player
        E.input_blocked = true
        -- and the mouse must not turn her: freeze her animation too, unless the timeline is driving her
        local Actor = require("Director.actor")
        local pb = E.player_body()
        local pa = pb and Actor.wrap(pb)
        if pa and not pa.paused and not pa.puppet and not pa:has_pose_work() then pa:set_paused(true); Cam.fly.statue = pa end
        E.mouse_delta()           -- flush the accumulated delta
        require("Director.history").mark_break("fly")
        Cam.fly.on = true
        Log.info("fly on: %s", c.name)
    else
        Cam.fly.on = false
        E.input_blocked = false
        if Cam.fly.statue then pcall(function() Cam.fly.statue:set_paused(false) end); Cam.fly.statue = nil end
        if not Cam.fly.prev_player_frozen then E.set_player_frozen(false) end
        Log.info("fly off")
    end
    return Cam.fly.on
end

Cam.on_key_camera = nil -- set by the bridge: function(cam_idx)
local function fly_update(c, dt)
    if not E.game_focused then E.mouse_delta(); return end -- keys typed into the Studio must not fly the camera
    local mx, my, wheel = E.mouse_delta()
    local sens = Cam.fly.sens * math.pi / 180
    -- arrow keys look too (fallback when the mouse device is unavailable)
    local kx = (E.key_down(VK.RIGHT) and 1 or 0) - (E.key_down(VK.LEFT) and 1 or 0)
    local ky = (E.key_down(VK.DOWN) and 1 or 0) - (E.key_down(VK.UP) and 1 or 0)
    Cam.fly.yaw = Cam.fly.yaw - mx * sens - kx * dt * 1.5
    Cam.fly.pitch = math.max(-1.55, math.min(1.55, Cam.fly.pitch - my * sens - ky * dt * 1.5))
    if wheel ~= 0 then
        local ticks = wheel > 0 and 1 or -1
        Cam.fly.speed = math.max(0.05, math.min(50, Cam.fly.speed * (1.25 ^ ticks)))
    end
    if E.key_down(0x5A) then c.roll = (c.roll or 0) - 40 * dt end   -- Z: roll left
    if E.key_down(0x43) then c.roll = (c.roll or 0) + 40 * dt end   -- C: roll right
    if E.key_pressed(0x58) then c.roll = 0 end                       -- X: level
    if E.key_pressed(VK.RBRACKET) or E.key_pressed(VK.NUM_ADD) then Cam.fly.speed = math.min(50, Cam.fly.speed * 1.5) end
    if E.key_pressed(VK.LBRACKET) or E.key_pressed(VK.NUM_SUB) then Cam.fly.speed = math.max(0.05, Cam.fly.speed / 1.5) end
    local f = fly_forward()
    local up = Vector3f.new(0, 1, 0)
    local right = f:cross(up):normalized()
    local v = Cam.fly.speed * dt
    if E.key_down(VK.SHIFT) then v = v * 4 end
    if E.key_down(VK.CTRL) then v = v * 0.25 end
    local d = Vector3f.new(0, 0, 0)
    if E.key_down(VK.W) then d = d + f end
    if E.key_down(VK.S) then d = d - f end
    if E.key_down(VK.D) then d = d + right end
    if E.key_down(VK.A) then d = d - right end
    if E.key_down(VK.E) then d = d + up end
    if E.key_down(VK.Q) then d = d - up end
    c.pos = { c.pos[1] + d.x * v, c.pos[2] + d.y * v, c.pos[3] + d.z * v }
    c.auto = nil
    if c.mode ~= "lookat" then
        local eye = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
        local q = E.look_rotation(eye, eye + f)
        c.rot = { q.w, q.x, q.y, q.z }
    end
end

local last_clock = os.clock()
E.pre_lockscene(function()
    local now = os.clock()
    local dt = math.min(now - last_clock, 0.1)
    last_clock = now
    if not Cam.enabled or not Cam.active then return end
    if Cam.active == 0 and Cam.work then Cam.cams[0] = Cam.work end   -- undo restores copies; the work camera is not film
    local c = Cam.cams[Cam.active]
    if not c then Cam.stop(); return end
    local cam = E.primary_camera(); if not cam then return end
    if E.game_focused and E.key_pressed(0x72) and Cam.on_key_camera then Cam.on_key_camera(Cam.active) end -- F3 = key the live camera (M opens the game map)
    if Cam.fly.on then Log.try("fly", fly_update, c, dt) end
    local pos, rot, fov = Cam.evaluate(c, dt)
    if not pos then return end
    pos, rot = Cam.apply_extras(c, pos, rot)
    local go = cam:call("get_GameObject")
    local xf = E.transform(go)
    xf:call("set_Position", pos)
    xf:call("set_Rotation", rot)
    if fov then cam:call("set_FOV", E.f(fov, 50)) end
    -- depth of field on/off per camera
    if Cam.dof_go ~= go then
        Cam.dof_go = go
        Cam.dof_comp = E.component(go, "via.render.DepthOfField")
        Cam.dof_blender = E.component(go, "via.render.DepthOfFieldParamBlender")
    end
    local want = (c.dof ~= false)
    -- cinematic DoF: our own aperture / focus instead of the game's blended values
    local manual = want and (c.dof_f ~= nil or c.dof_focus ~= nil)
    if Cam.dof_state ~= want or Cam.dof_manual ~= manual then
        Cam.dof_state = want
        Cam.dof_manual = manual
        if Cam.dof_comp then pcall(function() Cam.dof_comp:call("set_Enabled", want) end) end
        if Cam.dof_blender then pcall(function() Cam.dof_blender:call("set_Enabled", want and not manual) end) end
    end
    if manual and Cam.dof_comp then
        local focus = c.dof_focus
        if focus == nil or focus == "target" then
            local anchor = actor_anchor(c.target)
            if anchor then
                local look = Vector3f.new(anchor.x + c.offset[1], anchor.y + c.offset[2], anchor.z + c.offset[3])
                focus = (look - pos):length()
            else
                focus = 3.0
            end
        end
        pcall(function()
            Cam.dof_comp:call("set_FocusDistance", E.f(focus, 3))
            Cam.dof_comp:call("set_FNumber", E.f(c.dof_f, 2.0))
        end)
    end
end)

-------------------------------------------------------------------------------
-- serialization
-------------------------------------------------------------------------------
function Cam.serialize()
    local out = {}
    for _, c in ipairs(Cam.cams) do
        local copy = {}
        for k, v in pairs(c) do copy[k] = v end
        copy.target = nil -- actor addresses are not stable across sessions; resolved by name in project
        copy.target_name = c.target_name
        out[#out + 1] = copy
    end
    return { cams = out, active = Cam.active, enabled = Cam.enabled }
end

function Cam.deserialize(data)
    Cam.cams = {}
    for _, c in ipairs(data.cams or {}) do
        local n = new_cam(c.name)
        for k, v in pairs(c) do n[k] = v end
        Cam.cams[#Cam.cams + 1] = n
    end
    Cam.active = nil
    Cam.enabled = false
end

return Cam
