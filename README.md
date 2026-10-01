# RigReel Studio

RigReel Studio is an open-source desktop animation and machinima tool for making films with games you legally own. It reads supported assets from an installed game, lets you stage and animate them, and renders the result without redistributing the game's content.

The first game plugin targets Capcom's RE Engine. Resident Evil 4 Remake is the primary and most extensively tested game. Resident Evil 2 Remake support is experimental, and the remaining RE Engine profiles should be treated as unverified.

> **Alpha software:** RigReel works, but it is not yet polished or fully tested across every supported game, rig, animation, and rendering workflow. Back up important film projects and report reproducible problems.

## What it can do

- Browse and spawn characters, looks, props, maps, cameras, and lights.
- Pose characters and sequence game animation on a multi-track timeline.
- Retarget motion between characters and between supported games.
- Import external Mixamo, FBX, BVH, glTF, and Collada animation through Blender.
- Build camera cuts, shots, walk paths, constraints, secondary motion, and layered animation.
- Render H.264, ProRes, or PNG output from the standalone Qt Quick 3D viewport.
- Optionally direct a running copy of RE4 Remake through the included REFramework Lua runtime.
- Add other game families through the C++ game-plugin interface.

## Project status

| Target | Status | Notes |
| --- | --- | --- |
| Resident Evil 4 Remake | Primary target | End-to-end standalone and live workflows have been exercised. Edge cases remain. |
| Resident Evil 2 Remake (RT) | Experimental | Characters, motion, maps, and cross-game retargeting have received limited testing. |
| Other RE Engine games | Unverified | Profiles and format support exist, but they need maintainers with legally obtained installations. |

RigReel has two runtimes behind the same editing interface:

- **Standalone** is the default. A game plugin reads the user's installed game and RigReel renders the scene itself.
- **Live RE4** uses the `mod/Director` REFramework runtime to animate and capture the running game.

The full technical design is documented in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Requirements

- Windows 10 or 11, x64.
- A legally obtained installation of a supported game. No game assets are included here.
- Qt 6.8 or newer with Qt Quick 3D, Qt Quick Controls, Qt Multimedia, and the MinGW toolchain. Development is currently tested with Qt 6.10.3 and MinGW 13.1.
- CMake 3.21 or newer and Ninja.
- FFmpeg on `PATH` for video rendering.
- Blender for external animation import.
- REasy data files for RE Engine filename lists and RSZ type information. Set `RIGREEL_REASY` or the legacy `DIRECTOR_REASY` variable to that installation.
- REFramework only when using the optional live RE4 runtime.

See [docs/BUILDING.md](docs/BUILDING.md) for a clean source build, [docs/RELEASING.md](docs/RELEASING.md) for the GitHub workflow, and [docs/LEGAL.md](docs/LEGAL.md) before redistributing binaries.

## Quick source build

From Git Bash on Windows:

```bash
export RIGREEL_QT_PREFIX='C:/Qt/6.10.3/mingw_64'
export RIGREEL_MINGW_BIN='/c/Qt/Tools/mingw1310_64/bin'
bash studio/qt/build.sh Release
./studio/qt/build/RigReelStudio.exe
```

Useful optional variables:

| Variable | Purpose |
| --- | --- |
| `RIGREEL_HOME` | Repository/workspace root. `DIRECTOR_HOME` remains supported for compatibility. |
| `RIGREEL_GAME_DIR` | RE4 installation used by live mode. `DIRECTOR_GAME_DIR` remains supported. |
| `RIGREEL_REASY` | REasy installation or data directory. `DIRECTOR_REASY` remains supported. |
| `RIGREEL_QT_PREFIX` | Qt MinGW prefix passed to CMake. |
| `RIGREEL_MINGW_BIN` | MinGW `bin` directory in Git Bash path form. |

## Repository layout

```text
docs/                 Architecture and contributor documentation
mod/Director/         Optional RE4 live runtime (REFramework Lua)
studio/qt/            Qt 6 C++/QML desktop application
  plugins/reengine/   RE Engine game plugin
  sdk/director/       Game-plugin interface
  src/engine/         Standalone film runtime and renderer
tools/anim_import/    Blender-side animation conversion helpers
tools/scripts/        Developer catalog/deployment utilities
```

Generated builds, extracted game data, downloaded third-party applications, local virtual environments, and renders are intentionally excluded from Git.

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). Please do not attach copyrighted game files to issues or pull requests. Logs, minimal metadata, and exact reproduction steps are much more useful.

## License and trademarks

RigReel Studio is licensed under the [GNU General Public License version 3](LICENSE), unless a file says otherwise. Third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

RigReel is an unofficial community project. It is not affiliated with, authorized by, or endorsed by Capcom. Resident Evil, RE ENGINE, and related names and assets belong to their respective owners. Users must provide their own legally obtained games; this repository must not contain copyrighted game assets.
