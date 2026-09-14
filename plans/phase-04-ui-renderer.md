# Phase 4 — CNAEXT UI renderer

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-04001` … `STUDIO-04999` and are never reused.

**Purpose.** Draw the Studio UI through CNA's public modern graphics API: text, icons, batching, clipping, render resources and DPI, with no renderer-specific code.

**Exit criteria.** The UI draws correctly and efficiently on every renderer that satisfies the host capability contract, with one implementation.

**Progress:** 0 of 15 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-04001` | Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary | ⬜ | `STUDIO-03014`, `STUDIO-02020` |
| `STUDIO-04002` | Vertex and index buffer management for UI geometry | ⬜ | `STUDIO-04001` |
| `STUDIO-04003` | Draw-call batching by texture, clip rectangle and blend state | ⬜ | `STUDIO-04002` |
| `STUDIO-04004` | Scissor-based clipping, including nested clip stacks | ⬜ | `STUDIO-04002` |
| `STUDIO-04005` | Font atlas construction and glyph rasterization | ⬜ | `STUDIO-04001` |
| `STUDIO-04006` | Text rendering with kerning, and correct baseline and line metrics | ⬜ | `STUDIO-04005` |
| `STUDIO-04007` | Dynamic glyph upload without frame stalls or dropped glyphs | ⬜ | `STUDIO-04005` |
| `STUDIO-04008` | Icon atlas with DPI-appropriate variants | ⬜ | `STUDIO-04005` |
| `STUDIO-04009` | Select and document legally redistributable fonts and icons | ⬜ | — |
| `STUDIO-04010` | Render-resource lifetime and recreation on device loss | ⬜ | `STUDIO-04002` |
| `STUDIO-04011` | Window resize handling without artefacts | ⬜ | `STUDIO-04010` |
| `STUDIO-04012` | Render-target composition for the viewport panel | ⬜ | `STUDIO-04004` |
| `STUDIO-04013` | Screenshot and readback support for visual testing | ⬜ | `STUDIO-04001` |
| `STUDIO-04014` | Rounded rectangles, borders and separators as first-class primitives | ⬜ | `STUDIO-04002` |
| `STUDIO-04015` | Per-renderer smoke test: draw a reference panel and assert non-empty output | ⬜ | `STUDIO-04013` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-04001` — Create the `cna-studio-ui-renderer` module inside the CNA-linking boundary

**Acceptance.** Consumes `UiDrawData` and produces pixels; contains no UI toolkit header, preserving the seam that keeps the core CNA-free

### `STUDIO-04003` — Draw-call batching by texture, clip rectangle and blend state

**Acceptance.** A typical frame issues a small bounded number of draw calls

**Verification.** A test asserts the batch count for a reference panel layout

### `STUDIO-04005` — Font atlas construction and glyph rasterization

**Acceptance.** Studio owns its atlas; `SpriteFont` is constructed through CNA's public CNAEXT constructor (see CNA gap G-04)

### `STUDIO-04007` — Dynamic glyph upload without frame stalls or dropped glyphs

**Acceptance.** Glyphs first needed on a frame that does not draw are still uploaded — the prototype shipped this bug once (legacy ED-119) and it must not return

**Verification.** A regression test for the update/draw phase split

### `STUDIO-04009` — Select and document legally redistributable fonts and icons

**Acceptance.** Licences recorded in `THIRD_PARTY_NOTICES.md`; no proprietary third-party assets of any kind

### `STUDIO-04012` — Render-target composition for the viewport panel

**Acceptance.** Accounts for CNA gap G-03's sampling origin in exactly one place

