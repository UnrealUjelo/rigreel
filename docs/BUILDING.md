# Building RigReel Studio

The current build is Windows-first and uses Qt 6 with MinGW.

## Toolchain

- Qt 6.8 or newer; the working development configuration is Qt 6.10.3 MinGW x64.
- Qt modules: Core, Gui, Widgets, Quick, QuickWidgets, QuickControls2, Network, Svg, Multimedia, Concurrent, and Quick 3D.
- MinGW 13.1, CMake 3.21 or newer, and Ninja.
- `libzstd.dll` is currently kept under `studio/qt/third_party/zstd` with its license.

Qt Quick 3D is a GPL-3.0-only or commercial module. See `docs/LEGAL.md` before redistributing a build.

## Build from Git Bash

An optional project-local Python environment can provide CMake, Ninja, the helper-script dependencies, and the Lua syntax checker:

```powershell
py -m venv tools\venv
tools\venv\Scripts\python.exe -m pip install -r requirements-dev.txt
```

Install Qt with the official installer or `aqt`. Ensure that the MinGW kit and all modules listed above are present.

Set the paths for your Qt installation. The defaults match the original development machine's standard Qt layout but do not contain a user-specific path.

```bash
export RIGREEL_QT_PREFIX='C:/Qt/6.10.3/mingw_64'
export RIGREEL_MINGW_BIN='/c/Qt/Tools/mingw1310_64/bin'

# Optional when CMake and Ninja are not already on PATH:
export RIGREEL_TOOLS_BIN='/path/to/cmake-and-ninja/bin'

bash studio/qt/build.sh Release
```

Outputs include:

- `studio/qt/build/RigReelStudio.exe`
- `studio/qt/build/plugins/games/reengine.dll`
- `studio/qt/build/recli.exe`
- `studio/qt/build/dirview.exe`

The development restart loop is:

```bash
bash studio/qt/run.sh Release
```

To assemble a local portable directory under `app/`:

```bash
bash studio/qt/package.sh
```

The `app/` directory is ignored by Git. Release ZIP files should be attached to a GitHub Release, never committed to repository history.

## Runtime dependencies

Standalone mode discovers installed Steam libraries and can also be pointed at a game folder from the UI. RE Engine filename lists and RSZ type data can be provided with:

```bash
export RIGREEL_REASY='C:/path/to/REasy'
```

External animation import needs Blender and a Python interpreter capable of running `studio/retarget.py`. Video output needs FFmpeg on `PATH`.

Live RE4 mode additionally requires REFramework and a deployed copy of `mod/Director.lua` plus `mod/Director/` under the game's `reframework/autorun` directory.

## Clean-build verification

Before a release, clone the repository into a new path that does not contain downloaded tools or extracted game content, configure all dependencies explicitly, build Release, and test startup without a game installed. Then test each advertised game/runtime combination separately.
