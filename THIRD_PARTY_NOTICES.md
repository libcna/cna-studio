# Third-party components

## Dear ImGui

`third_party/imgui/` contains Dear ImGui version 1.92.9b (docking branch), by Omar Cornut and
contributors, licensed under the MIT License. See `third_party/imgui/LICENSE.txt`.

Only Dear ImGui's core is vendored — `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp` and their headers. None of Dear ImGui's own platform or renderer backends are
included or built: cna-editor supplies its own, written against CNA's public API
(`src/viewport/CnaUiRenderer.cpp` and `src/viewport/CnaUiPlatform.cpp`).

## cgltf

`third_party/cgltf/` contains cgltf version 1.15, by Johannes Kuhlmann and contributors, licensed
under the MIT License. See `third_party/cgltf/LICENSE`.

`cgltf.h` and `LICENSE` are verbatim copies of upstream, and are the same version CNA vendors at
`third_party/cgltf/`. It is copied rather than reached for across the sibling checkout because the
default build of this repository has no CNA checkout at all (ANALYSIS.md decision D-03), and the
glTF importer is in `cna-editor-assets`, which is one of the CNA-free modules. CNA's own glTF
reader is not usable here for a second reason: it lives in `CNA::Internal::GltfImport`, and D-01
forbids the editor from reaching into CNA's internals.

`cgltf_impl.cpp` is *not* upstream. cgltf is header-only and requires exactly one translation unit
to define `CGLTF_IMPLEMENTATION`; upstream ships no such file, so that file is this repository's
and carries this repository's licence.
