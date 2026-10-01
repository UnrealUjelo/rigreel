# Contributing to RigReel Studio

Thank you for helping make game-based animation more approachable.

## Before opening a change

1. Search existing issues and describe the game, game version, runtime mode, and exact steps involved.
2. Never upload game archives, extracted models, textures, audio, animations, encryption material, or other copyrighted game content.
3. Keep engine-specific behavior inside a game plugin when possible. The standalone editor should consume the neutral interfaces under `studio/qt/sdk/director`.
4. Preserve the shared operation/state/data contract between the standalone runtime and the optional live runtime.

## Development workflow

1. Follow [docs/BUILDING.md](docs/BUILDING.md).
2. Create a focused branch.
3. Build the affected targets and test the smallest relevant workflow.
4. Update documentation when behavior, compatibility, file formats, or setup changes.
5. Open a pull request describing what changed, how it was verified, and any game-specific assumptions.

Do not commit generated builds, downloaded tools, extracted content, personal paths, logs, renders, or local settings. The root `.gitignore` covers the common cases, but contributors remain responsible for reviewing every staged file.

## Coding notes

- C++ uses C++20 and Qt conventions already present in the codebase.
- QML state must tolerate empty collections and a temporarily unavailable runtime.
- Prefer deterministic, scriptable checks over long manual UI procedures.
- Treat files parsed from game installations as untrusted input: validate offsets, sizes, counts, and allocation limits.
- Retain required copyright and license notices when adapting third-party work.

## Testing expectations

At minimum, build the touched target. Parser and retargeting changes should also be exercised with `recli`; rendering changes should be checked with `dirview` or a short standalone render. Live-runtime changes should be tested separately and must not make REFramework a dependency of standalone mode.

By contributing, you agree that your contribution is provided under GPL-3.0-only and that you have the right to submit it.
