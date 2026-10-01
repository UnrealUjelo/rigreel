-- Director :: two-bone IK for hands and feet (analytic, keeps the current bend plane)
--
--   IK.solve(actor, chain, target [, opts])   chain = "L_Hand" | "R_Hand" | "L_Foot" | "R_Foot"; target = world Vector3f
--   Writes the resulting local rotations of the upper / lower bone (and the effector, which keeps its world orientation)
--   into the actor's working pose layer, so the result behaves like any other posed joint (key it with K / auto-key).
local Log = require("Director.log")
local E = require("Director.engine")

local IK = { working = nil --[[ function(actor) -> work, layer (set by the bridge) ]] }

IK.CHAINS = {
    L_Hand = { "L_UpperArm", "L_Forearm", "L_Hand" },
    R_Hand = { "R_UpperArm", "R_Forearm", "R_Hand" },
    L_Foot = { "L_Thigh", "L_Shin", "L_Foot" },
    R_Foot = { "R_Thigh", "R_Shin", "R_Foot" },
}

local function qinv(q) return E.quat(q.w, -q.x, -q.y, -q.z) end

-- rotate v by unit quaternion q (same convention as E.basis: matrix columns are the rotated axes)
local function qrot(q, v)
    local m = q:to_mat4()
    return Vector3f.new(m[0].x * v.x + m[1].x * v.y + m[2].x * v.z, m[0].y * v.x + m[1].y * v.y + m[2].y * v.z, m[0].z * v.x + m[1].z * v.y + m[2].z * v.z)
end
IK.qrot = qrot

-- shortest rotation taking direction a onto direction b
local function from_to(a, b)
    a = a:normalized(); b = b:normalized()
    local d = math.max(-1, math.min(1, a:dot(b)))
    if d > 0.999999 then return E.quat_identity() end
    local axis
    if d < -0.999999 then
        axis = Vector3f.new(1, 0, 0):cross(a)
        if axis:length() < 1e-4 then axis = Vector3f.new(0, 1, 0):cross(a) end
        axis = axis:normalized()
        return E.quat(0, axis.x, axis.y, axis.z)
    end
    axis = a:cross(b):normalized()
    local ang = math.acos(d)
    local h = ang * 0.5
    local s = math.sin(h)
    return E.quat(math.cos(h), axis.x * s, axis.y * s, axis.z * s)
end

local function axis_angle(axis, ang)
    local h = ang * 0.5
    local s = math.sin(h)
    return E.quat(math.cos(h), axis.x * s, axis.y * s, axis.z * s)
end

local function joint_world(j)
    local p = j:call("get_Position"); local r = j:call("get_Rotation")
    return Vector3f.new(p.x, p.y, p.z), E.quat(r.w, r.x, r.y, r.z)
end

function IK.effector_pos(actor, chain)
    local names = IK.CHAINS[chain]; if not names then return nil end
    local j = actor:joint(names[3]); if not j then return nil end
    local p = j:call("get_Position")
    return Vector3f.new(p.x, p.y, p.z)
end

-- opts: keep_effector (default true: hand / foot keeps its world orientation), pole = Vector3f (optional bend hint)
function IK.solve(actor, chain, target, opts)
    opts = opts or {}
    local names = IK.CHAINS[chain]
    if not names then return false, "unknown chain " .. tostring(chain) end
    local ja, jb, jc = actor:joint(names[1]), actor:joint(names[2]), actor:joint(names[3])
    if not (ja and jb and jc) then return false, "missing joints for " .. chain end
    local ok, err = pcall(function()
        local a, qa = joint_world(ja)
        local b, qb = joint_world(jb)
        local c, qc = joint_world(jc)
        local parent = ja:call("get_Parent")
        local qp = parent and select(2, joint_world(parent)) or E.quat_identity()
        local l1, l2 = (b - a):length(), (c - b):length()
        local reach = l1 + l2
        local to_t = target - a
        local d = math.max(0.02, math.min(to_t:length(), reach * 0.999))
        -- bend plane: keep the current one (elbow / knee direction); a pole hint can override it
        local n = (b - a):cross(c - b)
        if opts.pole then n = to_t:cross(opts.pole - a) end
        if n:length() < 1e-5 then n = to_t:cross(Vector3f.new(0, 1, 0)); if n:length() < 1e-5 then n = to_t:cross(Vector3f.new(1, 0, 0)) end end
        n = n:normalized()
        -- 1) aim the upper bone so the chain points at the target
        local q1 = from_to(c - a, to_t)
        qa = (q1 * qa):normalized(); qb = (q1 * qb):normalized()
        b = a + qrot(q1, b - a); c = a + qrot(q1, c - a)
        -- 2) set the bend angle at the middle joint (law of cosines), rotating about the plane normal
        local cos_b = math.max(-1, math.min(1, (l1 * l1 + l2 * l2 - d * d) / (2 * l1 * l2)))
        local want = math.acos(cos_b)
        local u, v = (a - b):normalized(), (c - b):normalized()
        local have = math.acos(math.max(-1, math.min(1, u:dot(v))))
        local q2 = axis_angle(n, want - have)
        -- try both directions, keep the one that shortens the error
        local c1 = b + qrot(q2, c - b)
        local q2b = axis_angle(n, have - want)
        local c2 = b + qrot(q2b, c - b)
        if math.abs((c2 - a):length() - d) < math.abs((c1 - a):length() - d) then q2 = q2b; c = c2 else c = c1 end
        qb = (q2 * qb):normalized()
        -- 3) re-aim the upper bone so the effector lands on the target
        local q3 = from_to(c - a, to_t)
        qa = (q3 * qa):normalized(); qb = (q3 * qb):normalized()
        -- locals: world = parent_world * local
        local la = (qinv(qp) * qa):normalized()
        local lb = (qinv(qa) * qb):normalized()
        local w, layer = IK.working(actor)
        layer.joints[names[1]] = la
        layer.joints[names[2]] = lb
        if opts.keep_effector ~= false then layer.joints[names[3]] = (qinv(qb) * qc):normalized() end
        w.saved_joints = layer.joints
        -- apply immediately so a drag feels live (Pose.apply repeats it after the next motion update)
        ja:call("set_LocalRotation", la); jb:call("set_LocalRotation", lb)
        if opts.keep_effector ~= false then jc:call("set_LocalRotation", layer.joints[names[3]]) end
    end)
    if not ok then Log.err("ik %s: %s", chain, tostring(err)); return false, err end
    return true
end

-- move the effector by a world delta from where it is now
function IK.nudge(actor, chain, delta, opts)
    local p = IK.effector_pos(actor, chain)
    if not p then return false end
    return IK.solve(actor, chain, p + delta, opts)
end

return IK
