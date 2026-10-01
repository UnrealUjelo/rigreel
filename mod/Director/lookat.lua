-- Director :: look-at layer — turns Neck/Head toward a target (camera or another actor) after the animation update
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")

local L = {}

local function defaults()
    return { enabled = false, kind = "camera", target = nil, weight = 1.0, max_deg = 75, smooth = 0.12,
             neck_share = 0.35, forward_sign = 1, offset = { 0, 1.55, 0 }, head = "Head", neck = "Neck_1", cur = nil }
end

function L.setup(a)
    if not a.lookat then a.lookat = defaults() end
    return a.lookat
end

local function rot_between(a, b)
    a = a:normalized(); b = b:normalized()
    local d = a:dot(b)
    if d > 0.99999 then return E.quat_identity() end
    if d < -0.99999 then
        local axis = Vector3f.new(0, 1, 0):cross(a)
        if axis:length() < 1e-4 then axis = Vector3f.new(1, 0, 0):cross(a) end
        axis = axis:normalized()
        return E.quat(0, axis.x, axis.y, axis.z)
    end
    local c = a:cross(b)
    local w = math.sqrt((1 + d) * 2)
    local inv = 1 / w
    return E.quat(w * 0.5, c.x * inv, c.y * inv, c.z * inv):normalized()
end
L.rot_between = rot_between

local function angle_of(q) -- radians
    local w = math.max(-1, math.min(1, math.abs(q.w)))
    return 2 * math.acos(w)
end

function L.target_pos(a)
    local s = a.lookat
    -- a constraint can drive the look-at at a bare world point (sequenced head aim)
    if s.kind == "point" and s.point then return Vector3f.new(s.point[1], s.point[2], s.point[3]) end
    if s.kind == "camera" then
        local cam = E.primary_camera()
        if not cam then return nil end
        return E.v3(E.transform(cam:call("get_GameObject")):call("get_Position"))
    end
    local t = s.target and Actor.get(s.target)
    if not t or not t:valid() then return nil end
    local hj = t:joint("Head")
    if hj then
        local ok, p = pcall(function() return hj:call("get_Position") end)
        if ok and p then return E.v3(p) end
    end
    local p = E.v3(t.xform:call("get_Position"))
    return Vector3f.new(p.x + s.offset[1], p.y + s.offset[2], p.z + s.offset[3])
end

local function solve(a)
    local s = a.lookat
    local head = a:joint(s.head)
    local neck = a:joint(s.neck)
    if not head then return end
    local target = L.target_pos(a)
    if not target then return end
    local head_pos = E.v3(head:call("get_Position"))
    local dir = target - head_pos
    if dir:length() < 0.05 then return end
    local okf, fwd4 = pcall(function() return a.xform:call("get_AxisZ") end)
    if not okf or not fwd4 then return end
    local fwd = E.v3(fwd4)
    if s.forward_sign < 0 then fwd = Vector3f.new(-fwd.x, -fwd.y, -fwd.z) end
    local delta = rot_between(fwd, dir:normalized())
    -- clamp
    local ang = angle_of(delta)
    local maxr = math.rad(s.max_deg)
    if ang > maxr and ang > 1e-4 then delta = E.quat_identity():slerp(delta, maxr / ang):normalized() end
    -- smooth toward the goal
    local cur = s.cur or E.quat_identity()
    if cur:dot(delta) < 0 then delta = E.quat(-delta.w, -delta.x, -delta.y, -delta.z) end
    cur = cur:slerp(delta, math.max(0.01, math.min(1, s.smooth))):normalized()
    s.cur = cur
    local d = E.quat_identity():slerp(cur, math.max(0, math.min(1, s.weight))):normalized()
    -- distribute across neck + head (world-space rotations composed on the left)
    local head_w = head:call("get_Rotation")
    if neck then
        local okp, parent = pcall(function() return neck:call("get_Parent") end)
        local parent_w = (okp and parent) and parent:call("get_Rotation") or nil
        local neck_w = neck:call("get_Rotation")
        local dn = E.quat_identity():slerp(d, s.neck_share):normalized()
        local neck_new = (dn * neck_w):normalized()
        local head_new = (d * head_w):normalized()
        if parent_w then neck:call("set_LocalRotation", (parent_w:inverse() * neck_new):normalized()) end
        head:call("set_LocalRotation", (neck_new:inverse() * head_new):normalized())
    else
        local okp, parent = pcall(function() return head:call("get_Parent") end)
        local parent_w = (okp and parent) and parent:call("get_Rotation") or nil
        local head_new = (d * head_w):normalized()
        if parent_w then head:call("set_LocalRotation", (parent_w:inverse() * head_new):normalized()) end
    end
end

-- runs after Actor pose layers (registered later => later in the after_motion list)
E.after_motion(function()
    for _, a in ipairs(Actor.all()) do
        if a.lookat and a.lookat.enabled and a.lookat.weight > 0.001 then
            local ok, err = pcall(solve, a)
            if not ok then Log.err("lookat %s: %s", a.name, tostring(err)); a.lookat.enabled = false end
        end
    end
end)

function L.serialize(a)
    if not a.lookat then return nil end
    local s = a.lookat
    local t = s.target and Actor.get(s.target)
    return { enabled = s.enabled, kind = s.kind, target_name = t and t.name or nil, weight = s.weight, max_deg = s.max_deg,
             smooth = s.smooth, neck_share = s.neck_share, forward_sign = s.forward_sign, offset = s.offset, head = s.head, neck = s.neck }
end

function L.deserialize(a, data, resolve_actor)
    if not data then return end
    local s = L.setup(a)
    for k, v in pairs(data) do if k ~= "target_name" then s[k] = v end end
    if data.target_name then
        local t = resolve_actor and resolve_actor(data.target_name)
        s.target = t and t.addr or nil
    end
    s.cur = nil
end

return L
