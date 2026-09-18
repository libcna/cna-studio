# Phase 10 — Asset pipeline and importing

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-10001` … `STUDIO-10999` and are never reused.

**Purpose.** Import the content a real game needs, incrementally, without absorbing large third-party parsers into Studio itself.

**Exit criteria.** Each supported category imports, reimports without losing settings, and reports failure usefully.

**Progress:** 4 of 14 complete `███░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-10001` | Import settings model, persisted per asset and preserved across reimport | ✅ | `STUDIO-09014` |
| `STUDIO-10002` | Importer plugin interface | ✅ | `STUDIO-10001` |
| `STUDIO-10003` | Texture import: formats, sRGB, mips, compression settings | ⬜ | `STUDIO-10001` |
| `STUDIO-10004` | Model import: glTF (carried forward from the prototype) | ⬜ | `STUDIO-10002` |
| `STUDIO-10005` | Audio import | ⬜ | `STUDIO-10002` |
| `STUDIO-10006` | Font import | ⬜ | `STUDIO-04005` |
| `STUDIO-10007` | Material assets | ⬜ | `STUDIO-19001` |
| `STUDIO-10008` | Shader and effect assets | ⬜ | `STUDIO-22001` |
| `STUDIO-10009` | Animation import | ⬜ | `STUDIO-21001` |
| `STUDIO-10010` | Environment map import and processing | ⬜ | `STUDIO-20001` |
| `STUDIO-10011` | Import jobs are cancellable and report progress accurately | ⬜ | `STUDIO-30001` |
| `STUDIO-10012` | Provenance record for every third-party dependency | ✅ | — |
| `STUDIO-10013` | A failed import reports why, and does not leave a half-imported asset | ⬜ | `STUDIO-10011` |
| `STUDIO-10014` | Decide how Studio decodes an image without a graphics device | ✅ | `STUDIO-10012` |

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

**Done.** `StudioAssetImporter` is the interface; `StudioImporterRegistry` holds them;
`applyImporterFacts` dispatches through it.

**What it replaces.** Importing was a chain of `if (record->type == AssetType::SpriteFont) … if (…
== Model) …` in the middle of the asset system, each branch reaching whatever library it needed.
That works, and it has two properties that get worse with every format: adding one means editing a
function nobody else has business touching, and the function that dispatches ends up naming every
third-party parser Studio has. A plugin cannot edit it at all, which is why `STUDIO-28003` waits on
this.

**The built-ins went through the interface too**, rather than staying a special case beside it. A
dispatch with one path for "ours" and another for "theirs" is two dispatches, and the second one is
the one that rots.

**What is deliberately not on the interface.** How to *edit* an importer's settings: that is a
`ComponentDescriptor`, which the inspector already draws (decision D-05), and two ways to describe
one importer's settings would be one too many. Nor does an importer own the walk, the sidecar write
or the "only write when something actually changed" rule — those are identical for every importer,
and one that got them wrong would fill a repository with spurious diffs.

**The readers did not change.** `applyTextureFacts` and its siblings stay exactly where they were,
beside the libraries they need; what moved is *who decides which one runs*. Rewriting them in the
same change would have made a refactor and a behaviour change indistinguishable in one diff.

**A duplicate id is refused rather than replacing**, and the first importer claiming a *type* wins
in registration order. Two importers answering to one id is a build that behaves differently
depending on which was registered last; an ambiguity resolved by hash order is one that behaves
differently on another machine.

**The structural half of the acceptance is a test.** "A third-party library lives behind one, not in
Studio core" is the half that decays quietly: somebody needs a parser for one thing, includes it
where it is convenient, and a module that was dependency-free is not any more — and nobody notices
until the build breaks somewhere else. `NoThirdPartyLibraryIsReachedFromOutsideTheFilesAllowedToReachIt` scans every `.cpp` and `.hpp` under `src/` and `include/` and fails on an include of a vendored
header outside a four-entry allowlist, each entry a translation unit that owns one library. Checked
by causing it: an added `#include "stb_image.h"` in `AssetShortcuts.cpp` fails the suite by name.

**An asset type nothing claims is left alone** rather than reported as a failure. A project holds
files Studio does not import, and a scan that complained about each of them is a scan nobody reads.

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

**Decided: vendor a CPU decoder.** `third_party/stb/stb_image.h` v2.30, behind
`CNA/Studio/Assets/ImageDecode.hpp`, in `cna-studio-assets` — one of the CNA-free modules — so a
worker thread can call it in a build with no CNA at all.

**Why that one, over the two alternatives this task was filed to weigh.** Writing a PNG decoder is
about a week of work and a *larger* security surface than the one it avoids: a parser nobody else
reads, against a battle-tested one. Accepting thumbnails as a main-thread budget is a smaller
product, and the task said it should be chosen deliberately if it is chosen — it was considered and
not chosen, because the whole of `STUDIO-09003` is "as cancellable background jobs" and a budget is
a different feature wearing the same name.

