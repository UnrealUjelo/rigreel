-- Director :: lights — point / spot lights you can place, colour, aim and keep in the project (SFM's lights)
--
-- A light is a GameObject with a via.render.PointLight / SpotLight component. Property setter names differ a little
-- between engine builds, so every setter is tried from a candidate list and the first one that exists is remembered.
local Log = require("Director.log")
local E = require("Director.engine")

local Lights = { list = {}, next_id = 1 }

-- Point and spot are the ones RE4 actually renders at runtime. AreaLight exists in the engine but draws
-- nothing in this build (tested: same position, same intensity, no contribution), so it is not offered.
local COMP = { point = "via.render.PointLight", spot = "via.render.SpotLight", sun = "via.render.DirectionalLight" }
local SETTERS = {
    intensity = { "set_Intensity", "set_Power" },
    radius = { "set_Radius", "set_Range", "set_AttenuationRadius" },
    cone = { "set_Cone", "set_ConeAngle", "set_Angle", "set_OuterAngle" },
    spread = { "set_Spread", "set_InnerAngle", "set_Penumbra" },
    shadows = { "set_ShadowEnable", "set_Shadow", "set_EnableShadow", "set_CastShadow" },
    enabled = { "set_Enabled" },
    -- film controls the engine already has: colour temperature, bounce, volumetrics, shadow softness
    temperature = { "set_Temperature" },
    blackbody = { "set_BlackBodyRadiation" },
    bounce = { "set_BounceIntensity" },
    volumetric = { "set_VolumetricScatteringIntensity" },
    specular = { "set_SpecularScale" },
    shadow_bias = { "set_ShadowBias" },
    shadow_soft = { "set_ShadowAngle", "set_ShadowVariance" },
    size = { "set_LineLightRadius" },
    importance = { "set_ImportantLevel" },
    ao = { "set_AOEfficiency" },
    min_rough = { "set_MinRoughness" },
}
local resolved = {} -- [comp type .. key] = method name or false

local function call_first(comp, key, ...)
    local td = comp:get_type_definition()
    local ck = td:get_full_name() .. "|" .. key
    if resolved[ck] == false then return false end
    if resolved[ck] then return pcall(function(...) comp:call(resolved[ck], ...) end, ...) end
    for _, m in ipairs(SETTERS[key] or {}) do
        if td:get_method(m) then
            resolved[ck] = m
            return pcall(function(...) comp:call(m, ...) end, ...)
        end
    end
    resolved[ck] = false
    Log.warn("light: no setter for %s on %s", key, td:get_full_name())
    return false
end

local function set_color(comp, rgb)
    local ok = pcall(function()
        local col = ValueType.new(sdk.find_type_definition("via.Color"))
        local r, g, b = math.floor((rgb[1] or 1) * 255 + 0.5), math.floor((rgb[2] or 1) * 255 + 0.5), math.floor((rgb[3] or 1) * 255 + 0.5)
        col:set_field("rgba", (255 << 24) | (b << 16) | (g << 8) | r)
        comp:call("set_Color", col)
    end)
    if not ok then
        -- some builds expose a float colour vector
        pcall(function() comp:call("set_Color", Vector4f.new(rgb[1] or 1, rgb[2] or 1, rgb[3] or 1, 1)) end)
    end
end

function Lights.get(id) for _, l in ipairs(Lights.list) do if l.id == id then return l end end end

function Lights.add(kind, pos, rot, opts)
    kind = COMP[kind] and kind or "point"
    opts = opts or {}
    local LABEL = { point = "Light", spot = "Spot", area = "Area", sun = "Sun" }
    local name = opts.name or (LABEL[kind] or "Light") .. " " .. Lights.next_id
    -- A bare GameObject.create() light is never drawn: it belongs to no scene, so the renderer ignores it.
    -- Creating it inside the camera's folder is what makes it actually light the picture.
    local folder
    pcall(function()
        local cam = E.primary_camera()
        local cgo = cam and cam:call("get_GameObject")
        folder = cgo and cgo:call("get_Folder")
    end)
    local go
    if folder then
        go = sdk.call_native_func(nil, sdk.find_type_definition("via.GameObject"), "create(System.String, via.Folder)", "Director_Light_" .. Lights.next_id, folder)
    end
    if not go then
        go = sdk.call_native_func(nil, sdk.find_type_definition("via.GameObject"), "create(System.String)", "Director_Light_" .. Lights.next_id)
        Log.warn("light %s has no scene folder: it may not be drawn", name)
    end
    if not go then return nil, "GameObject.create failed" end
    go = go:add_ref()
    local comp = go:call("createComponent(System.Type)", sdk.typeof(COMP[kind]))
    if not comp then pcall(function() go:call("destroy", go) end); return nil, "no " .. COMP[kind] end
    local xf = go:call("get_Transform")
    if pos then xf:call("set_Position", pos) end
    if rot then xf:call("set_Rotation", rot) end
    local l = { id = Lights.next_id, go = go, comp = comp, xf = xf, kind = kind, name = name,
        intensity = opts.intensity or (kind == "spot" and 3000 or kind == "sun" and 6 or 1500), color = opts.color or { 1, 0.96, 0.9 },
        radius = opts.radius or 8, cone = opts.cone or 45, spread = opts.spread or 0.5,
        temperature = opts.temperature or 6500, blackbody = opts.blackbody or false,
        bounce = opts.bounce or 1, volumetric = opts.volumetric or 1, specular = opts.specular or 1,
        size = opts.size or 0.25, shadow_bias = opts.shadow_bias or 0, shadow_soft = opts.shadow_soft or 0,
        shadows = opts.shadows ~= false, enabled = true }
    Lights.next_id = Lights.next_id + 1
    Lights.list[#Lights.list + 1] = l
    Lights.apply(l)
    Log.info("light added: %s (%s)", name, COMP[kind])
    return l
end

function Lights.apply(l)
    if not (l.comp and E.valid(l.comp)) then return end
    call_first(l.comp, "enabled", l.enabled ~= false)
    call_first(l.comp, "intensity", E.f(l.intensity, 1000))
    if l.kind ~= "sun" then call_first(l.comp, "radius", E.f(l.radius, 8)) end
    if l.kind == "spot" then
        call_first(l.comp, "cone", E.f(l.cone, 45))
        call_first(l.comp, "spread", E.f(l.spread, 0.5))
    end
    if l.kind == "area" then call_first(l.comp, "size", E.f(l.size, 0.25)) end
    call_first(l.comp, "shadows", l.shadows ~= false)
    -- film extras: a warm/cool dial instead of picking RGB by hand, plus how much the light bounces,
    -- how much it lights fog, how sharp its shadow edge is
    if l.blackbody then
        call_first(l.comp, "blackbody", true)
        call_first(l.comp, "temperature", E.f(l.temperature, 6500))
    else
        call_first(l.comp, "blackbody", false)
        set_color(l.comp, l.color or { 1, 1, 1 })
    end
    call_first(l.comp, "bounce", E.f(l.bounce, 1))
    call_first(l.comp, "volumetric", E.f(l.volumetric, 1))
    call_first(l.comp, "specular", E.f(l.specular, 1))
    if (l.shadow_bias or 0) ~= 0 then call_first(l.comp, "shadow_bias", E.f(l.shadow_bias, 0)) end
    if (l.shadow_soft or 0) ~= 0 then call_first(l.comp, "shadow_soft", E.f(l.shadow_soft, 0)) end
end

function Lights.update(id, fields)
    local l = Lights.get(id); if not l then return end
    for k, v in pairs(fields or {}) do
        if k == "pos" then l.xf:call("set_Position", Vector3f.new(v[1], v[2], v[3]))
        elseif k == "euler" then l.xf:call("set_Rotation", E.quat_from_euler_deg(v[1], v[2], v[3]))
        elseif k == "name" or k == "intensity" or k == "color" or k == "radius" or k == "cone" or k == "spread" or k == "shadows" or k == "enabled" or k == "temperature" or k == "blackbody" or k == "bounce" or k == "volumetric" or k == "specular" or k == "size" or k == "shadow_bias" or k == "shadow_soft" then l[k] = v end
    end
    Lights.apply(l)
end

-- point the light (spot) at a world position
function Lights.aim(id, target)
    local l = Lights.get(id); if not l then return end
    local p = l.xf:call("get_Position")
    l.xf:call("set_Rotation", E.look_rotation(p, target))
end

function Lights.remove(id)
    for i, l in ipairs(Lights.list) do
        if l.id == id then
            pcall(function() l.go:call("destroy", l.go) end)
            table.remove(Lights.list, i)
            return true
        end
    end
    return false
end

function Lights.clear() for i = #Lights.list, 1, -1 do Lights.remove(Lights.list[i].id) end end

function Lights.describe()
    local out = {}
    for _, l in ipairs(Lights.list) do
        local ok, p, r = pcall(function() return l.xf:call("get_Position"), l.xf:call("get_Rotation") end)
        local ex, ey, ez = 0, 0, 0
        if ok and r then ex, ey, ez = E.euler_deg(r) end
        out[#out + 1] = { id = l.id, kind = l.kind, name = l.name, pos = ok and { p.x, p.y, p.z } or { 0, 0, 0 }, euler = { ex, ey, ez },
            intensity = l.intensity, color = l.color, radius = l.radius, cone = l.cone, spread = l.spread, shadows = l.shadows ~= false, enabled = l.enabled ~= false,
            temperature = l.temperature or 6500, blackbody = l.blackbody or false, bounce = l.bounce or 1, volumetric = l.volumetric or 1,
            specular = l.specular or 1, size = l.size or 0.25, shadow_bias = l.shadow_bias or 0, shadow_soft = l.shadow_soft or 0 }
    end
    return out
end

function Lights.serialize() return Lights.describe() end

function Lights.deserialize(data)
    Lights.clear()
    for _, d in ipairs(data or {}) do
        local l = Lights.add(d.kind, Vector3f.new(d.pos[1], d.pos[2], d.pos[3]), E.quat_from_euler_deg(d.euler[1], d.euler[2], d.euler[3]),
            { name = d.name, intensity = d.intensity, color = d.color, radius = d.radius, cone = d.cone, spread = d.spread, shadows = d.shadows,
              temperature = d.temperature, blackbody = d.blackbody, bounce = d.bounce, volumetric = d.volumetric, specular = d.specular,
              size = d.size, shadow_bias = d.shadow_bias, shadow_soft = d.shadow_soft })
        if l then l.enabled = d.enabled ~= false; Lights.apply(l) end
    end
end

-- editor overlay: a small sun for every light (spot: plus its direction)
function Lights.draw()
    local Gizmo = package.loaded["Director.gizmo"]
    if not (Gizmo and Gizmo.enabled) or #Lights.list == 0 then return end
    for _, l in ipairs(Lights.list) do
        local ok, p, r = pcall(function() return l.xf:call("get_Position"), l.xf:call("get_Rotation") end)
        if ok and p then
            local s = draw.world_to_screen(p)
            if s then
                local col = l.enabled ~= false and 0xFFFFE08A or 0x88FFE08A
                draw.filled_circle(s.x, s.y, 4, col)
                for i = 0, 7 do local a = i * math.pi / 4; draw.line(s.x + math.cos(a) * 7, s.y + math.sin(a) * 7, s.x + math.cos(a) * 11, s.y + math.sin(a) * 11, col) end
                draw.text(l.name, s.x + 14, s.y - 7, col)
                if l.kind == "spot" and r then
                    local f = E.basis(r)
                    local e = draw.world_to_screen(Vector3f.new(p.x + f.x * 1.5, p.y + f.y * 1.5, p.z + f.z * 1.5))
                    if e then draw.line(s.x, s.y, e.x, e.y, col) end
                end
            end
        end
    end
end
E.on_frame(function() pcall(Lights.draw) end)

return Lights
