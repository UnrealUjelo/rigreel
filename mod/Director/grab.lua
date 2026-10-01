-- Director :: modal transforms driven by the mouse in the game window (Blender style, no REFramework cursor needed)
--   bone selected  : X / Y / Z   -> rotate the joint around that local axis with the mouse (Shift = fine, Ctrl = 5° steps)
--   character      : G then X/Y/Z (or none = ground plane) -> move · R (Y) -> turn
--   Enter / LMB confirm · Esc / RMB cancel · another axis key switches the axis
-- Only while the game window has focus (E.game_focused, told by the Studio host) and nothing is flying.
local Log = require("Director.log")
local E = require("Director.engine")
local Pose = require("Director.pose")
local Cam = require("Director.camera")

local Grab = { active = nil, ctx = nil } -- ctx(): actor, work, layer  (set by the bridge)

local VK = { X = 0x58, Y = 0x59, Z = 0x5A, G = 0x47, R = 0x52, ENTER = 0x0D, ESC = 0x1B, SHIFT = 0x10, CTRL = 0x11, LMB = 0x01, RMB = 0x02 }
local AXES = { x = Vector3f.new(1, 0, 0), y = Vector3f.new(0, 1, 0), z = Vector3f.new(0, 0, 1) }

local function axis_angle(ax, deg)
    local h = math.rad(deg) * 0.5
    local s = math.sin(h)
    return E.quat(math.cos(h), ax.x * s, ax.y * s, ax.z * s)
end

local function begin(kind, a, w, layer, axis)
    local g = { kind = kind, axis = axis, actor = a, work = w, layer = layer, accum = 0, frames = 0 }
    if kind == "bone" then
        g.name = w.joint
        g.start = layer.joints[g.name] or Pose.read_local(a, g.name)
        if not g.start then return nil end
        g.start = E.quat(g.start.w, g.start.x, g.start.y, g.start.z)
    else
        local p = a.xform:call("get_Position"); local r = a.xform:call("get_Rotation")
        g.start_pos = Vector3f.new(p.x, p.y, p.z); g.start_rot = E.quat(r.w, r.x, r.y, r.z)
        g.dx, g.dy = 0, 0
    end
    E.mouse_delta() -- flush
    require("Director.history").mark_break(kind == "bone" and "rotate bone" or kind)
    Grab.active = g
    return g
end

local function cancel()
    local g = Grab.active
    if not g then return end
    if g.kind == "bone" then
        g.layer.joints[g.name] = g.start
        g.work.euler = { E.euler_deg(g.start) }
    else
        if not g.actor.root_lock then g.actor:set_root_lock(true) end
        g.actor:set_root_pose(g.start_pos, g.start_rot)
    end
    Grab.active = nil
end

Grab.on_confirm = nil -- function(g)
local function confirm() local g = Grab.active; Grab.active = nil; if g and Grab.on_confirm then pcall(Grab.on_confirm, g) end end

local function apply(g)
    local mx, my = E.mouse_delta()
    local fine = E.key_down(VK.SHIFT) and 0.2 or 1.0
    if g.kind == "bone" then
        g.accum = g.accum + mx * 0.5 * fine
        local ang = g.accum
        if E.key_down(VK.CTRL) then ang = math.floor(ang / 5 + 0.5) * 5 end
        local q = (g.start * axis_angle(AXES[g.axis], ang)):normalized() -- rotate in the joint's own frame
        g.layer.joints[g.name] = q
        g.work.euler = { E.euler_deg(q) }
    elseif g.kind == "move" then
        g.dx = g.dx + mx * 0.01 * fine; g.dy = g.dy + my * 0.01 * fine
        local pos
        if g.axis then
            local d = g.dx
            if E.key_down(VK.CTRL) then d = math.floor(d / 0.1 + 0.5) * 0.1 end
            local ax = AXES[g.axis]
            pos = Vector3f.new(g.start_pos.x + ax.x * d, g.start_pos.y + ax.y * d, g.start_pos.z + ax.z * d)
        else
            -- ground plane, relative to the view: mouse right = camera right, mouse up = camera forward
            local pose = Cam.game_pose()
            local f, r = Vector3f.new(0, 0, 1), Vector3f.new(1, 0, 0)
            if pose then
                local q = E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4])
                f, r = E.basis(q)
                f.y = 0; r.y = 0
                if f:length() > 0.01 then f = f:normalized() end
                if r:length() > 0.01 then r = r:normalized() end
            end
            pos = Vector3f.new(g.start_pos.x + r.x * g.dx - f.x * g.dy, g.start_pos.y, g.start_pos.z + r.z * g.dx - f.z * g.dy)
        end
        if not g.actor.root_lock then g.actor:set_root_lock(true) end
        g.actor:set_root_pose(pos, g.start_rot)
    elseif g.kind == "rotate" then
        g.accum = g.accum + mx * 0.5 * fine
        local ang = g.accum
        if E.key_down(VK.CTRL) then ang = math.floor(ang / 15 + 0.5) * 15 end
        local q = (axis_angle(AXES[g.axis or "y"], -ang) * g.start_rot):normalized()
        if not g.actor.root_lock then g.actor:set_root_lock(true) end
        g.actor:set_root_pose(g.start_pos, q)
    end
end

function Grab.update()
    if not E.game_focused or Cam.fly.on then if Grab.active then cancel() end return end
    local g = Grab.active
    local x, y, z = E.key_pressed(VK.X), E.key_pressed(VK.Y), E.key_pressed(VK.Z)
    local axis = x and "x" or y and "y" or z and "z" or nil
    if g then
        g.frames = g.frames + 1
        if E.key_pressed(VK.ESC) or E.key_pressed(VK.RMB) then cancel(); return end
        if E.key_pressed(VK.ENTER) or (g.frames > 5 and E.key_pressed(VK.LMB)) then confirm(); return end
        if axis and g.kind ~= "bone" then g.axis = (g.axis == axis) and nil or axis end
        if axis and g.kind == "bone" and axis ~= g.axis then g.layer.joints[g.name] = g.start; g.axis = axis; g.accum = 0 end
        apply(g)
        return
    end
    local ctx = Grab.ctx and Grab.ctx() or nil
    if not (ctx and ctx.actor) then return end
    local a, w, layer = ctx.actor, ctx.work, ctx.layer
    if w and w.joint and layer and axis then
        if begin("bone", a, w, layer, axis) then Log.info("rotate %s around %s", w.joint, axis) end
    elseif E.key_pressed(VK.G) then
        begin("move", a, w, layer, nil)
    elseif E.key_pressed(VK.R) then
        begin("rotate", a, w, layer, "y")
    end
end

function Grab.state()
    local g = Grab.active
    if not g then return nil end
    return { kind = g.kind, axis = g.axis, name = g.name }
end

return Grab
