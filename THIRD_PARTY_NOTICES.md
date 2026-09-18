# Third-party components

Every file under `third_party/` is listed in `third_party/PROVENANCE.tsv` with its origin, licence,
version and hash, and `tests/ThirdPartyProvenanceTests.cpp` fails the build when the tree and that
list disagree — a file vendored without a record, a record for a file that has gone, or a vendored
file that has drifted from the hash it was recorded with (`plan.md` STUDIO-10012).

That file is the machine-checkable half. This one is where the *reasons* live, which is what an
audit actually needs: a licence file says what may be done with a dependency and cannot say why it
is here, or what was considered instead.

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

`cgltf_prefixed.h` is not upstream either, and for a sharper reason. CNA vendors the same cgltf and
compiles its implementation, and cgltf declares its whole API inside `extern "C"` — so a build that
links both defines the same unmangled symbols twice and fails at the link with "multiple definition
of cgltf_parse". A namespace does not help: `extern "C"` is the instruction to ignore one. That
header renames cgltf's public symbols so the two copies cannot collide, and the file's own comment
explains why every caller must include it rather than `cgltf.h`.

## stb_truetype

`third_party/stb/stb_truetype.h` contains stb_truetype version 1.26, by Sean Barrett, dual licensed
under the MIT License and the Unlicense (public domain). See `third_party/stb/LICENSE`.

Fetched verbatim from <https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h>;
SHA-256 `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`.

It is the glyph rasterizer behind `CNA/Studio/UiCore/StudioFontAtlas.hpp`, and it is included by
**exactly one** translation unit, `src/ui-core/StudioFontAtlas.cpp`, with `STBTT_STATIC` so that its
symbols have internal linkage. `STBTT_STATIC` earned its place rather than being a habit: Dear ImGui
used to vendor its own copy at `third_party/imgui/imstb_truetype.h`, included by `imgui_draw.cpp`
inside `namespace ImStb`, and the two were verified not to collide even in a binary holding both --
verified rather than assumed, because this repository had already paid once for a duplicate
definition that compiled cleanly, linked cleanly and corrupted memory at run time. Dear ImGui itself
is gone (`STUDIO-07030`, `STUDIO-07031`), so there is no longer a second copy to collide with; the
internal linkage stays regardless, since nothing about this translation unit's own reasons for it
changed.

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

## Icons — none

Studio vendors no icon set, and this section exists so that the absence is a recorded decision
rather than an omission somebody later "fixes" by adding one.

The obvious route is an icon font: another few hundred kilobytes, another licence to track, a second
atlas to manage, and a visual language designed for somebody else's product. Studio instead *draws*
its icons as vector paths in code (`src/ui-core/StudioIcons.cpp`), over the primitives the draw list
already has. Each is authored on a 16-unit grid and mapped onto whatever rectangle it is asked for,
so the same definition is crisp at a 14-pixel toolbar and at 32 pixels on a 200% display — rather
than at whichever sizes somebody baked.

There is therefore nothing here to attribute, nothing to redistribute, and no licence that could
change under us. See `plan.md` `STUDIO-04008` and `STUDIO-04009` for the reasoning in full.
