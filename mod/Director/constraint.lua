-- Director :: constraints — relationships the sequencer keeps true every frame
--
-- Attachment (extras.lua) already parents a prop to a joint. Constraints go the other way and are what a film
-- actually needs: make a character's *hand* follow something (hold a lamp with both hands, keep a hand on a
-- door, put a palm on another character's shoulder), keep a foot planted while the body moves, or aim the head
-- at a moving target. Each one is solved after the animation has played, so it wins over the clip underneath.
--
--   { id, kind = "ik_pin", actor = addr, chain = "L_Hand", target = { actor = addr, joint = "R_Wep" },
--     offset = {x,y,z}, weight = 1, range = { a, b } | nil, name }
--
-- kind:
--   ik_pin   the chain's effector sits on the target (a prop, a joint of another character, or a world point)
--   plant    the effector is frozen where it was when the constraint started (a planted foot / hand)
--   look     the actor's look-at is driven at the target (sequenced version of the Look at panel)
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")
local IK = require("Director.ik")

local C = { list = {}, next_id = 1 }

local function target_point(t)
    if not t then return nil end
    if t.point then return Vector3f.new(t.point[1], t.point[2], t.point[3]) end
    local a = t.actor and Actor.get(t.actor)
    if not (a and a:valid()) then return nil end
    if t.joint and t.joint ~= "" then
        local j = a:joint(t.joint)
        if j then
            local ok, p = pcall(function() return j:call("get_Position") end)
            if ok and p then return E.v3(p) end
        end
    end
    local ok, p = pcall(function() return E.v3(a.xform:call("get_Position")) end)
    return ok and p or nil
end

function C.get(id) for _, c in ipairs(C.list) do if c.id == id then return c end end end

function C.add(spec)
    local a = spec.actor and Actor.get(spec.actor)
    if not a then return nil, "no character selected" end
    local c = {
        id = C.next_id, kind = spec.kind or "ik_pin", actor = a.addr, actor_name = a.name,
        chain = spec.chain or "R_Hand", target = spec.target, offset = spec.offset or { 0, 0, 0 },
        weight = spec.weight or 1, range = spec.range, enabled = true,
        name = spec.name,
    }
    if not c.name then
        local what = c.target and c.target.actor and (Actor.get(c.target.actor) or {}).name or "a point"
        c.name = (c.kind == "plant" and "Plant " or c.kind == "look" and "Look " or "Hold ") .. c.chain .. (c.kind == "ik_pin" and (" on " .. tostring(what)) or "")
    end
    C.next_id = C.next_id + 1
    C.list[#C.list + 1] = c
    Log.info("constraint: %s (%s)", c.name, c.kind)
    return c
end

function C.update(id, fields)
    local c = C.get(id); if not c then return end
    for k, v in pairs(fields or {}) do
        if k == "target_actor" then c.target = c.target or {}; c.target.actor = v; c.target.point = nil
        elseif k == "target_joint" then c.target = c.target or {}; c.target.joint = v
        elseif k == "clear_range" then c.range = nil
        else c[k] = v end
    end
end

function C.remove(id)
    for i, c in ipairs(C.list) do if c.id == id then table.remove(C.list, i); return true end end
    return false
end

function C.remove_for(addr)
    for i = #C.list, 1, -1 do if C.list[i].actor == addr then table.remove(C.list, i) end end
end

function C.clear() C.list = {} end

-- solved every frame after the sequence has posed everybody
function C.solve(t)
    for _, c in ipairs(C.list) do
        if c.enabled ~= false and (not c.range or (t >= c.range[1] and t <= c.range[2])) then
            local a = Actor.get(c.actor)
            if a and a:valid() and not a.detached then
                if c.kind == "look" then
                    local p = target_point(c.target)
                    if p then
                        a.lookat = a.lookat or {}
                        a.lookat.enabled = true
                        a.lookat.kind = "point"
                        a.lookat.point = { p.x + c.offset[1], p.y + c.offset[2], p.z + c.offset[3] }
                        a.lookat.weight = c.weight or 1
                    end
                else
                    local p
                    if c.kind == "plant" then
                        if not c.planted then
                            local e = IK.effector_pos(a, c.chain)
                            c.planted = e and { e.x, e.y, e.z } or nil
                        end
                        p = c.planted and Vector3f.new(c.planted[1], c.planted[2], c.planted[3]) or nil
                    else
                        p = target_point(c.target)
                    end
                    if p then
                        local goal = Vector3f.new(p.x + c.offset[1], p.y + c.offset[2], p.z + c.offset[3])
                        Log.try("constraint ik", IK.solve, a, c.chain, goal, { weight = c.weight or 1 })
                    end
                end
            end
        elseif c.kind == "plant" then
            c.planted = nil -- leaving the range releases the plant, so it re-grabs next time
        end
    end
end

function C.describe()
    local out = {}
    for _, c in ipairs(C.list) do
        local ta = c.target and c.target.actor and Actor.get(c.target.actor)
        out[#out + 1] = {
            id = c.id, kind = c.kind, name = c.name, actor = c.actor, actor_name = c.actor_name, chain = c.chain,
            target_actor = c.target and c.target.actor or nil, target_name = ta and (ta.display_name or ta.name) or (c.target and c.target.point and "a fixed point" or nil),
            target_joint = c.target and c.target.joint or nil, offset = c.offset, weight = c.weight or 1,
            range = c.range, enabled = c.enabled ~= false,
        }
    end
    return out
end

function C.serialize()
    local out = {}
    for _, c in ipairs(C.list) do
        local ta = c.target and c.target.actor and Actor.get(c.target.actor)
        out[#out + 1] = { kind = c.kind, name = c.name, actor_name = c.actor_name, chain = c.chain,
            target_name = ta and ta.name or nil, target_point = c.target and c.target.point or nil,
            target_joint = c.target and c.target.joint or nil, offset = c.offset, weight = c.weight, range = c.range, enabled = c.enabled }
    end
    return out
end

function C.deserialize(data, resolve)
    C.clear()
    for _, d in ipairs(data or {}) do
        local a = resolve and resolve(d.actor_name)
        if a then
            local target = nil
            if d.target_point then target = { point = d.target_point }
            elseif d.target_name then local t = resolve(d.target_name); if t then target = { actor = t.addr, joint = d.target_joint } end end
            local c = C.add({ kind = d.kind, actor = a.addr, chain = d.chain, target = target, offset = d.offset, weight = d.weight, range = d.range, name = d.name })
            if c then c.enabled = d.enabled ~= false end
        end
    end
end

return C
