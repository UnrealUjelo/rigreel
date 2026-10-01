-- Director :: engine helpers (components, scene, resources, math, frame hooks, player freeze)
local Log = require("Director.log")

local E = {}

-------------------------------------------------------------------------------
-- objects & components
-------------------------------------------------------------------------------
local typeof_cache = {}
function E.typeof(name)
    local t = typeof_cache[name]
    if t == nil then
        t = sdk.typeof(name) or false
        typeof_cache[name] = t
    end
    return t or nil
end

function E.valid(obj)
    return obj ~= nil and sdk.is_managed_object(obj)
end

function E.component(go, typename)
    local t = E.typeof(typename)
    if not t or not E.valid(go) then return nil end
    local ok, c = pcall(function() return go:call("getComponent(System.Type)", t) end)
    return ok and c or nil
end

function E.components(go)
    local out = {}
    local ok, arr = pcall(function() return go:call("get_Components") end)
    if ok and arr then
        for _, c in ipairs(arr:get_elements()) do out[#out + 1] = c end
    end
    return out
end

function E.component_names(go)
    local names = {}
    for _, c in ipairs(E.components(go)) do names[#names + 1] = c:get_type_definition():get_name() end
    return names
end

function E.go_name(go)
    local ok, n = pcall(function() return go:call("get_Name") end)
    return (ok and n) or "?"
end

function E.transform(go)
    local ok, xf = pcall(function() return go:call("get_Transform") end)
    return ok and xf or nil
end

function E.scene()
    local sm = sdk.get_native_singleton("via.SceneManager")
    if not sm then return nil end
    return sdk.call_native_func(sm, sdk.find_type_definition("via.SceneManager"), "get_CurrentScene()")
end

-- all components of a type in the current scene (table of REManagedObject)
function E.find_components(typename)
    local scene = E.scene()
    local t = E.typeof(typename)
    if not scene or not t then return {} end
    local ok, arr = pcall(function() return scene:call("findComponents(System.Type)", t) end)
    if not ok or not arr then return {} end
    return arr:get_elements()
end

function E.player_body()
    local cm = sdk.get_managed_singleton("chainsaw.CharacterManager")
    if not cm then return nil end
    local ok, ctx = pcall(function() return cm:call("getPlayerContextRef") end)
    if not ok or not ctx then return nil end
    local ok2, body = pcall(function() return ctx:call("get_BodyGameObject") end)
    return ok2 and body or nil
end

function E.primary_camera()
    return sdk.get_primary_camera()
end

-------------------------------------------------------------------------------
-- resources (cached holders)
-------------------------------------------------------------------------------
local holder_cache = {}
-- type_name e.g. "via.motion.MotionListResource"; path in engine form ("_chainsaw/.../x.motlist")
function E.resource_holder(type_name, path)
    local key = type_name .. "|" .. path:lower()
    local h = holder_cache[key]
    if h then return h end
    local res = sdk.create_resource(type_name, path)
    if not res then return nil, "create_resource returned nil for " .. path end
    res:add_ref()
    h = res:create_holder(type_name .. "Holder")
    if not h then return nil, "create_holder failed for " .. path end
    h:add_ref()
    holder_cache[key] = h
    return h
end

-------------------------------------------------------------------------------
-- math
-------------------------------------------------------------------------------
-- REFramework bit-casts a Lua *integer* passed to a System.Single parameter (set_Speed(1) lands as 0.0):
-- every float argument must go through E.f
function E.f(x, default) x = x == nil and default or x; return (tonumber(x) or 0) + 0.0 end

function E.v3(v) return Vector3f.new(v.x, v.y, v.z) end
function E.v4(v) return Vector4f.new(v.x, v.y, v.z, 1.0) end

-- unambiguous quaternion constructor (fields set explicitly; avoids ctor argument-order doubts)
function E.quat(w, x, y, z)
    local q = Quaternion.new()
    q.w, q.x, q.y, q.z = w, x, y, z
    return q
end

function E.quat_identity() return E.quat(1, 0, 0, 0) end

-- rotation that makes a camera at `eye` look at `target` (camera looks down local -Z; right-handed basis)
function E.look_rotation(eye, target, up)
    up = up or Vector3f.new(0, 1, 0)
    local f = (E.v3(target) - E.v3(eye))
    if f:length() < 1e-5 then return E.quat_identity() end
    f = f:normalized()
    if math.abs(f:dot(up)) > 0.999 then up = Vector3f.new(0, 0, 1) end
    local right = f:cross(up):normalized()
    local nup = right:cross(f):normalized()
    local m = Matrix4x4f.new()
    m[0] = Vector4f.new(right.x, right.y, right.z, 0)
    m[1] = Vector4f.new(nup.x, nup.y, nup.z, 0)
    m[2] = Vector4f.new(-f.x, -f.y, -f.z, 0)
    m[3] = Vector4f.new(0, 0, 0, 1)
    return m:to_quat()
end

-- forward (-Z), right (+X), up (+Y) of a rotation
function E.basis(q)
    local m = q:to_mat4()
    return Vector3f.new(-m[2].x, -m[2].y, -m[2].z), Vector3f.new(m[0].x, m[0].y, m[0].z), Vector3f.new(m[1].x, m[1].y, m[1].z)
end

function E.quat_from_euler_deg(x, y, z)
    local r = math.pi / 180.0
    return Quaternion.new(Vector3f.new(x * r, y * r, z * r))
end

function E.euler_deg(q)
    local e = q:to_euler()
    local d = 180.0 / math.pi
    return e.x * d, e.y * d, e.z * d
end

function E.dist(a, b)
    local d = E.v3(a) - E.v3(b)
    return d:length()
end

-------------------------------------------------------------------------------
-- frame hooks (single registration, dispatch to lists with error isolation)
-------------------------------------------------------------------------------
local hooks = { frame = {}, after_motion = {}, pre_lockscene = {}, late_update = {} }

local function dispatch(list, tag)
    for i = 1, #list do
        local ok, err = pcall(list[i])
        if not ok then Log.err("%s hook: %s", tag, tostring(err)) end
    end
end

function E.on_frame(fn) hooks.frame[#hooks.frame + 1] = fn end
function E.after_motion(fn) hooks.after_motion[#hooks.after_motion + 1] = fn end
function E.pre_lockscene(fn) hooks.pre_lockscene[#hooks.pre_lockscene + 1] = fn end
function E.late_update(fn) hooks.late_update[#hooks.late_update + 1] = fn end

local deferred = {}
-- run fn after n frames (default next frame)
function E.defer(fn, n) deferred[#deferred + 1] = { fn = fn, n = n or 1 } end

E.frame_count = 0

re.on_frame(function()
    E.frame_count = E.frame_count + 1
    if #deferred > 0 then
        for i = #deferred, 1, -1 do
            local d = deferred[i]
            d.n = d.n - 1
            if d.n <= 0 then
                table.remove(deferred, i)
                local ok, err = pcall(d.fn)
                if not ok then Log.err("deferred: %s", tostring(err)) end
            end
        end
    end
    dispatch(hooks.frame, "frame")
    -- A render step is armed by a bridge command during dispatch. Leave its
    -- fixed delta active for exactly one complete engine frame, then hold time
    -- again before the (slow) screen capture and disk write happen.
    local rc = E.render_clock
    if rc and rc.active and rc.phase == "stepping" and E.frame_count > rc.armed_frame then
        -- Chain/GpuCloth use their own clocks and can keep simulating while
        -- GlobalSpeed is zero. Hold them before the slow screen capture.
        pcall(E.render_secondary_hold, rc)
        local ok_delta, delta = pcall(function()
            return sdk.call_native_func(rc.app, rc.app_type, "get_DeltaTime")
        end)
        if ok_delta then rc.actual_delta = delta end
        local ok, err = pcall(function()
            sdk.call_native_func(rc.app, rc.app_type, "set_GlobalSpeed", E.f(0, 0))
        end)
        if ok then
            -- Do not report the frame yet: the step frame itself carries motion blur and TAA
            -- history from the 1/fps of movement, and the capture could land on it or on a later
            -- held frame, so moving cloth/hair alternated smeared/sharp between output frames.
            -- Let a fixed number of held frames render first, then every capture is equally settled.
            rc.phase = "settling"
            rc.settle_until = E.frame_count + (rc.settle_frames or 3)
        else
            rc.error = tostring(err)
            rc.phase = "error"
        end
    elseif rc and rc.active and rc.phase == "settling" and E.frame_count >= (rc.settle_until or 0) then
        rc.ready = rc.requested
        rc.phase = "held"
        rc.engine_frame = E.frame_count
    elseif rc and rc.active and os.clock() - (rc.last_request or os.clock()) > 120 then
        Log.warn("render clock timed out; restoring game time")
        E.render_clock_end()
    end
    Log.tick()
end)

re.on_application_entry("UpdateMotion", function() dispatch(hooks.after_motion, "after_motion") end)
re.on_pre_application_entry("LateUpdateBehavior", function() dispatch(hooks.late_update, "late_update") end)
re.on_pre_application_entry("LockScene", function() dispatch(hooks.pre_lockscene, "pre_lockscene") end)

-------------------------------------------------------------------------------
-- player input freeze (skips chainsaw.PlayerBodyUpdater update/lateUpdate, like REFramework's FreeCam)
-------------------------------------------------------------------------------
E.player_frozen = false
E.game_focused = true -- the Studio host tells us (ping); game-side hotkeys only act when the game window has focus

-- input block: while the Director owns the mouse/keyboard (edit mode, fly, drive) the game must not see buttons or keys.
-- via.hid device getters are hooked; our own readers set E.reading_input around their calls to see the real state.
E.input_blocked = false
E.reading_input = false
local function install_input_block()
    local function neutral(type_name, method, value)
        local td = sdk.find_type_definition(type_name); if not td then return end
        local m = td:get_method(method); if not m then return end
        pcall(sdk.hook, m, function(args) end, function(ret)
            if E.input_blocked and not E.reading_input then return sdk.to_ptr(value) end
            return ret
        end)
    end
    for _, mth in ipairs({ "isDown", "isTrigger", "isRelease", "isRepeat" }) do
        neutral("via.hid.MouseDevice", mth, 0)
        neutral("via.hid.KeyboardDevice", mth, 0)
        neutral("via.hid.GamePadDevice", mth, 0)
    end
    for _, mth in ipairs({ "get_Button", "get_ButtonDown", "get_ButtonUp", "get_ButtonRepeat" }) do
        neutral("via.hid.MouseDevice", mth, 0)
        neutral("via.hid.GamePadDevice", mth, 0)
    end
end
pcall(install_input_block)

-- the base PlayerBodyUpdater hooks below miss classes that override lateUpdate (Separate Ways Ada: Ch3a8z0BodyUpdater)
-- and never touch the "head" object where input becomes actions (Ch3a8z0HeadUpdater): hook the exact types on demand
local extra_hooked = {}
local function hook_frozen(type_name, method)
    local key = type_name .. "." .. method
    if extra_hooked[key] ~= nil then return end
    extra_hooked[key] = false
    local td = sdk.find_type_definition(type_name); if not td then return end
    local m = td:get_method(method); if not m then return end
    local ok = pcall(sdk.hook, m, function(args)
        if E.player_frozen then return sdk.PreHookResult.SKIP_ORIGINAL end
        return sdk.PreHookResult.CALL_ORIGINAL
    end, function(r) return r end)
    extra_hooked[key] = ok
    if ok then Log.info("player freeze hook: %s", key) end
end
function E.ensure_player_hooks()
    local body = E.player_body(); if not body then return end
    for _, c in ipairs(E.components(body)) do
        local n = c:get_type_definition():get_full_name()
        if n:find("BodyUpdater") then hook_frozen(n, "update"); hook_frozen(n, "lateUpdate") end
    end
    local hname = E.go_name(body):gsub("_body$", "_head")
    local scene = E.scene()
    local head = scene and scene:call("findGameObject(System.String)", hname)
    if head then
        for _, c in ipairs(E.components(head)) do
            local n = c:get_type_definition():get_full_name()
            if n:find("HeadUpdater") then hook_frozen(n, "update"); hook_frozen(n, "lateUpdate") end
        end
    end
end
local freeze_hooked = false

local function install_freeze_hooks()
    if freeze_hooked then return end
    freeze_hooked = true
    local td = sdk.find_type_definition("chainsaw.PlayerBodyUpdater")
    if not td then Log.warn("PlayerBodyUpdater type not found; player freeze unavailable"); return end
    local function pre(args)
        if not E.player_frozen then return sdk.PreHookResult.CALL_ORIGINAL end
        local comp = sdk.to_managed_object(args[2])
        local body = E.player_body()
        if comp and body and comp:call("get_GameObject") == body then
            return sdk.PreHookResult.SKIP_ORIGINAL
        end
        return sdk.PreHookResult.CALL_ORIGINAL
    end
    for _, name in ipairs({ "update", "lateUpdate" }) do
        local m = td:get_method(name)
        if m then sdk.hook(m, pre, nil) else Log.warn("PlayerBodyUpdater.%s not found", name) end
    end
    Log.info("player freeze hooks installed")
end

function E.set_player_frozen(on)
    if on then pcall(E.ensure_player_hooks) end
    if on then install_freeze_hooks() end
    E.player_frozen = on and true or false
end

-------------------------------------------------------------------------------
-- world freeze (scene time scale)
-------------------------------------------------------------------------------
E.frozen = false
function E.set_frozen(on)
    local scene = E.scene()
    if not scene then return false end
    local ok, err = pcall(function() scene:call("set_TimeScale", on and 0.0 or 1.0) end)
    if ok then E.frozen = on and true or false else Log.err("set_TimeScale failed: %s", tostring(err)) end
    return ok
end

-------------------------------------------------------------------------------
-- deterministic render clock
-------------------------------------------------------------------------------
-- via.Application DeltaTime is expressed in 60 Hz frame units. In RE4,
-- FixedFrameRate produces 0.1 * GlobalSpeed, so a requested number of seconds
-- uses GlobalSpeed = seconds * 600. The renderer environment must explicitly
-- opt into GlobalSpeed or foliage/shader time continues independently.
E.render_clock = { active = false, phase = "off", requested = 0, ready = 0 }

local function native_call(obj, td, name, ...)
    return sdk.call_native_func(obj, td, name, ...)
end

local function component_get(c, method)
    local ok, value = pcall(function() return c:call(method) end)
    -- Preserve boolean false. `ok and value or nil` would turn an original
    -- unfrozen state into nil and leave cloth frozen after the render.
    if ok then return value end
    return nil
end

local function component_set(c, method, value)
    return pcall(function() c:call(method, value) end)
end

-- Secondary motion does not consistently obey Application.GlobalSpeed.
-- GpuCloth has its own seconds-based DeltaTime, while Chain and ShellFur
-- expose explicit pose/freeze controls. Capture their original settings once
-- and drive them for exactly one engine update per output frame.
local function capture_secondary_motion()
    local out = {}
    local function add(type_name, kind, freeze_get, freeze_set, delta)
        for _, c in ipairs(E.find_components(type_name)) do
            out[#out + 1] = {
                c = c, kind = kind, freeze_set = freeze_set,
                old_freeze = component_get(c, freeze_get),
                old_delta = delta and component_get(c, "get_DeltaTime") or nil,
            }
        end
    end
    add("via.dynamics.GpuCloth", "cloth", "get_Freeze", "set_Freeze", true)
    add("via.motion.Chain", "chain", "get_FreezePose", "set_FreezePose", false)
    add("via.render.ShellFurMesh", "fur", "get_Freeze", "set_Freeze", false)
    return out
end

-- GpuCloth is never frozen: toggling Freeze on every step agitated it badly (SumMovement ~3.9 per step on a held
-- pose vs ~0.1 in normal play: clothes fluttered out of step with the body). It keeps simulating and is held
-- by a ~zero time step instead, then gets exactly the step's seconds on the step frame.
local CLOTH_HOLD_DT = 0.00001

function E.render_secondary_hold(rc)
    for _, x in ipairs(rc.secondary or {}) do
        if E.valid(x.c) then
            if x.kind == "cloth" then
                component_set(x.c, "set_Freeze", false)
                component_set(x.c, "set_DeltaTime", E.f(CLOTH_HOLD_DT, CLOTH_HOLD_DT))
            else
                component_set(x.c, x.freeze_set, true)
            end
        end
    end
end

local function render_secondary_step(rc, seconds)
    local advance = seconds > 0
    for _, x in ipairs(rc.secondary or {}) do
        if E.valid(x.c) then
            if x.kind == "cloth" then
                component_set(x.c, "set_Freeze", false)
                component_set(x.c, "set_DeltaTime", E.f(advance and seconds or CLOTH_HOLD_DT, 1 / 60))
            else
                component_set(x.c, x.freeze_set, not advance)
            end
        end
    end
end

local function restore_secondary_motion(rc)
    local errors = {}
    for _, x in ipairs(rc.secondary or {}) do
        if E.valid(x.c) then
            if x.old_delta ~= nil then
                -- a delta captured during an earlier hold would be ~0: normal play is 1/60
                local d = (x.old_delta and x.old_delta > 0.001) and x.old_delta or 1 / 60
                local ok, err = component_set(x.c, "set_DeltaTime", E.f(d, 1 / 60))
                if not ok then errors[#errors + 1] = tostring(err) end
            end
            -- back to running: a "frozen" original can only be a leftover of an interrupted render
            local ok, err = component_set(x.c, x.freeze_set, false)
            if not ok then errors[#errors + 1] = tostring(err) end
        end
    end
    return errors
end

function E.render_clock_begin(fps)
    if E.render_clock.active then E.render_clock_end() end
    local app = sdk.get_native_singleton("via.Application")
    local app_type = sdk.find_type_definition("via.Application")
    local renderer = sdk.get_native_singleton("via.render.Renderer")
    local renderer_type = sdk.find_type_definition("via.render.Renderer")
    local scene = E.scene()
    if not app or not app_type or not renderer or not renderer_type or not scene then
        return false, "RE Engine timing controls are unavailable"
    end
    local ok, err = pcall(function()
        local rc = {
            active = true, phase = "held", requested = 0, ready = 0,
            fps = math.max(1, tonumber(fps) or 30), app = app, app_type = app_type,
            renderer = renderer, renderer_type = renderer_type, scene = scene,
            old_fixed = native_call(app, app_type, "get_FixedFrameRate"),
            old_speed = native_call(app, app_type, "get_GlobalSpeed"),
            old_renderer_speed = native_call(renderer, renderer_type, "get_AffectedGlobalSpeedEnable"),
            old_scene_scale = scene:call("get_TimeScale"),
            last_request = os.clock(), engine_frame = E.frame_count,
        }
        E.render_clock = rc
        rc.secondary = capture_secondary_motion()
        E.render_secondary_hold(rc)
        native_call(app, app_type, "set_FixedFrameRate", true)
        native_call(renderer, renderer_type, "set_AffectedGlobalSpeedEnable", true)
        scene:call("set_TimeScale", E.f(1, 1))
        native_call(app, app_type, "set_GlobalSpeed", E.f(0, 0))
    end)
    if not ok then
        E.render_clock.error = tostring(err)
        pcall(E.render_clock_end)
        return false, tostring(err)
    end
    return true
end

function E.render_clock_step(token, seconds)
    local rc = E.render_clock
    if not rc.active then return false, "render clock is not active" end
    if rc.phase ~= "held" then return false, "previous render step is not finished" end
    token = math.floor(tonumber(token) or 0)
    seconds = math.max(0, math.min(0.25, tonumber(seconds) or 0))
    rc.requested = token
    rc.requested_seconds = seconds
    rc.last_request = os.clock()
    rc.phase = "stepping"
    rc.armed_frame = E.frame_count
    local ok, err = pcall(function()
        render_secondary_step(rc, seconds)
        native_call(rc.app, rc.app_type, "set_GlobalSpeed", E.f(seconds * 600, 0))
    end)
    if not ok then rc.error = tostring(err); rc.phase = "error"; return false, tostring(err) end
    return true
end

function E.render_clock_end()
    local rc = E.render_clock
    if not rc or not rc.active then
        E.render_clock = { active = false, phase = "off", requested = 0, ready = 0 }
        return true
    end
    local errors = {}
    local function restore(fn)
        local ok, err = pcall(fn)
        if not ok then errors[#errors + 1] = tostring(err) end
    end
    -- Stop first so a failed later restore never leaves a fast clock running.
    restore(function() native_call(rc.app, rc.app_type, "set_GlobalSpeed", E.f(0, 0)) end)
    local secondary_errors = restore_secondary_motion(rc)
    for _, err in ipairs(secondary_errors) do errors[#errors + 1] = err end
    restore(function() native_call(rc.renderer, rc.renderer_type, "set_AffectedGlobalSpeedEnable", rc.old_renderer_speed) end)
    restore(function() native_call(rc.app, rc.app_type, "set_FixedFrameRate", rc.old_fixed) end)
    restore(function() rc.scene:call("set_TimeScale", E.f(rc.old_scene_scale, 1)) end)
    restore(function() native_call(rc.app, rc.app_type, "set_GlobalSpeed", E.f(rc.old_speed, 1)) end)
    E.render_clock = { active = false, phase = #errors > 0 and "error" or "off", requested = rc.requested,
                       ready = rc.ready, error = #errors > 0 and table.concat(errors, "; ") or nil }
    return #errors == 0, E.render_clock.error
end

function E.render_clock_state()
    local rc = E.render_clock or {}
    return { active = rc.active and true or false, phase = rc.phase or "off", requested = rc.requested or 0,
             ready = rc.ready or 0, fps = rc.fps, engine_frame = rc.engine_frame,
             requested_seconds = rc.requested_seconds, actual_delta = rc.actual_delta,
             secondary_count = rc.secondary and #rc.secondary or 0, error = rc.error }
end

-------------------------------------------------------------------------------
-- keyboard edge detection (virtual-key codes)
-------------------------------------------------------------------------------
local key_prev = {}
function E.key_pressed(vk)
    local down = reframework:is_key_down(vk)
    local was = key_prev[vk] or false
    key_prev[vk] = down
    return down and not was
end


function E.key_down(vk) return reframework:is_key_down(vk) end

-- relative mouse motion since the last call. via.hid.MouseDevice exposes get_RawMovePosition / get_WheelDelta (via.Point);
-- the value is only valid inside the frame callback, so it is accumulated in on_frame and consumed by the reader (pre-LockScene).
local mouse_dev, mouse_bad = nil, false
local mouse_acc = { x = 0, y = 0, w = 0 }
local function mouse_pump()
    if mouse_bad then return end
    local ok, err = pcall(function()
        if not mouse_dev then
            local m = sdk.get_native_singleton("via.hid.Mouse")
            local mt = sdk.find_type_definition("via.hid.Mouse")
            mouse_dev = sdk.call_native_func(m, mt, "get_Device")
        end
        E.reading_input = true
        local p = mouse_dev:call("get_RawMovePosition")
        mouse_acc.x = mouse_acc.x + p.x; mouse_acc.y = mouse_acc.y + p.y
        mouse_acc.w = mouse_acc.w + (mouse_dev:call("get_WheelDelta") or 0)
        E.reading_input = false
    end)
    if not ok then mouse_dev = nil; mouse_bad = true; Log.warn("mouse device unavailable: %s", tostring(err)) end
end
function E.mouse_delta()
    local x, y, w = mouse_acc.x, mouse_acc.y, mouse_acc.w
    mouse_acc.x, mouse_acc.y, mouse_acc.w = 0, 0, 0
    return x, y, w
end

-- rotate a vector by a quaternion
function E.rotate(q, v)
    local m = q:to_mat4()
    return Vector3f.new(m[0].x * v.x + m[1].x * v.y + m[2].x * v.z,
                        m[0].y * v.x + m[1].y * v.y + m[2].y * v.z,
                        m[0].z * v.x + m[1].z * v.y + m[2].z * v.z)
end

-------------------------------------------------------------------------------
-- prefabs: standby -> poll get_Ready -> callback(pfb)   (see re4-runtime-findings)
-------------------------------------------------------------------------------
local pending_pfbs = {}
function E.load_prefab(path, cb)
    local pfb = sdk.create_instance("via.Prefab"):add_ref()
    pfb:call(".ctor")
    pfb:call("set_Path", path)
    pfb:call("set_Standby", true)
    pending_pfbs[#pending_pfbs + 1] = { pfb = pfb, path = path, cb = cb, frames = 0 }
    return pfb
end

function E.instantiate(pfb, pos, rot)
    local go = pfb:call("instantiate(via.vec3)", pos or Vector3f.new(0, 0, 0))
    if go and rot then pcall(function() E.transform(go):call("set_Rotation", rot) end) end
    return go
end

E.on_frame(function()
    mouse_pump()
    for i = #pending_pfbs, 1, -1 do
        local p = pending_pfbs[i]
        p.frames = p.frames + 1
        local ok, ready = pcall(function() return p.pfb:call("get_Ready") end)
        if ok and ready then
            table.remove(pending_pfbs, i)
            local okc, err = pcall(p.cb, p.pfb)
            if not okc then Log.err("prefab callback failed (%s): %s", p.path, tostring(err)) end
        elseif p.frames > 1200 or not ok then
            table.remove(pending_pfbs, i)
            Log.err("prefab never became ready: %s", p.path)
            pcall(p.cb, nil)
        end
    end
end)

return E
