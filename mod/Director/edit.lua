-- Director :: Edit mode — the mouse works inside the picture (SFM work camera / Blender viewport)
--
--   F2 (in game) or the Edit button    toggle. While on: the player is a statue, a Director "work camera" is live,
--                                       the cursor is drawn by us, game input is ignored.
--   LMB click                           select the character under the cursor (hover ring shows who)
--   LMB drag                            move the selected character on the floor · Ctrl+drag turns it
--   RMB drag                            look around · with WASD / Q E fly (Shift fast)
--   MMB drag                            orbit around the selection (Shift: pan) · wheel dollies
--   F                                   frame the selection · Esc / F2 leave edit mode
--   "Place" from the Cast list          the next click drops that character where the cursor points
local Log = require("Director.log")
local E = require("Director.engine")
local Cam = require("Director.camera")
local Actor = require("Director.actor")
local IK = require("Director.ik")

local Edit = {
    on = false,
    hover = nil,        -- addr under the cursor
    drag = nil,         -- { kind="move"|"turn", a=actor, plane_y=, grab=Vector3f offset, start_rot=, accum= }
    look = false,       -- RMB held
    orbit = nil,        -- { pivot=Vector3f, dist=, yaw=, pitch=, pan=bool }
    place = nil,        -- { id=, tree=, preset=, name= } pending spawn on click
    pivot = nil,        -- last focus point (frame / orbit centre)
    candidates = nil,   -- function() -> { {addr=, go=, name=}, ... } (bridge supplies scene characters)
    on_select = nil,    -- function(addr)
    on_moved = nil,     -- function(actor) (auto-key)
    group = nil,        -- function(actor) -> other selected actors (group move / turn)
    on_place = nil,     -- function(place, pos, yaw_deg)
    cam_idx = nil,
    made_camera = false,
    statue = nil,
    prev_frozen = false,
    sens = 0.12, speed = 2.5,
    ik_handles = true,  -- show hand / foot handles on the selected character (drag = IK)
    ik_hover = nil,     -- chain name under the cursor
    on_ik_done = nil,   -- function(actor, chain) (auto-key)
}

local VK = { W = 0x57, A = 0x41, S = 0x53, D = 0x44, Q = 0x51, E = 0x45, F = 0x46, P = 0x50, ENTER = 0x0D, BACK = 0x08, SHIFT = 0x10, CTRL = 0x11, ESC = 0x1B, F2 = 0x71 }
local BTN_L, BTN_R, BTN_C = 1, 2, 4

-------------------------------------------------------------------------------
-- input helpers
-------------------------------------------------------------------------------
local mouse_dev
local function mouse()
    local ok, x, y, b = pcall(function()
        if not mouse_dev then
            local m = sdk.get_native_singleton("via.hid.Mouse"); local mt = sdk.find_type_definition("via.hid.Mouse")
            mouse_dev = sdk.call_native_func(m, mt, "get_Device")
        end
        E.reading_input = true
        local p = mouse_dev:call("get_Position")
        local b = mouse_dev:call("get_Button") or 0
        E.reading_input = false
        return p.x, p.y, math.floor(b)
    end)
    if ok then return x, y, b end
    mouse_dev = nil
    return 0, 0, 0
end

local function display()
    local ok, d = pcall(imgui.get_display_size)
    if ok and d and d.x > 0 then return d.x, d.y end
    return 1920, 1080
end

-------------------------------------------------------------------------------
-- camera math
-------------------------------------------------------------------------------
local function live_cam()
    local i = Cam.active
    return i and Cam.cams[i] or nil, i
end

-- world ray through a screen point for the live Director camera (RE Engine FOV is HORIZONTAL; right-handed, looks down -Z)
local function screen_ray(sx, sy)
    local c = live_cam()
    if not c then return nil end
    local pos, rot, fov = Cam.evaluate(c, 0)
    if not pos then return nil end
    local w, h = display()
    local t = math.tan(math.rad((fov or 50) * 0.5))
    local nx = (2 * sx / w - 1) * t
    local ny = (1 - 2 * sy / h) * t * (h / w)
    local f, r, u = E.basis(rot) -- forward, right, up
    local dir = Vector3f.new(f.x + r.x * nx + u.x * ny, f.y + r.y * nx + u.y * ny, f.z + r.z * nx + u.z * ny):normalized()
    return pos, dir
end

