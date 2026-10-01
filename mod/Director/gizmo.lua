-- Director :: in-world gizmos (ImGuizmo via draw.gizmo) for actors, cameras and bones
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Pose = require("Director.pose")

local G = {
    enabled = true,
    op = "move",          -- exactly one of move | rotate | scale
    mode = "world",       -- world | local
    target = "actor",     -- actor | camera | bone
    show_bones = true,
    bone_filter_helpers = true,  -- hide *_s / *_chain helper bones in the picker
    hover_px = 22,
    bone_op = "rotate",
    snap = 1,             -- index into SNAPS

    ui = nil,             -- set by ui.lua: function returning { actor=, cam_idx= }
    last_pick = nil,
}

local SNAPS = { { "Off", 0, 0 }, { "0.1 m / 15 deg", 0.1, 15 }, { "0.5 m / 45 deg", 0.5, 45 }, { "1 m / 90 deg", 1.0, 90 } }
G.SNAPS = SNAPS

local function snap_pose(pos, rot)
    local sn = SNAPS[G.snap]
    if not sn or sn[2] == 0 then return pos, rot end
    local g, d = sn[2], sn[3]
    pos = Vector3f.new(math.floor(pos.x / g + 0.5) * g, math.floor(pos.y / g + 0.5) * g, math.floor(pos.z / g + 0.5) * g)
    local ex, ey, ez = E.euler_deg(rot)
    rot = E.quat_from_euler_deg(math.floor(ex / d + 0.5) * d, math.floor(ey / d + 0.5) * d, math.floor(ez / d + 0.5) * d)
    return pos, rot
end

local OPS = { move = imgui.ImGuizmoOperation.TRANSLATE, rotate = imgui.ImGuizmoOperation.ROTATE, scale = imgui.ImGuizmoOperation.SCALE }
local MODES = { world = imgui.ImGuizmoMode.WORLD, ["local"] = imgui.ImGuizmoMode.LOCAL }

local function col_len(v) return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z) end

-- decompose a (possibly scaled) matrix into pos, rot, scale
local function decompose(m)
    local sx, sy, sz = col_len(m[0]), col_len(m[1]), col_len(m[2])
    local n = Matrix4x4f.new()
    n[0] = Vector4f.new(m[0].x / sx, m[0].y / sx, m[0].z / sx, 0)
    n[1] = Vector4f.new(m[1].x / sy, m[1].y / sy, m[1].z / sy, 0)
    n[2] = Vector4f.new(m[2].x / sz, m[2].y / sz, m[2].z / sz, 0)
    n[3] = Vector4f.new(0, 0, 0, 1)
    return Vector3f.new(m[3].x, m[3].y, m[3].z), n:to_quat():normalized(), Vector3f.new(sx, sy, sz)
end

local function compose(pos, rot, scale)
    local m = rot:to_mat4()
    scale = scale or Vector3f.new(1, 1, 1)
    m[0] = Vector4f.new(m[0].x * scale.x, m[0].y * scale.x, m[0].z * scale.x, 0)
    m[1] = Vector4f.new(m[1].x * scale.y, m[1].y * scale.y, m[1].z * scale.y, 0)
    m[2] = Vector4f.new(m[2].x * scale.z, m[2].y * scale.z, m[2].z * scale.z, 0)
    m[3] = Vector4f.new(pos.x, pos.y, pos.z, 1)
    return m
end
G.compose = compose
G.decompose = decompose

-------------------------------------------------------------------------------
-- actor transform gizmo
-------------------------------------------------------------------------------
local function draw_actor_gizmo(a)
    local xf = a.xform
    local pos = E.v3(xf:call("get_Position"))
    local rot = xf:call("get_Rotation")
    local okS, scl = pcall(function() return xf:call("get_LocalScale") end)
    scl = (okS and scl) and E.v3(scl) or Vector3f.new(1, 1, 1)
    local m = compose(pos, rot, scl)
    local changed, nm = draw.gizmo(a.addr, m, OPS[G.op] or OPS.move, MODES[G.mode] or MODES.world)
    if changed then
        local npos, nrot, nscl = decompose(nm)
        npos, nrot = snap_pose(npos, nrot)
        if a.kind ~= "object" and G.op == "move" then
            npos = require("Director.extras").resolve_motion(pos, npos) or npos
            local gy = require("Director.extras").ground_y(npos.x, npos.y, npos.z, 2, 5)
            if gy and math.abs(gy - npos.y) < 2.5 then npos.y = gy end
        end
        if G.op == "scale" then pcall(function() xf:call("set_LocalScale", nscl) end) end
        if not G.editing then require("Director.history").mark_break("gizmo") end
        a:set_root_pose(npos, nrot)
        if not a.root_lock then a:set_root_lock(true) end  -- placed actors stay where you put them
        G.editing = { kind = "actor", actor = a }
        return true
    end
    if G.editing and G.editing.kind == "actor" then
        local ed = G.editing; G.editing = nil
        if G.on_edit_done then pcall(G.on_edit_done, ed) end
    end
    return false
