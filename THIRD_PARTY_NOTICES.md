# Third-party notices

RigReel Studio depends on third-party projects. Their names do not imply endorsement.

## Distributed in this repository

- **Lucide icons** — ISC License. The complete notice is in `studio/qt/resources/icons/LICENSE-lucide.txt`.
- **Zstandard (`libzstd`)** — BSD 3-Clause License. The complete notice is in `studio/qt/third_party/zstd/LICENSE-zstd.txt`.

## Required or optional external software

- **Qt 6 / Qt Quick 3D** — Qt Quick 3D is available under GPL-3.0-only or a commercial Qt license. Community builds of RigReel are distributed under GPL-3.0-only. Binary distributors must satisfy Qt's corresponding-source and notice requirements for the exact Qt build they ship.
- **FFmpeg** — invoked externally for rendering, and also used by Qt Multimedia builds on some platforms. Redistributors must document the exact FFmpeg build and comply with its LGPL/GPL configuration.
- **Blender** — optional external application used to convert imported animation.
- **REFramework** — optional external dependency for live RE4 mode. It is not required by the standalone editor.
- **REasy** — external source of community filename lists and RSZ type metadata. The full REasy application and source tree are intentionally not vendored here. REasy is licensed separately by its authors.
- **Noesis and community Noesis plugins** — development references only. Noesis binaries are not part of this repository or its releases. Any independently reimplemented format logic must retain applicable notices and must not copy code without compatible permission.
- **vgmstream** — optional external audio-decoding tool; not vendored in this repository.

## Release checklist

A binary release must include the applicable license texts and notices for every shipped DLL, plugin, codec, runtime, and asset. It must also provide the complete corresponding source required by GPL components, including the scripts and build information needed to reproduce the distributed build.