Vendoring is the smallest of the three here specifically because the precedent is exact rather than
merely similar: this repository already vendors stb_truetype, same author, same collection, same
dual MIT/Unlicense terms, same single-translation-unit-with-internal-linkage arrangement. The rule
that made this a decision rather than a commit — a dependency arrives with its provenance, licence,
version and reason recorded — is now *enforced* by `STUDIO-10012`, and the new dependency went in
through that gate rather than around it.

**The security surface is real and is narrowed rather than waved at.** This is a parser reading
files Studio did not write, which is the one genuinely uncomfortable thing about the decision.

- **Three formats, enabled one at a time** with `STBI_ONLY_PNG`, `STBI_ONLY_JPEG`, `STBI_ONLY_BMP`,
  rather than taking everything the decoder offers. A format nobody imports is attack surface with
  no user. The three are exactly the set `readImageSize` reads headers for, and a test asserts that
  the two agree — an asset that reported a size the editor could not then draw would be a worse bug
  than not reading the format.
- **No file access of its own** (`STBI_NO_STDIO`). Studio reads the bytes and hands over a buffer; a
  decoder that cannot open a file cannot be talked into opening one.
- **Dimensions bounded at 16 384 an edge, before any allocation.** A header claiming two billion
  pixels a side is a few bytes to write and an allocation nobody survives, and arithmetic is a more
  reliable refusal than hoping an allocator fails politely.
- **Failure is always reported.** Empty, truncated, mislabelled, absurd and not-an-image-at-all are
  each a message naming the file. A decoder that aborted the editor on a bad asset would make one
  broken file cost a session.
- **Internal linkage**, so nothing outside the one translation unit can reach the decoder, and a
  second copy linked later cannot collide with it.

Green under AddressSanitizer, which is the configuration that has something to say about a parser,
and under ThreadSanitizer, which is the one that has something to say about the concurrency claim.

**Asserted, not assumed.** `tests/ImageDecodeTests.cpp` builds real PNGs in the test rather than
committing binary fixtures — a fixture is a file nobody can read in a review — decodes one from
eight threads at once, and holds the format policy so that turning the rest of the decoder on is a
test failure rather than a quiet widening.

**What this unblocks.** `STUDIO-09003` (thumbnails as cancellable background jobs), and behind it
`STUDIO-09004`, `STUDIO-09015` and `STUDIO-30025`.

### `STUDIO-10012` — Provenance record for every third-party dependency

**Acceptance.** Provenance, licence, version and reason for inclusion recorded before a dependency is added

**Done, and the point of doing it now is that it is a gate rather than a habit.** `THIRD_PARTY_
NOTICES.md` already carried the reasons for everything vendored — that was written as each
dependency arrived, which is the rule working. What it could not do is fail. A rule of the form
"recorded before it is added" decays the ordinary way: somebody drops a header into `third_party/`
to get a build working, means to write it up, and does not; by the time anybody audits the tree the
file has been there for months and nobody remembers where it came from.

So the facts now live in `third_party/PROVENANCE.tsv` — path, hash, licence, version, origin, one
row per file — and `tests/ThirdPartyProvenanceTests.cpp` walks the directory and fails on a file
that is not listed, a listing for a file that has gone, or a hash that no longer matches. Both
failure modes were checked by causing them. `THIRD_PARTY_NOTICES.md` keeps the reasons, and the test
requires every component to be named there too, so the write-up and the tree cannot drift into
describing different sets of things.

**The audit found one gap and one inconsistency.** `third_party/cgltf/cgltf_prefixed.h` was in the
tree and in nobody's write-up — it is this repository's own file, and "this repository's own" is a
licence statement that has to be made rather than assumed. And cgltf was described as "verbatim
copies of upstream" with no hash, where stb_truetype and the fonts had recorded theirs; every
vendored file now has one, so "verbatim" is checked rather than asserted. The four hashes that were
already recorded all still matched.

**SHA-256 is written out in the test.** Studio's core has no cryptographic dependency and is not
acquiring one in order to check its dependencies, which would be funny in the wrong way. It is sixty
lines of FIPS 180-4 and the known-answer vectors beside it are what make it trustworthy — a hash
subtly wrong would report every vendored file as drifted, which reads as a supply-chain scare rather
than as a bug.

**What this unblocks.** `STUDIO-10014` — how Studio decodes an image with no graphics device — is
the decision that has been waiting on this, because the candidate answer is a vendored CPU decoder
and the rule was that a dependency arrives with its provenance recorded. The rule is now enforceable,
so that decision can be taken on its merits.