end

-------------------------------------------------------------------------------
-- camera gizmo (static / lookat cameras)
-------------------------------------------------------------------------------
local function draw_camera_gizmo(idx)
    local c = Cam.cams[idx]
    if not c or c.mode == "orbit" then return false end
    local pos = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
    local rot = E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4])
    if c.mode == "lookat" then
        -- show the evaluated look rotation, but only position is editable
        local p, r = Cam.evaluate(c, 0)
        if r then rot = r end
    end
    local m = compose(pos, rot, nil)
    local op = (c.mode == "lookat") and OPS.move or OPS[G.op == "scale" and "move" or G.op]
    local changed, nm = draw.gizmo(900000 + idx, m, op, MODES[G.mode])
    if changed then
        local npos, nrot = decompose(nm)
        npos, nrot = snap_pose(npos, nrot)
        c.pos = { npos.x, npos.y, npos.z }
        if c.mode == "static" then c.rot = { nrot.w, nrot.x, nrot.y, nrot.z } end
        return true
    end
    -- small marker so a non-live camera is visible
    draw.sphere(pos, 0.06, 0xFF40C0FF, false)
    draw.world_text(c.name, pos, 0xFFFFFFFF)
    return false
end

-------------------------------------------------------------------------------
-- bones: markers + picking + rotation gizmo on the selected joint
-------------------------------------------------------------------------------
local function joint_objects(a)
    if a.jobjs then return a.jobjs end
    local out = {}
    pcall(function()
        local arr = a.xform:call("get_Joints")
        if arr then
            for _, j in ipairs(arr:get_elements()) do
                local ok, n = pcall(function() return j:call("get_Name") end)
                if ok and n then out[#out + 1] = { name = n, j = j } end
            end
        end
    end)
    a.jobjs = out
    return out
end

local function is_helper(name)
    return name:find("_s$") or name:find("_s%d$") or name:find("_chain") or name:find("_offset") or name:find("_Mat") or name == "root" or name:find("Null")
end

local function draw_bone_markers(a, w)
    local mouse = imgui.get_mouse()
    local best, best_d = nil, G.hover_px
    for _, jo in ipairs(joint_objects(a)) do
        if not (G.bone_filter_helpers and is_helper(jo.name)) then
            local okp, p = pcall(function() return jo.j:call("get_Position") end)
            if okp and p then
                local sp = draw.world_to_screen(p)
                if sp then
                    local selected = (w.joint == jo.name)
                    local posed = w.layer and w.layer.joints[jo.name] ~= nil
                    draw.filled_circle(sp.x, sp.y, selected and 6 or 3.5, selected and 0xFF00FFFF or (posed and 0xFFFF60FF or 0xB0FFFFFF))
                    local d = math.sqrt((sp.x - mouse.x) ^ 2 + (sp.y - mouse.y) ^ 2)
                    if d < best_d then best, best_d = jo, d end
                end
            end
        end
    end
    if best then
        local okp, p = pcall(function() return best.j:call("get_Position") end)
        if okp and p then
            local sp = draw.world_to_screen(p)
            if sp then
                draw.text(best.name, sp.x + 10, sp.y - 8, 0xFFFFFFFF)
                draw.filled_circle(sp.x, sp.y, 7, 0x8000FF00)
            end
        end
        -- Ctrl + click picks
        if reframework:is_key_down(0x11) and imgui.is_mouse_clicked(0) then
            G.last_pick = best.name
        end
    end
end

local function draw_bone_gizmo(a, w, layer)
    local name = w.joint
    if not name then return false end
    local j = a:joint(name)
    if not j then return false end
    local m = j:call("get_WorldMatrix")
    m[3] = Vector4f.new(m[3].x, m[3].y, m[3].z, 1)
    local op = (G.bone_op == "move") and OPS.move or OPS.rotate
    local changed, nm = draw.gizmo(j:get_address(), m, op, MODES[G.mode])
    if changed then
        local npos, nrot = decompose(nm)
        if not G.editing then require("Director.history").mark_break("gizmo bone") end
        pcall(function() j:call("set_Rotation", nrot) end)
        if G.bone_op == "move" then pcall(function() j:call("set_Position", npos) end) end
        local lq = j:call("get_LocalRotation")
        layer.joints[name] = lq
        w.euler = { E.euler_deg(lq) }
        G.editing = { kind = "bone", actor = a, joint = name, layer = layer }
        return true
    end
    if G.editing and G.editing.kind == "bone" then
        local ed = G.editing; G.editing = nil
        if G.on_edit_done then pcall(G.on_edit_done, ed) end
    end
    return false
end

-------------------------------------------------------------------------------
-- per frame
-------------------------------------------------------------------------------
-- in-world camera markers: sphere + name + small frustum; orbit cameras show their circle
G.show_cameras = true
local function draw_cam_marker(c, i, live)
    local pos, rot = Cam.evaluate(c, 0)
    if not pos then return end
    local col = live and 0xFF35D3B6 or 0xFF40C0FF
    draw.sphere(pos, live and 0.09 or 0.06, col, false)
    draw.world_text(string.format("%s%s", c.name, live and "  (live)" or ""), Vector3f.new(pos.x, pos.y + 0.12, pos.z), 0xFFFFFFFF)
    if rot then
        local f, r, u = E.basis(rot)
        local d, hw, hh = 0.5, 0.28, 0.16
        local center = Vector3f.new(pos.x + f.x * d, pos.y + f.y * d, pos.z + f.z * d)
        local corners = {}
        for _, s in ipairs({ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }) do
            corners[#corners + 1] = Vector3f.new(center.x + r.x * hw * s[1] + u.x * hh * s[2], center.y + r.y * hw * s[1] + u.y * hh * s[2], center.z + r.z * hw * s[1] + u.z * hh * s[2])
        end
        local function seg(a, b)
            local sa, sb = draw.world_to_screen(a), draw.world_to_screen(b)
            if sa and sb then draw.line(sa.x, sa.y, sb.x, sb.y, col) end
        end
        for k = 1, 4 do seg(pos, corners[k]); seg(corners[k], corners[k % 4 + 1]) end
    end
    if c.mode == "orbit" and c.target then
        local a = Actor.get(c.target)
        if a and a:valid() then
            local ap = E.v3(a.xform:call("get_Position"))
            local prev = nil
            for k = 0, 32 do
                local ang = k / 32 * math.pi * 2
                local p = Vector3f.new(ap.x + math.cos(ang) * c.radius, ap.y + c.height, ap.z + math.sin(ang) * c.radius)
                if prev then
                    local sa, sb = draw.world_to_screen(prev), draw.world_to_screen(p)
                    if sa and sb then draw.line(sa.x, sa.y, sb.x, sb.y, 0x8040C0FF) end
                end
                prev = p
            end
        end
    end
end

local function draw_cam_paths()
    local Seq = require("Director.sequence")
    local s = Seq.current; if not s then return end
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "cammove" and tr.keys and #tr.keys > 1 then
            local col = (Cam.enabled and Cam.active == tr.cam) and 0xFFFFC85A or 0xAAFFC85A
            local prev
            local k1 = tr.keys[1]
            local kn = tr.keys[#tr.keys]
            local n = math.max(8, math.min(120, math.floor((kn.t - k1.t) / 8)))
            for i = 0, n do
                local t = k1.t + (kn.t - k1.t) * i / n
                -- same interpolation as eval_cammove
                local k0, kk = tr.keys[1], nil
                for j = 1, #tr.keys do if tr.keys[j].t <= t then k0 = tr.keys[j]; kk = tr.keys[j + 1] else break end end
                local p
                if kk and kk.t > k0.t then
                    local u = (t - k0.t) / (kk.t - k0.t); u = u * u * (3 - 2 * u)
                    p = Vector3f.new(k0.pos[1] + (kk.pos[1] - k0.pos[1]) * u, k0.pos[2] + (kk.pos[2] - k0.pos[2]) * u, k0.pos[3] + (kk.pos[3] - k0.pos[3]) * u)
                else
                    p = Vector3f.new(k0.pos[1], k0.pos[2], k0.pos[3])
                end
                local sp = draw.world_to_screen(p)
                if sp and prev then draw.line(prev.x, prev.y, sp.x, sp.y, col) end
                prev = sp
            end
            for _, k in ipairs(tr.keys) do
                local sp = draw.world_to_screen(Vector3f.new(k.pos[1], k.pos[2], k.pos[3]))
                if sp then draw.filled_circle(sp.x, sp.y, 3, col); draw.text(tostring(k.t), sp.x + 5, sp.y - 6, col) end
            end
        end
    end
end

function G.draw_cameras()
    if not G.show_cameras then return end
    pcall(draw_cam_paths)
    for i, c in ipairs(Cam.cams) do
        local live = Cam.enabled and Cam.active == i
        if not live then pcall(draw_cam_marker, c, i, false) end
    end
end

function G.draw()
    G.draw_cameras()
    if not G.enabled or not reframework:is_drawing_ui() then return end
    local ctx = G.ui and G.ui() or {}
    Log.try("gizmo", function()
        if G.target == "actor" and ctx.actor then
            draw_actor_gizmo(ctx.actor)
        elseif G.target == "camera" and ctx.cam_idx then
            draw_camera_gizmo(ctx.cam_idx)
        elseif G.target == "bone" and ctx.actor and ctx.work and ctx.layer then
            if G.show_bones then draw_bone_markers(ctx.actor, ctx.work) end
            if G.last_pick then
                local a, w, layer = ctx.actor, ctx.work, ctx.layer
                w.joint = G.last_pick
                if not layer.joints[w.joint] then local q = Pose.read_local(a, w.joint); if q then layer.joints[w.joint] = q end end
                local q = layer.joints[w.joint]; if q then w.euler = { E.euler_deg(q) } end
                G.last_pick = nil
            end
            draw_bone_gizmo(ctx.actor, ctx.work, ctx.layer)
        end
    end)
end

-- toolbar row (mockup style): Gizmo: [Actor][Camera][Bone] | [Move][Rotate][Scale] | Space | Snapping
local function seg_button(label, on, color_on)
    imgui.push_style_color(21, on and (color_on or 0xFF8A5A2A) or 0xFF3A3A3A)
    local clicked = imgui.button(label)
    imgui.pop_style_color(1)
    return clicked
end

function G.draw_toolbar()
    imgui.text("Gizmo:")
    imgui.same_line()
    if seg_button(" Actor ", G.target == "actor") then G.target = "actor" end
    imgui.same_line()
    if seg_button(" Camera ", G.target == "camera") then G.target = "camera" end
    imgui.same_line()
    if seg_button(" Bone ", G.target == "bone") then G.target = "bone" end
    imgui.same_line(); imgui.text("  |  "); imgui.same_line()
    if seg_button(" Move ", G.op == "move", 0xFF2A6A8A) then G.op = "move"; G.bone_op = "move" end
    imgui.same_line()
    if seg_button(" Rotate ", G.op == "rotate", 0xFF2A6A8A) then G.op = "rotate"; G.bone_op = "rotate" end
    imgui.same_line()
    if seg_button(" Scale ", G.op == "scale", 0xFF2A6A8A) then G.op = "scale"; G.bone_op = "rotate" end
    imgui.same_line()
    imgui.same_line(); imgui.text("  |  Space:"); imgui.same_line()
    imgui.push_item_width(90)
    local modes = { "World", "Local" }
    local mi = G.mode == "local" and 2 or 1
    local ch; ch, mi = imgui.combo("##space", mi, modes)
    if ch then G.mode = mi == 2 and "local" or "world" end
    imgui.pop_item_width()
    imgui.same_line(); imgui.text("  Snapping:"); imgui.same_line()
    imgui.push_item_width(130)
    local names = {}
    for i, sn in ipairs(SNAPS) do names[i] = sn[1] end
    local c2; c2, G.snap = imgui.combo("##snap", G.snap, names)
    imgui.pop_item_width()
    imgui.same_line(); imgui.text("  "); imgui.same_line()
    local c3; c3, G.enabled = imgui.checkbox("show gizmo", G.enabled)
    if G.target == "bone" then
        imgui.same_line()
        local c4; c4, G.show_bones = imgui.checkbox("bone dots (Ctrl+click picks)", G.show_bones)
        imgui.same_line()
        local c5; c5, G.bone_filter_helpers = imgui.checkbox("hide helpers", G.bone_filter_helpers)
    end
end

return G
