-- Director :: cast spawning
--
-- A cast member is built the way the game builds characters, minus the game logic:
--   root GameObject  = via.motion.DummySkeleton (<code>.fbxskel / .skeleton) + via.motion.Motion (+ TreeLayers)
--   parts            = the game's costume prefabs, parented under the root; each part is Mesh + Motion (+ Chain, facial)
--                      and follows the parent's joints by name.
-- Which parts go together comes from the game's own costume preset tables (chainsaw.CostumePresetUserData), baked into
-- director/catalog/cast.json by tools/scripts/build_cast_catalog.py: a preset = a complete, game-authored look
-- (body/head/hair/clothes/accessories) plus, per prefab, the mesh sub-parts to switch off.
-- No AI, no partner logic, no colliders: the character is a pure puppet the Director animates.
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")

local Cast = { list = nil, spawned = {}, LAYERS = 8 }

-- Facial motlists belong to the Motion components carried by the character's
-- visual prefabs. Sending them to the full-body dummy skeleton deforms the body.
local function face_actors(a)
    if not (a and a.cast) then return {} end
    if a.cast.face_actors then return a.cast.face_actors end
    local out = {}
    for _, p in ipairs(a.cast.parts or {}) do
        -- Costume _10 is the head. Body (_00), hair (_20) and clothes (_01)
        -- also carry Motion components but must never receive facial banks.
        -- Costume head prefabs use both `_10.pfb` and numbered variants such
        -- as `_10_01_00.pfb`. Accept either so village cast can load facial
        -- motion banks after its costume parts attach.
        local head = (p.path or ""):lower():match("_10[^/\\]*%.pfb$")
        local mo = head and E.component(p.go, "via.motion.Motion")
        if mo then
            local fa = Actor.wrap(p.go)
            if fa and fa.addr ~= a.addr then fa.internal = true; fa.face_parent = a; out[#out + 1] = fa end
        end
    end
    if #out > 0 then a.cast.face_actors = out end
    return out
end

function Cast.load_facial(a, path, cb)
    local bank = require("Director.catalog").bank_id(path)
    a.facial_banks = a.facial_banks or {}
    local proxy = a.facial_banks[bank]
    if proxy and proxy.ready then if cb then cb(bank, proxy.motions) end; return bank end
    proxy = proxy or { path = path, ready = false, motions = nil }; a.facial_banks[bank] = proxy
    local faces = face_actors(a)
    if #faces == 0 then
        proxy.tries = (proxy.tries or 0) + 1
        if proxy.tries <= 30 then
            E.defer(function() if a:valid() then Cast.load_facial(a, path, cb) end end, 3)
        else Log.warn("%s: no head Motion component is ready", a.name) end
        return bank
    end
    local fired = false
    for _, fa in ipairs(faces) do
        fa:load_motlist(path, function(_, motions)
            if not fired and motions and #motions > 0 then
                fired = true; proxy.ready = true; proxy.motions = motions
                if cb then cb(bank, motions) end
            end
        end)
    end
    return bank
end

function Cast.play_facial(a, bank, mot, opts)
    opts = opts or {}
    local played = false
    for _, fa in ipairs(face_actors(a)) do
        if fa:bank_alive(bank) then
            fa:set_paused(false)
            fa:play(bank, mot, { layer = math.min(opts.layer or 0, math.max(0, fa:layer_count() - 1)), blend = opts.blend, start = opts.start, speed = opts.speed, puppet = false })
            played = true
        end
    end
    if not played then Log.warn("%s: facial bank %d is still loading", a.name, bank) end
    return played
end

function Cast.facial_layer(a, bank, layer)
    for _, fa in ipairs(face_actors(a)) do
        if fa:bank_alive(bank) then return fa:layer_info(math.min(layer or 0, math.max(0, fa:layer_count() - 1))), fa end
    end
end

function Cast.set_facial_frame(a, bank, layer, frame)
    local _, fa = Cast.facial_layer(a, bank, layer)
    if fa then fa:set_frame(math.min(layer or 0, math.max(0, fa:layer_count() - 1)), frame) end
end

function Cast.pause_facial(a, paused)
    for _, fa in ipairs(face_actors(a)) do fa:set_paused(paused) end
end

function Cast.load()
    if Cast.list then return Cast.list end
    local ok, data = pcall(json.load_file, "director/catalog/cast.json")
    Cast.list = (ok and type(data) == "table") and data or {}
    local n, np = 0, 0
    for _, c in ipairs(Cast.list) do
        if c.skel then n = n + 1 end
        np = np + #(c.presets or {})
    end
    Log.info("cast catalog: %d characters (%d spawnable), %d looks", #Cast.list, n, np)
    return Cast.list
end

function Cast.find(id, tree)
    for _, c in ipairs(Cast.load()) do
        if c.id == id and (not tree or c.tree == tree) then return c end
    end
end

-- bare animated skeleton
local function make_root(name, skel_path, layers)
    local go = sdk.call_native_func(nil, sdk.find_type_definition("via.GameObject"), "create(System.String)", name)
    if not go then return nil, "GameObject.create failed" end
    go = go:add_ref()
    local ds = go:call("createComponent(System.Type)", sdk.typeof("via.motion.DummySkeleton"))
    local res = sdk.create_resource("via.motion.SkeletonResource", skel_path)
    if not res then
        pcall(function() go:call("destroy", go) end)
        return nil, "skeleton not found: " .. tostring(skel_path)
    end
    ds:call("set_SkeletonResourceHandle", res:add_ref():create_holder("via.motion.SkeletonResourceHolder"))
    local mo = go:call("createComponent(System.Type)", sdk.typeof("via.motion.Motion"))
    mo:call("setLayerCount", layers)
    for i = 0, layers - 1 do
        local layer = sdk.create_instance("via.motion.TreeLayer", true) or sdk.create_instance("via.motion.TreeLayer")
        layer = layer:add_ref()
        pcall(function() layer:call(".ctor") end)
        mo:call("setLayer", i, layer)
        pcall(function() layer:call("set_Speed", 1.0) end)
    end
    return go, mo
end

-- switch off the mesh sub-parts the preset hides (via.render.Mesh parts; method name differs between engine builds)
local parts_setter
local function mesh_parts_off(go, off)
    if not off or #off == 0 then return end
    local mesh = E.component(go, "via.render.Mesh")
    if not mesh then return end
    for _, cand in ipairs(parts_setter and { parts_setter } or { "setPartsEnable", "set_PartsEnable", "setPartsEnabled", "setPartEnable" }) do
        local ok = pcall(function() for _, idx in ipairs(off) do mesh:call(cand, math.floor(idx), false) end end)
        if ok then parts_setter = cand; return end
    end
    Log.warn("mesh parts setter not found (%s)", go:call("get_Name"))
end

-- plaga tentacles and damage skins are separate materials the game switches on at runtime; a spawned puppet has no
-- character controller, so they would all show. Off by default; Cast.set_gore(a, true) brings them back.
local GORE = { "Tentacle", "Damage", "Plaga", "Parasite", "Blood" }
local function mesh_gore(go, on)
    local mesh = E.component(go, "via.render.Mesh")
    if not mesh then return 0 end
    local n = 0
    pcall(function()
        local cnt = mesh:call("get_MaterialNum") or 0
        for i = 0, cnt - 1 do
            local nm = mesh:call("getMaterialName", i) or ""
            for _, g in ipairs(GORE) do
                if nm:find(g, 1, true) then mesh:call("setMaterialsEnable", i, on and true or false); n = n + 1; break end
            end
        end
    end)
    return n
end

function Cast.set_gore(a, on)
    if not (a and a.cast) then return end
    a.cast.gore = on and true or false
    for _, p in ipairs(a.cast.parts) do if E.valid(p.go) then mesh_gore(p.go, on) end end
end

-- pick an idle-looking clip from the character's general set and play it (so the puppet is not in bind pose)
function Cast.play_idle(a, general_path)
    a:load_motlist(general_path, function(bank, motions)
        if not motions then return end
        local pick
        for _, pat in ipairs({ "stand_loop", "idle_loop", "wait_loop", "stand", "idle", "wait" }) do
            for _, m in ipairs(motions) do
                if m.name:lower():find(pat, 1, true) then pick = m; break end
            end
            if pick then break end
        end
        pick = pick or motions[1]
        if pick then a:play(bank, pick.id, { layer = 0, blend = 0, speed = 1 }) end
    end)
end

-- attach the prefabs of `parts` ({path, off}) under entry.go; cb() once all are in
local function attach_parts(entry, parts, cb)
    local go, xf = entry.go, entry.go:call("get_Transform")
    entry.pending = entry.pending + #parts
    for _, part in ipairs(parts) do
        E.load_prefab(part.path, function(pfb)
            entry.pending = entry.pending - 1
            if pfb and E.valid(go) then
                local ok, err = pcall(function()
                    local pgo = E.instantiate(pfb, xf:call("get_Position"))
                    if not pgo then error("instantiate returned nil") end
                    pgo = pgo:add_ref()
                    local px = pgo:call("get_Transform")
                    px:call("set_Parent", xf)
                    px:call("set_LocalPosition", Vector3f.new(0, 0, 0))
                    px:call("set_LocalRotation", E.quat_identity())
                    entry.parts[#entry.parts + 1] = { path = part.path, go = pgo }
                    entry.face_actors = nil
                    E.defer(function() mesh_parts_off(pgo, part.off); mesh_gore(pgo, entry.gore == true) end, 2) -- after the mesh component has initialised
                    E.defer(function() if E.valid(pgo) then mesh_gore(pgo, entry.gore == true) end end, 40)      -- and again once materials streamed in
                end)
                if not ok then Log.warn("cast part %s failed: %s", part.path, tostring(err)) end
            else
                Log.warn("cast part failed to load: %s", part.path)
            end
            if entry.pending == 0 and cb then pcall(cb) end
        end)
    end
    if #parts == 0 and cb then pcall(cb) end
end

local function preset_of(c, preset)
    local list = c.presets or {}
    if type(preset) == "number" and list[preset] then return list[preset], preset end
    if type(preset) == "string" then
        for i, p in ipairs(list) do if p.name == preset then return p, i end end
    end
    return list[1], list[1] and 1 or nil
end

-- spec: { id=, tree=, preset=index|name, name=, pos=Vector3f, rot=Quaternion, no_idle=bool }
-- returns the Actor immediately (parts attach over the next frames); cb(actor) when every part is in.
function Cast.spawn(spec, cb)
    local c = Cast.find(spec.id, spec.tree)
    if not c then return nil, "unknown cast id " .. tostring(spec.id) end
    if not c.skel then return nil, "no skeleton known for " .. c.id end
    local preset, pidx = preset_of(c, spec.preset)
    if not preset then return nil, "no looks for " .. c.id end
    -- villagers / cultists / soldiers / enemies carry the look's name (Tomás, Soldier (gas mask), Garrador ...); costume looks the character's
    local base = (preset.name and not preset.name:match("^Default") and not preset.name:match("^Costume")) and preset.name:gsub(" · damaged.*$", "") or c.name
    local name = spec.name or (base .. " " .. tostring(#Cast.spawned + 1))
    local go, mo = make_root("Director_" .. name:gsub("[^%w]", "_"), c.skel, Cast.LAYERS)
    if not go then return nil, mo end
    local xf = go:call("get_Transform")
    if spec.pos then xf:call("set_Position", spec.pos) end
    if spec.rot then xf:call("set_Rotation", spec.rot) end
    local entry = { go = go, motion = mo, cast = c, preset = pidx, parts = {}, pending = 0, name = name }
    Cast.spawned[#Cast.spawned + 1] = entry
    local a = Actor.wrap(go)
    a.spawned = true
    a.cast = entry
    a.display_name = name
    a.on_play = function(actor) if actor.cast and actor.cast.physics == false then Cast.set_physics(actor, true) end end
    attach_parts(entry, preset.parts, function()
        Log.info("cast spawned: %s (%s '%s', %d parts)", name, c.id, preset.name, #entry.parts)
        if c.general and not spec.no_idle then Cast.play_idle(a, c.general) else E.defer(function() Cast.set_physics(a, false) end, 3) end
        if cb then pcall(cb, a) end
    end)
    return a
end

local function destroy_parts(entry)
    for _, fa in ipairs(entry.face_actors or {}) do pcall(function() fa:forget() end) end
    entry.face_actors = nil
    for _, p in ipairs(entry.parts) do
        local fa = Actor.get(p.go:get_address())
        if fa and fa.internal then pcall(function() fa:forget() end) end
        pcall(function() p.go:call("get_Transform"):call("set_Parent", nil); p.go:call("destroy", p.go) end)
    end
    entry.parts = {}
end

-- change the whole look (another preset of the same character)
function Cast.set_preset(a, preset)
    local entry = a and a.cast
    if not entry then return false end
    local p, idx = preset_of(entry.cast, preset)
    if not p then return false end
    destroy_parts(entry)
    a.facial_banks = nil
    entry.preset = idx
    attach_parts(entry, p.parts, function() Log.info("%s: look -> %s", entry.name, p.name) end)
    return true
end

function Cast.destroy(a)
    local entry = a and a.cast
    if entry then
        destroy_parts(entry)
        for i, e in ipairs(Cast.spawned) do if e == entry then table.remove(Cast.spawned, i); break end end
    end
    pcall(function() a.go:call("destroy", a.go) end)
    a:forget()
end

function Cast.destroy_all()
    for i = #Cast.spawned, 1, -1 do
        local e = Cast.spawned[i]
        local a = Actor.get(e.go:get_address())
        if a then Cast.destroy(a) else table.remove(Cast.spawned, i) end
    end
end

-- hair / strap / cloth physics on the parts: off in the rest pose (nothing to react to), on as soon as a clip plays
function Cast.set_physics(a, on)
    local entry = a and a.cast
    if not entry then return end
    entry.physics = on and true or false
    for _, p in ipairs(entry.parts) do
        for _, c in ipairs(E.components(p.go)) do
            local n = c:get_type_definition():get_full_name()
            if n == "via.motion.Chain" or n:find("GPUCloth") or n:find("Strands") then pcall(function() c:call("set_Enabled", on and true or false) end) end
        end
    end
end

-- serializable description of a spawned member (for projects / state)
function Cast.describe(a)
    local e = a and a.cast
    if not e then return nil end
    local p = (e.cast.presets or {})[e.preset or 1]
    local paths = {}
    for _, part in ipairs(e.parts) do paths[#paths + 1] = part.path end
    return { id = e.cast.id, code = e.cast.code, tree = e.cast.tree, preset = e.preset, preset_name = p and p.name or nil, parts = paths, name = e.name, pending = e.pending, physics = e.physics ~= false, gore = e.gore == true }
end

return Cast
