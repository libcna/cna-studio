# Phase 1 — Product rename

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-01001` … `STUDIO-01999` and are never reused.

**Purpose.** Turn the imported prototype into CNA Studio at the level of identity: build targets, executable, public API, user-visible text and documentation.

**Exit criteria.** Nothing in the repository presents itself as "cna-editor" except deliberately pinned serialized contracts, and the suite is still green.

**Progress:** 13 of 16 complete `██████████░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-01001` | Rename the CMake project and all library targets | ✅ | `STUDIO-00008` |
| `STUDIO-01002` | Rename the main executable to `cna-studio` | ✅ | `STUDIO-01001` |
| `STUDIO-01003` | Rename the build options `CNA_EDITOR_*` to `CNA_STUDIO_*` | ✅ | `STUDIO-01001` |
| `STUDIO-01004` | Migrate the public namespace `CNA::Editor` to `CNA::Studio` | ✅ | `STUDIO-01001` |
| `STUDIO-01005` | Move the public include root to `include/CNA/Studio/` | ✅ | `STUDIO-01004` |
| `STUDIO-01006` | Rename `Editor*`-prefixed public types to `Studio*` | ✅ | `STUDIO-01004` |
| `STUDIO-01007` | Pin the serialized contracts that must NOT be renamed | ✅ | `STUDIO-01006` |
| `STUDIO-01008` | Rename the vendored cgltf symbol prefixes | ✅ | `STUDIO-01001` |
| `STUDIO-01009` | Rewrite user-visible text: help, diagnostics, log lines, error messages | ✅ | `STUDIO-01002` |
| `STUDIO-01010` | Rewrite the README around the CNA Studio product | ✅ | `STUDIO-01009` |
| `STUDIO-01011` | Update documentation and rename documentation image assets | ✅ | `STUDIO-01009` |
| `STUDIO-01012` | Update the CI workflow for the renamed targets | ✅ | `STUDIO-01001` |
| `STUDIO-01013` | Audit that no legacy identifier survives anywhere outside history and pinned formats | ✅ | `STUDIO-01008`, `STUDIO-01009` |
| `STUDIO-01014` | Decide and document the compatibility-shim policy for the renamed public API | ⬜ | `STUDIO-01006` |
| `STUDIO-01015` | Rename the state and configuration directory the application writes to | ⬜ | `STUDIO-01002` |
| `STUDIO-01016` | Retire `ANALYSIS.md` in favour of `docs/ARCHITECTURE.md` | 🔄 | `STUDIO-01010` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-01001` — Rename the CMake project and all library targets

**Acceptance.** `project(CnaStudio)`; 12 `cna-studio-*` libraries; no `cna_editor`/`CNA_EDITOR` identifiers remain

**Verification.** Clean configure from an empty build tree

### `STUDIO-01002` — Rename the main executable to `cna-studio`

**Acceptance.** `./build/cna-studio` exists and runs; `cna-player` keeps its name, being the game player rather than the editor

### `STUDIO-01003` — Rename the build options `CNA_EDITOR_*` to `CNA_STUDIO_*`

**Acceptance.** All six options renamed and documented in the README

### `STUDIO-01004` — Migrate the public namespace `CNA::Editor` to `CNA::Studio`

**Acceptance.** No `CNA::Editor` remains; the whole tree compiles

### `STUDIO-01005` — Move the public include root to `include/CNA/Studio/`

**Acceptance.** Moved with `git mv` so per-file history survives

### `STUDIO-01006` — Rename `Editor*`-prefixed public types to `Studio*`

**Acceptance.** `StudioApplication`, `StudioContext`, `StudioCommand`, `StudioUi`, `StudioPanel`, `StudioEntity`, `StudioViewport` and the rest; 94 files moved with `git mv`

### `STUDIO-01007` — Pin the serialized contracts that must NOT be renamed

**Acceptance.** `"editorState"` (scene) and `"editorApiVersion"` (plugin manifest) unchanged on disk and in the wire format, with their C++ carriers renamed; documented in `docs/FORMATS.md` and the README

**Verification.** Round-trip tests over existing `.cnascene` and plugin manifests still pass

### `STUDIO-01008` — Rename the vendored cgltf symbol prefixes

**Acceptance.** `cna_editor_cgltf_*` becomes `cna_studio_cgltf_*`; the ODR-isolation property is preserved

### `STUDIO-01009` — Rewrite user-visible text: help, diagnostics, log lines, error messages

**Acceptance.** No user-visible string calls the product an editor; `--help` describes an authoring environment

**Verification.** Three tests that pinned the old strings updated to the new wording rather than loosened

### `STUDIO-01010` — Rewrite the README around the CNA Studio product

**Acceptance.** Leads with the "produces CNA games, not CNA Studio games" rule; documents the real build options and the real baseline numbers

### `STUDIO-01011` — Update documentation and rename documentation image assets

**Acceptance.** `docs/images/editor-*.png` moved to `studio-*.png`; `docs/FORMATS.md` corrected where a search-and-replace had mis-stated the pinned JSON keys

### `STUDIO-01012` — Update the CI workflow for the renamed targets

**Acceptance.** The Linux GCC/Clang matrix builds and tests the renamed tree

### `STUDIO-01013` — Audit that no legacy identifier survives anywhere outside history and pinned formats

**Acceptance.** A repository-wide grep finds `cna-editor`/`CNA_EDITOR`/`CnaEditor`/`CNA::Editor` only in `ANALYSIS.md`, `NEXT.md` and the legacy task map, all of which are historical records

### `STUDIO-01014` — Decide and document the compatibility-shim policy for the renamed public API

**Acceptance.** Either a documented set of deprecated `CNA::Editor` aliases with a removal date, or a recorded decision that none are provided because no external consumer exists yet. Silence is not an answer

### `STUDIO-01015` — Rename the state and configuration directory the application writes to

**Acceptance.** Preferences and workspace state live under a Studio-named directory, with a one-time migration from the prototype location that reports what it moved

**Verification.** Test: a prototype-era state directory is migrated, and a second run does not re-migrate

### `STUDIO-01016` — Retire `ANALYSIS.md` in favour of `docs/ARCHITECTURE.md`

**Acceptance.** `ANALYSIS.md` gains a header marking it historical and pointing at the current document; its still-valid decisions are restated in `docs/ARCHITECTURE.md` rather than edited in place

