# Phase 10 — Asset pipeline and importing

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-10001` … `STUDIO-10999` and are never reused.

**Purpose.** Import the content a real game needs, incrementally, without absorbing large third-party parsers into Studio itself.

**Exit criteria.** Each supported category imports, reimports without losing settings, and reports failure usefully.

**Progress:** 1 of 14 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-10001` | Import settings model, persisted per asset and preserved across reimport | ✅ | `STUDIO-09014` |
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
| `STUDIO-10014` | Decide how Studio decodes an image without a graphics device | 🔬 | `STUDIO-10012` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-10001` — Import settings model, persisted per asset and preserved across reimport

**Most of the model already existed**, and saying so is the honest part: importer descriptors are
`ComponentDescriptor`s (`AssetImporters.hpp`), settings live in the `.cnaasset` sidecar, the
inspector edits them, and `SetImporterSettingCommand` / `ClearImporterSettingCommand` make every
edit undoable. What was missing was the second half of the title — **preserved across reimport** —
because there was no reimport.

**Settings and facts are different things, and only one of them a reimport may touch.** Both are
shaped as importer settings, which is what lets the inspector edit them with no new code, and the
importer marks the facts read-only. A *setting* is what the user chose — a model's `scaleFactor`, a
texture's `generateMipmaps`. A *fact* is what the file says — its pixel size, its triangle count. A
reimport rewrites the facts and leaves everything else alone. The failure this exists to prevent is
the classic one: somebody re-exports a mesh and every import setting in the project silently
reverts.

**The record gained a second stamp, and the first one's documentation was wrong.** `sourceSize` and
`sourceModifiedTime` said "at the last successful import" and had been overwritten by `AssetWatcher`
since `STUDIO-07051`, so they meant "as last seen" and nothing recorded when an asset was actually
imported. They now say what they mean, and `importedSize` / `importedModifiedTime` say the other
thing. Both are maintained without touching the filesystem — the watcher keeps one, a reimport keeps
the other — so *"does this need reimporting"* is arithmetic. That is what lets the Content Browser
mark every row without undoing what `STUDIO-30015` bought.

**Two predicates, because there are two questions.** `studioSourceChangedSinceImport` is the *news*
— the file moved on since Studio read it — and is what a row is marked with. `studioNeedsReimport`
also takes in the asset nobody has ever imported, and is the set "import everything that needs it"
acts on. Collapsing them would put an alarming badge on every asset of a freshly scanned project.

**A reimport is not undoable, deliberately.** Every *document* change is a command (D-06); a
reimport re-reads what is already on disk. Undoing one would restore facts describing a version of
the file that no longer exists — a sidecar claiming a texture is 512×512 when the file is 1024×1024
— and the thing a user might actually want back, their settings, was never touched.

**`applyImporterFacts` gained a per-asset overload**, because re-reading the whole project because
one file changed is the reason a reimport feels like a pause rather than an action. The wholesale
pass now runs over a snapshot of ids rather than over live record pointers, since writing a sidecar
can reallocate the record store underneath a walk holding pointers into it.

**Verification.** `tests/ProjectAndAssetTests.cpp`: a setting surviving two reimports while the fact
it sits beside changes, the stamp surviving a restart so a reopened project does not think every
asset needs importing, asking whether a reimport is due making no filesystem access across a hundred
calls, and a missing file refused with a reason. `tests/StudioContentBrowserTests.cpp` covers the
row marker and the menu.

### `STUDIO-10002` — Importer plugin interface

**Acceptance.** Importers are isolated; a third-party library lives behind one, not in Studio core

### `STUDIO-10004` — Model import: glTF (carried forward from the prototype)

**Acceptance.** The existing cgltf-based importer keeps working and gains import settings

### `STUDIO-10014` — Decide how Studio decodes an image without a graphics device

**Found by `STUDIO-09003`**, which cannot be done without it, and filed rather than worked around.

**What Studio can do today.** `readImageSize` reads a PNG, BMP or JPEG *header* — enough for the
pixel size, and nothing at all of the pixels. The only thing in the project that turns an image file
into pixels is `CnaStudioViewport::readImageFile`, which goes through CNA's `Texture2D` and
therefore through the graphics device. A device is not thread-safe and belongs to the main thread,
so nothing decodes an image off the frame.

**Why that blocks thumbnails.** `STUDIO-09003` is "thumbnail generation as cancellable background
jobs". The job system exists (`STUDIO-30001`) and is not the problem: the *work* a thumbnail job
would do is decode-and-downscale, and on the one build that can decode, decoding must happen on the
thread that owns the device. A job that read the bytes and handed them to the main thread to decode
would move the file read off the frame and leave the expensive half on it — worth something, and not
what the task says.

**The decision, which is a decision rather than an implementation.** A CPU-side decoder means a
third-party dependency (stb_image is the obvious candidate), and the rule is that a dependency
arrives with its provenance, licence, version and reason recorded — `STUDIO-10012`. The alternatives
are writing a PNG decoder, which is a week of work and a security surface, or accepting that
thumbnails are a main-thread budget rather than a background job, which is a smaller product and
should be chosen deliberately if it is chosen.

**Acceptance.** Studio can turn a PNG on disk into pixels with no graphics device, from a worker
thread, or the alternative has been chosen and `STUDIO-09003` has been rewritten to match.

### `STUDIO-10012` — Provenance record for every third-party dependency

**Acceptance.** Provenance, licence, version and reason for inclusion recorded before a dependency is added

