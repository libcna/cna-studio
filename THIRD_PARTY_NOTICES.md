# Third-party components

## Dear ImGui

`third_party/imgui/` contains Dear ImGui version 1.92.9b (docking branch), by Omar Cornut and
contributors, licensed under the MIT License. See `third_party/imgui/LICENSE.txt`.

Only Dear ImGui's core is vendored — `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp` and their headers. None of Dear ImGui's own platform or renderer backends are
included or built: cna-studio supplies its own, written against CNA's public API
(`src/viewport/CnaUiRenderer.cpp` and `src/viewport/CnaUiPlatform.cpp`).

## cgltf

`third_party/cgltf/` contains cgltf version 1.15, by Johannes Kuhlmann and contributors, licensed
under the MIT License. See `third_party/cgltf/LICENSE`.

`cgltf.h` and `LICENSE` are verbatim copies of upstream, and are the same version CNA vendors at
`third_party/cgltf/`. It is copied rather than reached for across the sibling checkout because the
default build of this repository has no CNA checkout at all (ANALYSIS.md decision D-03), and the
glTF importer is in `cna-studio-assets`, which is one of the CNA-free modules. CNA's own glTF
reader is not usable here for a second reason: it lives in `CNA::Internal::GltfImport`, and D-01
forbids the editor from reaching into CNA's internals.

`cgltf_impl.cpp` is *not* upstream. cgltf is header-only and requires exactly one translation unit
to define `CGLTF_IMPLEMENTATION`; upstream ships no such file, so that file is this repository's
and carries this repository's licence.

## stb_truetype

`third_party/stb/stb_truetype.h` contains stb_truetype version 1.26, by Sean Barrett, dual licensed
under the MIT License and the Unlicense (public domain). See `third_party/stb/LICENSE`.

Fetched verbatim from <https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h>;
SHA-256 `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`.

It is the glyph rasterizer behind `CNA/Studio/UiCore/StudioFontAtlas.hpp`, and it is included by
**exactly one** translation unit, `src/ui-core/StudioFontAtlas.cpp`, with `STBTT_STATIC` so that its
symbols have internal linkage. Dear ImGui vendors its own copy at
`third_party/imgui/imstb_truetype.h`, which `imgui_draw.cpp` includes inside `namespace ImStb`; the
two therefore cannot collide even in a binary holding both. That was verified rather than assumed,
because this repository has already paid once for a duplicate definition that compiled cleanly,
linked cleanly and corrupted memory at run time.

Atlas packing, glyph caching, metrics, kerning lookup, UTF-8 decoding and text layout are Studio's
own code. stb_truetype is used for outline rasterization and table lookup only.

## IBM Plex

`third_party/fonts/` contains three faces of the IBM Plex family, © 2017 IBM Corp. with Reserved
Font Name "Plex", licensed under the SIL Open Font License, Version 1.1. See
`third_party/fonts/OFL-IBMPlex.txt`.

| File | SHA-256 | Upstream |
|------|---------|----------|
| `IBMPlexSans-Regular.ttf` | `975dcda37d80f038dcd143c22e33ca2d97a0cc5a929aace1c749153b0fe1afa5` | <https://raw.githubusercontent.com/IBM/plex/master/packages/plex-sans/fonts/complete/ttf/IBMPlexSans-Regular.ttf> |
| `IBMPlexSans-SemiBold.ttf` | `a20caf8286023a6a7a85e40b1d2a4ae9fc3e3b1f9eda8f4c542dd4986af67bb1` | <https://raw.githubusercontent.com/IBM/plex/master/packages/plex-sans/fonts/complete/ttf/IBMPlexSans-SemiBold.ttf> |
| `IBMPlexMono-Regular.ttf` | `7c6fbddca4b700be918f5f6183d9bd4464fa427fe435f0b480d77fe2bb8c5a43` | <https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf> |

The OFL permits redistribution in a binary without restriction and without attribution in the user
interface, and forbids only selling the fonts on their own and reusing the reserved name "Plex" for
a modified version. Studio does neither: the files are embedded verbatim, and Studio's themes name
their typographic roles (`"Studio Sans"`, `"Studio Mono"`) rather than the vendor, so a theme is
never asserting a font it has modified.

The files are turned into byte arrays at build time by `cmake/EmbedBinary.cmake`; the generated
`.cpp` files are build artefacts and are not committed. **The fonts are embedded rather than loaded
from a data directory**: a tool that cannot draw text until it finds a file shows a blank window
when somebody moves the executable, and text is not optional content.

### Why this family

Kerning decided it. Most modern fonts express kerning as GPOS lookups, and the vendored rasterizer
reads only the common `PairPos` form; IBM Plex Sans uses exactly that form, so its kerning actually
reaches the screen. Open Sans and JetBrains Mono were evaluated first and rejected on measurement:
neither ships a `kern` feature at all, so text set in them has no kerning whatsoever. Beyond that,
one family across the sans and the monospace keeps a log panel and the label above it looking like
one application, and Plex was drawn for interfaces — a large x-height, and a monospace with visibly
distinct `0`/`O` and `1`/`l`/`I`, which an editor's log and identifier columns are read carefully
enough to need.
