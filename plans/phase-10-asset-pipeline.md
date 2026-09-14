# Phase 10 — Asset pipeline and importing

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-10001` … `STUDIO-10999` and are never reused.

**Purpose.** Import the content a real game needs, incrementally, without absorbing large third-party parsers into Studio itself.

**Exit criteria.** Each supported category imports, reimports without losing settings, and reports failure usefully.

**Progress:** 0 of 13 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-10001` | Import settings model, persisted per asset and preserved across reimport | ⬜ | `STUDIO-09014` |
| `STUDIO-10002` | Importer plugin interface | ⬜ | `STUDIO-10001` |
| `STUDIO-10003` | Texture import: formats, sRGB, mips, compression settings | ⬜ | `STUDIO-10001` |
| `STUDIO-10004` | Model import: glTF (carried forward from the prototype) | ⬜ | `STUDIO-10002` |
| `STUDIO-10005` | Audio import | ⬜ | `STUDIO-10002` |
| `STUDIO-10006` | Font import | ⬜ | `STUDIO-04005` |
| `STUDIO-10007` | Material assets | ⬜ | `STUDIO-19001` |
| `STUDIO-10008` | Shader and effect assets | ⬜ | `STUDIO-22001` |
| `STUDIO-10009` | Animation import | ⬜ | `STUDIO-21001` |
| `STUDIO-10010` | Environment map import and processing | ⬜ | `STUDIO-20001` |
| `STUDIO-10011` | Import jobs are cancellable and report progress accurately | ⬜ | `STUDIO-30001` |
| `STUDIO-10012` | Provenance record for every third-party dependency | ⬜ | — |
| `STUDIO-10013` | A failed import reports why, and does not leave a half-imported asset | ⬜ | `STUDIO-10011` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-10002` — Importer plugin interface

**Acceptance.** Importers are isolated; a third-party library lives behind one, not in Studio core

### `STUDIO-10004` — Model import: glTF (carried forward from the prototype)

**Acceptance.** The existing cgltf-based importer keeps working and gains import settings

### `STUDIO-10012` — Provenance record for every third-party dependency

**Acceptance.** Provenance, licence, version and reason for inclusion recorded before a dependency is added

