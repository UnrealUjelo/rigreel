-- Director :: in-game animation / machinima runtime for RE4 Remake (REFramework Lua)
-- The editor UI is RigReel Studio; this side has no UI except a small REFramework status node.
if reframework:get_game_name() ~= "re4" then return end

local Log = require("Director.log")
local E = require("Director.engine")
local Catalog = require("Director.catalog")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Project = require("Director.project")
local Seq = require("Director.sequence")
local Pose = require("Director.pose")
local Lookat = require("Director.lookat")
local Gizmo = require("Director.gizmo")
local Selftest = require("Director.selftest")
local Bridge = require("Director.bridge")

Director = { version = "0.1.0-alpha.1", Log = Log, E = E, Catalog = Catalog, Actor = Actor, Cam = Cam, Project = Project, Seq = Seq,
             Pose = Pose, Lookat = Lookat, Gizmo = Gizmo, Selftest = Selftest, Bridge = Bridge }

Catalog.load()

re.on_frame(function()
    Gizmo.draw()
    require("Director.grab").update()
    local Edit = require("Director.edit"); Edit.update(); Log.try("edit draw", Edit.draw)
    Log.try("drive", require("Director.drive").update)   -- in-world handles (need the REFramework cursor: Insert)
end)

re.on_draw_ui(function()
    if imgui.tree_node("Director (runtime)") then
        imgui.text(string.format("v%s   actors: %d   cameras: %d   override: %s   editor: %s",
            Director.version, #Actor.all(), #Cam.cams, Cam.enabled and "ON" or "off",
            (os.clock() - (Bridge.editor_alive or 0) < 3) and "connected" or "not connected"))
        imgui.text("UI: run RigReel Studio. Insert toggles the mouse for gizmos.")
        if imgui.button("Release everything") then Seq.release(); for _, a in ipairs(Actor.all()) do a:release() end; Cam.stop(); E.set_player_frozen(false) end
        imgui.tree_pop()
    end
end)

re.on_script_reset(function()
    pcall(E.render_clock_end)
    pcall(Seq.release)
    for _, a in ipairs(Actor.all()) do pcall(a.release, a) end
    Cam.stop()
    E.set_player_frozen(false)
end)

Log.info("Director %s loaded", Director.version)