local function ray_plane_y(pos, dir, y)
    if math.abs(dir.y) < 1e-4 then return nil end
    local t = (y - pos.y) / dir.y
    if t < 0 then return nil end
    return Vector3f.new(pos.x + dir.x * t, pos.y + dir.y * t, pos.z + dir.z * t)
end

local function to_static(c)
    if c.mode ~= "static" then
        local pos, rot = Cam.evaluate(c, 0)
        c.pos = { pos.x, pos.y, pos.z }; c.rot = { rot.w, rot.x, rot.y, rot.z }
        c.mode = "static"
    end
    c.auto = nil
end

local function yaw_pitch_of(rot)
    local f = E.basis(rot)
    return math.atan(f.x, f.z), math.asin(math.max(-1, math.min(1, f.y)))
end

local function set_look(c, yaw, pitch)
    local cp = math.cos(pitch)
    local f = Vector3f.new(math.sin(yaw) * cp, math.sin(pitch), math.cos(yaw) * cp)
    local eye = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
    local q = E.look_rotation(eye, eye + f)
    c.rot = { q.w, q.x, q.y, q.z }
end

-------------------------------------------------------------------------------
-- enable / disable
-------------------------------------------------------------------------------
function Edit.enable(on)
    on = on and true or false
    if on == Edit.on then return Edit.on end
    if on then
        if Cam.fly.on then Cam.set_fly(false) end
        local Seq = require("Director.sequence")
        Seq.cam_follow = false
        -- navigate with the live Director camera if any, else the Work Camera (never part of the film), placed
        -- where the game's view is now
        Edit.made_camera = false
        if not Cam.active then
            Seq.scene_view = false
            local first = Cam.work == nil
            Cam.set_work(true)
            if not first then Cam.work_from_view() end
            Edit.made_camera = true
        end
        Edit.cam_idx = Cam.active
        Edit.prev_frozen = E.player_frozen
        E.set_player_frozen(true)
        E.input_blocked = true
        local pb = E.player_body()
        local pa = pb and Actor.wrap(pb)
        if pa and not pa.paused and not pa.puppet and not pa:has_pose_work() then pa:set_paused(true); Edit.statue = pa end
        Edit.on = true
        E.mouse_delta()
        Log.info("edit mode on")
    else
        Edit.on = false
        Edit.drag, Edit.look, Edit.orbit, Edit.place, Edit.hover = nil, false, nil, nil, nil
        E.input_blocked = false
        if Edit.statue then pcall(function() Edit.statue:set_paused(false) end); Edit.statue = nil end
        if not Edit.prev_frozen then E.set_player_frozen(false) end
        -- hand the view back to the game only if edit mode itself brought up the work camera
        local c = live_cam()
        if Edit.made_camera and Cam.work_on then Cam.set_work(false)
        elseif c and c.work and Cam.active ~= 0 then Cam.stop() end   -- a legacy "Work camera" from older projects
        Edit.made_camera = false
        Log.info("edit mode off")
    end
    return Edit.on
end

function Edit.toggle() return Edit.enable(not Edit.on) end

-------------------------------------------------------------------------------
-- picking
-------------------------------------------------------------------------------
local function actor_feet(go)
    local ok, p = pcall(function() return go:call("get_Transform"):call("get_Position") end)
    return ok and p or nil
end

local function pick(sx, sy)
    local best, bestd = nil, 40
    for _, cnd in ipairs(Edit.candidates and Edit.candidates() or {}) do
        local p = cnd.go and E.valid(cnd.go) and actor_feet(cnd.go)
        -- props are not people: they are picked by their own centre, with a radius that follows how big they
        -- look on screen, so clicking a barrel selects the barrel instead of whoever stands behind it
        if p and cnd.prop then
            local sp = draw.world_to_screen(Vector3f.new(p.x, p.y, p.z))
            if sp then
                local d = math.sqrt((sp.x - sx) ^ 2 + (sp.y - sy) ^ 2)
                local r = 26
                if cnd.radius then
                    local edge = draw.world_to_screen(Vector3f.new(p.x + cnd.radius, p.y, p.z))
                    if edge then r = math.max(14, math.min(90, math.sqrt((edge.x - sp.x) ^ 2 + (edge.y - sp.y) ^ 2))) end
                end
                if d < r and d < bestd then best, bestd = cnd, d end
            end
            p = nil
        end
        if p then
            local a = draw.world_to_screen(Vector3f.new(p.x, p.y + 0.1, p.z))
            local b = draw.world_to_screen(Vector3f.new(p.x, p.y + 1.7, p.z))
            if a and b then
                -- distance from the cursor to the feet->head segment
                local vx, vy = b.x - a.x, b.y - a.y
                local wx, wy = sx - a.x, sy - a.y
                local len2 = vx * vx + vy * vy
                local t = len2 > 0 and math.max(0, math.min(1, (wx * vx + wy * vy) / len2)) or 0
                local dx, dy = a.x + vx * t - sx, a.y + vy * t - sy
                local d = math.sqrt(dx * dx + dy * dy)
                if d < bestd then best, bestd = cnd, d end
            end
        end
    end
    return best
