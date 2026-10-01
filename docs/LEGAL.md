# Legal and distribution notes

This document is practical project guidance, not legal advice.

## Project identity

RigReel Studio is an unofficial interoperability and creative-tool project. It is not affiliated with or endorsed by Capcom or any game publisher. Game and engine names should be used only to identify compatibility.

## Game content

The repository and release archives must not contain game PAKs, extracted models, textures, materials, audio, animations, maps, video, proprietary keys, or other copyrighted game content. Users supply their own legally obtained installation, and the software reads files locally.

Screenshots and demonstration videos should be selected deliberately, carry the project disclaimer, and follow the applicable publisher/platform fan-content rules. They must never be used as a substitute for redistributing source assets.

## Application license

Community builds use Qt Quick 3D, which Qt offers under GPL-3.0-only or a commercial license. RigReel's original code is therefore published under GPL-3.0-only. A distributor using a commercial Qt license should obtain independent advice before offering RigReel under different terms.

## Third-party provenance

Before accepting copied or adapted code, record its upstream URL, author, exact license, and the commit or release used. Reverse-engineered behavior may be reimplemented independently, but comments such as “based on” or “follows” another tool require a provenance review before release.

See `THIRD_PARTY_NOTICES.md` for the current inventory. Every binary release needs its own audit because deployment tools may add Qt, FFmpeg, compiler-runtime, and platform DLLs that are not present in the source tree.