end

-------------------------------------------------------------------------------
-- per frame
-------------------------------------------------------------------------------
local last_btn = 0
local last_clock = os.clock()
function Edit.update()
    if E.game_focused and E.key_pressed(VK.F2) then Edit.enable(not Edit.on); if not Edit.on then return end end
    if not Edit.on then return end
    if not E.game_focused then return end
    local now = os.clock(); local dt = math.min(now - last_clock, 0.1); last_clock = now
    local sx, sy, btn = mouse()
    local l, r, m = (btn & BTN_L) ~= 0, (btn & BTN_R) ~= 0, (btn & BTN_C) ~= 0
    local pl, pr, pm = (last_btn & BTN_L) ~= 0, (last_btn & BTN_R) ~= 0, (last_btn & BTN_C) ~= 0
    last_btn = btn
    local c = live_cam()
    if not c then Edit.enable(false); return end
    local mx, my, wheel = E.mouse_delta()

    if E.key_pressed(VK.ESC) then
        if Edit.path_drawing and Edit.path_drawing() then if Edit.on_path_toggle then Edit.on_path_toggle() end
        elseif Edit.place then Edit.place = nil elseif Edit.drag then Edit.drag = nil else Edit.enable(false); return end
    end
    -- ---- path pen: P starts / finishes, clicks drop waypoints on the floor, Backspace removes the last ----
    if E.key_pressed(VK.P) and Edit.on_path_toggle then Edit.on_path_toggle() end
    if Edit.path_drawing and Edit.path_drawing() then
        if E.key_pressed(VK.ENTER) then Edit.on_path_toggle() end
        if E.key_pressed(VK.BACK) and Edit.on_path_pop then Edit.on_path_pop() end
        if l and not pl and not r then
            local rp, rd = screen_ray(sx, sy)
            local sel0 = Edit.selected and Edit.selected()
            local ground = sel0 and sel0:valid() and sel0.xform:call("get_Position").y or (pivot and pivot.y - 1.0) or nil
            if not ground then local pb = E.player_body(); if pb then ground = pb:call("get_Transform"):call("get_Position").y end end
            local hit = rp and ground and ray_plane_y(rp, rd, ground)
            if hit and Edit.on_path_point then Edit.on_path_point(hit) end
        end
        Edit.hover = nil
        -- camera controls still work below; body select / drag do not
        if not r and not m then return end
    end

    -- ---- camera: RMB look + fly ----
    if r then
        if not pr then to_static(c); Edit.look = true; Edit.yaw, Edit.pitch = yaw_pitch_of(E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4])) end
        Edit.yaw = Edit.yaw - mx * Edit.sens * math.pi / 180
        Edit.pitch = math.max(-1.55, math.min(1.55, Edit.pitch - my * Edit.sens * math.pi / 180))
        set_look(c, Edit.yaw, Edit.pitch)
        local f, rgt = E.basis(E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4]))
        local up = Vector3f.new(0, 1, 0)
        local v = Edit.speed * dt * (E.key_down(VK.SHIFT) and 4 or 1) * (E.key_down(VK.CTRL) and 0.25 or 1)
        local d = Vector3f.new(0, 0, 0)
        if E.key_down(VK.W) then d = d + f end
        if E.key_down(VK.S) then d = d - f end
        if E.key_down(VK.D) then d = d + rgt end
        if E.key_down(VK.A) then d = d - rgt end
        if E.key_down(VK.E) then d = d + up end
        if E.key_down(VK.Q) then d = d - up end
        c.pos = { c.pos[1] + d.x * v, c.pos[2] + d.y * v, c.pos[3] + d.z * v }
        if wheel ~= 0 then Edit.speed = math.max(0.1, math.min(30, Edit.speed * (wheel > 0 and 1.25 or 0.8))) end
        Edit.busy = true
        return
    elseif pr then
        Edit.look = false
    end

    -- ---- camera: MMB orbit / pan, wheel dolly ----
    local pivot = Edit.pivot
    do
        local sel = Edit.selected and Edit.selected()
        if sel and sel:valid() then local p = sel.xform:call("get_Position"); pivot = Vector3f.new(p.x, p.y + 1.0, p.z) end
    end
    if m then
        if not pm then
            to_static(c)
            local eye = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
            pivot = pivot or (function() local f = E.basis(E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4])); return eye + f * 3.0 end)()
            local off = eye - pivot
            local dist = off:length()
            Edit.orbit = { pivot = pivot, dist = math.max(0.3, dist), yaw = math.atan(off.x, off.z), pitch = math.asin(math.max(-1, math.min(1, off.y / math.max(dist, 1e-4)))), pan = E.key_down(VK.SHIFT) }
        end
        local o = Edit.orbit
        if o.pan then
            local f, rgt, up = E.basis(E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4]))
            local k = o.dist * 0.0015
            o.pivot = o.pivot - rgt * (mx * k) + up * (my * k)
        else
            o.yaw = o.yaw - mx * Edit.sens * math.pi / 180
            o.pitch = math.max(-1.5, math.min(1.5, o.pitch + my * Edit.sens * math.pi / 180))
        end
        if wheel ~= 0 then o.dist = math.max(0.3, o.dist * (wheel > 0 and 0.85 or 1.18)) end
        local cp = math.cos(o.pitch)
        local eye = Vector3f.new(o.pivot.x + math.sin(o.yaw) * cp * o.dist, o.pivot.y + math.sin(o.pitch) * o.dist, o.pivot.z + math.cos(o.yaw) * cp * o.dist)
        c.pos = { eye.x, eye.y, eye.z }
        local q = E.look_rotation(eye, o.pivot); c.rot = { q.w, q.x, q.y, q.z }
        Edit.pivot = o.pivot
        Edit.busy = true
        return
    elseif pm then
        Edit.orbit = nil
    end
    Edit.busy = false
    if wheel ~= 0 and not Edit.drag then
        -- dolly toward the pivot (or straight ahead)
        to_static(c)
        local eye = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
        local f = E.basis(E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4]))
        local step = (pivot and (pivot - eye):length() or 3.0) * 0.15 * (wheel > 0 and 1 or -1)
        c.pos = { eye.x + f.x * step, eye.y + f.y * step, eye.z + f.z * step }
    end

    -- ---- F: frame the selection ----
    if E.key_pressed(VK.F) and pivot then
        to_static(c)
        local eye = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
        local dir = (eye - pivot); if dir:length() < 0.1 then dir = Vector3f.new(0, 0.3, 1) end
        dir = dir:normalized()
        local ne = pivot + dir * 3.0
        c.pos = { ne.x, ne.y, ne.z }
        local q = E.look_rotation(ne, pivot); c.rot = { q.w, q.x, q.y, q.z }
        Edit.pivot = pivot
    end

    -- ---- IK handles (hands / feet of the selected character) ----
    Edit.ik_hover = nil
    local sel = Edit.selected and Edit.selected()
    if Edit.ik_handles and sel and sel:valid() and not Edit.drag and not Edit.place then
        local bestd = 14
        for chain in pairs(IK.CHAINS) do
            local p = IK.effector_pos(sel, chain)
            local s = p and draw.world_to_screen(p)
            if s then
                local d = math.sqrt((s.x - sx) ^ 2 + (s.y - sy) ^ 2)
                if d < bestd then bestd = d; Edit.ik_hover = chain end
            end
        end
    end
    if Edit.ik_hover and l and not pl then
        local p = IK.effector_pos(sel, Edit.ik_hover)
        local c0 = live_cam()
        local _, crot = Cam.evaluate(c0, 0)
        local fwd = E.basis(crot)
        require("Director.history").mark_break("ik " .. Edit.ik_hover)
        Edit.drag = { kind = "ik", a = sel, chain = Edit.ik_hover, point = p, normal = fwd, moved = false }
        local rp, rd = screen_ray(sx, sy)
        if rp then
            local den = rd:dot(fwd)
            if math.abs(den) > 1e-4 then local tt = (p - rp):dot(fwd) / den; Edit.drag.grab = Vector3f.new(rp.x + rd.x * tt, rp.y + rd.y * tt, rp.z + rd.z * tt) - p end
        end
        Edit.drag.grab = Edit.drag.grab or Vector3f.new(0, 0, 0)
    end
    if Edit.drag and Edit.drag.kind == "ik" then
        local g = Edit.drag
        if not l then
            if g.moved and Edit.on_ik_done then Edit.on_ik_done(g.a, g.chain) end
            Edit.drag = nil
        elseif g.a:valid() then
            local rp, rd = screen_ray(sx, sy)
            if rp then
                local den = rd:dot(g.normal)
                if math.abs(den) > 1e-4 then
                    local tt = (g.point - rp):dot(g.normal) / den
                    local hit = Vector3f.new(rp.x + rd.x * tt, rp.y + rd.y * tt, rp.z + rd.z * tt) - g.grab
                    if E.key_down(VK.SHIFT) then -- Shift: depth instead (mouse up/down = away / toward the camera)
                        g.point = g.point + g.normal * (-my * 0.004)
                        hit = g.point
                    else
                        g.point = Vector3f.new(hit.x, hit.y, hit.z)
                    end
                    IK.solve(g.a, g.chain, hit)
                    g.moved = true
                end
            end
        end
        return
    end

    -- ---- hover / select / drag / place ----
    if not Edit.drag then Edit.hover = pick(sx, sy) end
    if l and not pl then
        local rp, rd = screen_ray(sx, sy)
        if Edit.place and rp then
            local ground = pivot and pivot.y - 1.0 or nil
            if not ground then local pb = E.player_body(); if pb then ground = pb:call("get_Transform"):call("get_Position").y end end
            local hit = ground and ray_plane_y(rp, rd, ground)
            if hit then local gy = require("Director.extras").ground_y(hit.x, hit.y, hit.z, 3, 6); if gy then hit = Vector3f.new(hit.x, gy, hit.z) end end
            if hit and Edit.on_place then
                local yaw = math.deg(math.atan(rp.x - hit.x, rp.z - hit.z)) -- face the camera
                Edit.on_place(Edit.place, hit, yaw)
            end
            Edit.place = nil
        elseif Edit.hover then
            if Edit.on_select then Edit.on_select(Edit.hover.addr) end
            local a = Actor.wrap(Edit.hover.go)
            if a then
                local p = a.xform:call("get_Position")
                local hit = rp and ray_plane_y(rp, rd, p.y)
                require("Director.history").mark_break(E.key_down(VK.CTRL) and "turn" or "move")
                Edit.drag = { kind = E.key_down(VK.CTRL) and "turn" or "move", a = a, plane_y = p.y,
                    grab = hit and (hit - Vector3f.new(p.x, p.y, p.z)) or Vector3f.new(0, 0, 0), start_rot = a.xform:call("get_Rotation"), accum = 0, moved = false, others = {} }
                for _, o in ipairs(Edit.group and Edit.group(a) or {}) do
                    local op = o.xform:call("get_Position")
                    Edit.drag.others[#Edit.drag.others + 1] = { a = o, off = Vector3f.new(op.x - p.x, op.y - p.y, op.z - p.z), start_rot = o.xform:call("get_Rotation") }
                end
            end
        end
    end
    if Edit.drag then
        local g = Edit.drag
        if not l then
            if g.moved and Edit.on_moved then Edit.on_moved(g.a) end
            Edit.drag = nil
        elseif g.a:valid() then
            if g.kind == "turn" then
                g.accum = g.accum + mx * 0.5
                local h = math.rad(-g.accum) * 0.5
                local dq = E.quat(math.cos(h), 0, math.sin(h), 0)
                local q = (dq * g.start_rot):normalized()
                if not g.a.root_lock then g.a:set_root_lock(true) end
                g.a:set_root_pose(nil, q)
                for _, o in ipairs(g.others or {}) do if o.a:valid() then if not o.a.root_lock then o.a:set_root_lock(true) end; o.a:set_root_pose(nil, (dq * o.start_rot):normalized()) end end
                g.moved = g.moved or mx ~= 0
            else
                local rp, rd = screen_ray(sx, sy)
                local hit = rp and ray_plane_y(rp, rd, g.plane_y)
                if hit then
                    local np = hit - g.grab
                    if not g.a.root_lock then g.a:set_root_lock(true) end
                    if g.a.kind ~= "object" then
                        local cur = E.v3(g.a.xform:call("get_Position"))
                        np = require("Director.extras").resolve_motion(cur, np) or np
                    end
                    local gy = require("Director.extras").ground_y(np.x, g.plane_y, np.z)
                    if gy and math.abs(gy - g.plane_y) < 2.5 then g.plane_y = gy end -- stairs / slopes: follow the floor
                    g.a:set_root_pose(Vector3f.new(np.x, g.plane_y, np.z), nil)
                    for _, o in ipairs(g.others or {}) do if o.a:valid() then if not o.a.root_lock then o.a:set_root_lock(true) end; o.a:set_root_pose(Vector3f.new(np.x + o.off.x, g.plane_y + o.off.y, np.z + o.off.z), nil) end end
                    g.moved = true
                end
            end
        end
    end
end

-------------------------------------------------------------------------------
-- drawing (on_frame, always when on)
-------------------------------------------------------------------------------
local function ring(p, radius, color, segs)
    segs = segs or 28
    local prev
    for i = 0, segs do
        local a = i / segs * math.pi * 2
        local w = Vector3f.new(p.x + math.cos(a) * radius, p.y + 0.03, p.z + math.sin(a) * radius)
        local s = draw.world_to_screen(w)
        if s and prev then draw.line(prev.x, prev.y, s.x, s.y, color) end
        prev = s
    end
end

function Edit.draw()
    if not Edit.on then return end
    local sx, sy = mouse()
    local sel = Edit.selected and Edit.selected()
    if sel and sel:valid() then ring(sel.xform:call("get_Position"), 0.45, 0xFF5FD9A6) end
    if sel and Edit.group then for _, o in ipairs(Edit.group(sel)) do if o:valid() then ring(o.xform:call("get_Position"), 0.4, 0xCC5FD9A6) end end end
    if Edit.hover and Edit.hover.go and E.valid(Edit.hover.go) and (not sel or Edit.hover.addr ~= sel.addr) then
        ring(actor_feet(Edit.hover.go), 0.4, 0x99FFFFFF)
        local p = actor_feet(Edit.hover.go); local s = p and draw.world_to_screen(Vector3f.new(p.x, p.y + 1.9, p.z))
        if s then draw.text(Edit.hover.label or Edit.hover.name or "", s.x - 20, s.y, 0xFFFFFFFF) end
    end
    if Edit.ik_handles and sel and sel:valid() then
        for chain in pairs(IK.CHAINS) do
            local p = IK.effector_pos(sel, chain)
            local s = p and draw.world_to_screen(p)
            if s then
                local hot = (Edit.ik_hover == chain) or (Edit.drag and Edit.drag.kind == "ik" and Edit.drag.chain == chain)
                draw.filled_circle(s.x, s.y, hot and 7 or 5, hot and 0xFF5FD9A6 or 0xAAFFFFFF)
                draw.filled_circle(s.x, s.y, hot and 4 or 2.5, 0xFF202028)
                if hot then draw.text(chain:gsub("_", " "), s.x + 10, s.y - 8, 0xFFFFFFFF) end
            end
        end
    end
    if not Edit.busy then
        -- cursor (the game hides the OS one)
        local col = Edit.place and 0xFF5FD9A6 or 0xFFFFFFFF
        draw.line(sx - 9, sy, sx - 3, sy, col); draw.line(sx + 3, sy, sx + 9, sy, col)
        draw.line(sx, sy - 9, sx, sy - 3, col); draw.line(sx, sy + 3, sx, sy + 9, col)
        draw.filled_circle(sx, sy, 1.5, col)
        if Edit.place then draw.text("click to place " .. tostring(Edit.place.name or Edit.place.id), sx + 12, sy + 10, col) end
    end
    local w, h = display()
    local hint = (Edit.path_drawing and Edit.path_drawing()) and "PATH · click the floor to add a point · Backspace removes the last · Enter / P done" or
        Edit.place and "EDIT · click: place · Esc: cancel" or
        (Edit.drag and (Edit.drag.kind == "turn" and "turning · release to keep" or Edit.drag.kind == "ik" and ("IK " .. tostring(Edit.drag.chain):gsub("_", " ") .. " · Shift: depth · release to keep") or "moving · release to keep") or
        "EDIT · click select · drag move · Ctrl+drag turn · RMB look+WASD · MMB orbit · Shift+MMB pan · wheel dolly · F frame · F2 exit")
    draw.filled_rect(8, h - 26, math.min(w - 16, 8 + #hint * 6.6), 18, 0x88000000)
    draw.text(hint, 12, h - 24, 0xFFDDDDDD)
end

function Edit.state()
    return { on = Edit.on, busy = Edit.busy or false, place = Edit.place and (Edit.place.name or Edit.place.id) or nil,
        hover = Edit.hover and Edit.hover.addr or nil, drag = Edit.drag and Edit.drag.kind or nil, cam = Edit.cam_idx, ik_hover = Edit.ik_hover, ik_handles = Edit.ik_handles }
end

return Edit
