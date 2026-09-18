# Phase 10 — Asset pipeline and importing

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-10001` … `STUDIO-10999` and are never reused.

**Purpose.** Import the content a real game needs, incrementally, without absorbing large third-party parsers into Studio itself.

**Exit criteria.** Each supported category imports, reimports without losing settings, and reports failure usefully.

**Progress:** 6 of 14 complete `█████░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-10001` | Import settings model, persisted per asset and preserved across reimport | ✅ | `STUDIO-09014` |
| `STUDIO-10002` | Importer plugin interface | ✅ | `STUDIO-10001` |
| `STUDIO-10003` | Texture import: formats, sRGB, mips, compression settings | ✅ | `STUDIO-10001` |
| `STUDIO-10004` | Model import: glTF (carried forward from the prototype) | ✅ | `STUDIO-10002` |
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

### `STUDIO-10003` — Texture import: formats, sRGB, mips, compression settings

**Acceptance.** The four settings exist, mean something specific, survive a reimport, and the
combinations that cannot be honoured are said rather than silently resolved.

**What the settings are.** `outputFormat` (`Color` or `DxtCompressed`) and `srgbRead` are new;
`generateMipmaps` and `premultiplyAlpha` were already there and are unchanged. `outputFormat` is
spelled the way XNA's `TextureProcessorOutputFormat` spells it, and XNA's third option, `NoChange`,
is deliberately missing: it means "keep the source bitmap's own format", and the only decoder
Studio has produces eight-bit RGBA whatever it is handed — so it would be a choice with one
outcome, which is a place for a user to look for behaviour that is not there.

**`srgbRead` is off by default, and that is XNA's answer rather than the industry's.** Unity and
Unreal both default their sRGB checkbox *on*, because both render in linear space. XNA renders in
gamma space: its textures are sampled as the bytes stand, and `SurfaceFormat.Color` is what every
existing XNA game expects. Defaulting it on would change what faithful content looks like, so it is
opt-in — named after what it *does* (does the hardware convert on read) rather than after what the
data *is*, because the second reading is the one that gets normal maps wrong.

**The resolved outcome is a function, not a sidecar field.** `studioPlanTextureImport` takes the
settings and the facts and answers with a `SurfaceFormat` name, a mip count, a byte estimate and a
list of notes. It is pure — no filesystem, no device, no state — so the inspector calls it every
frame and stores nothing. Writing a resolved format into the sidecar was the obvious alternative
and is the wrong one: it is a *derivation* of two things that change independently, and a project
would end up carrying a `mipLevels: 11` next to a "Generate Mipmaps" that was unticked half an hour
ago. That is the same failure the fact/setting split exists to prevent, one level up.

**Three combinations cannot be given, and each one says so.**

- **DXT on an edge that is not a multiple of four** resolves back to `Color`. Padding is a decision
  about somebody else's art — a row of invented pixels that shows up as a seam when it is sampled —
  and the note names the size, because "this texture" is not findable and "100 x 62" is.
- **An opaque texture that wants hardware sRGB and compression** is a **CNA gap**, recorded as one
  rather than worked around invisibly. `SurfaceFormat` has `Dxt5SrgbEXT` and `Bc7SrgbEXT` and *no*
  sRGB DXT1, although D3D and OpenGL both define one (`BC1_UNORM_SRGB`,
  `COMPRESSED_SRGB_S3TC_DXT1_EXT`). So Studio gives it `Dxt5SrgbEXT` — correct colour, twice the
  size — and the note says what the alternative is, because a user reading "CNA has no sRGB DXT1"
  has no way to know the setting they can actually reach is the one next to it.
- **A file whose header Studio cannot read** gets *no* format at all. This is the one it would be
  easy to get wrong: resolving one anyway looks harmless, since the format does not depend on the
  size — but it depends on the alpha channel, and an unmeasured source's `hasAlphaChannel` is a
  struct default rather than a fact. "Dxt1" reads as a decision somebody can rely on, and that one
  would be a coin toss; "0 bytes" reads as "this costs nothing".

**Two facts were added to read those settings against.** `sourceFormat` and `sourceAlpha`, both
read-only, both from the file's own magic bytes rather than its name — so a renamed file reports
what it actually is. `readImageSize` grew into `readImageDescription` and became a wrapper over it,
because two readers that could disagree about a size is a bug waiting to be written. The alpha
question is answered from the encoding, not the pixels: whether *any* pixel uses alpha needs a
decode, and the question that matters here — may this be DXT1 — the encoding settles on its own. A
paletted PNG is the one case the fixed header cannot answer, since its transparency lives in an
optional `tRNS` chunk, so that chunk is walked for, bounded by the first `IDAT`.

**The byte estimate walks the chain rather than multiplying by four thirds.** A DXT mip chain does
not bottom out at a byte: its unit is a 4x4 block, so the last three levels of a 1024-square
texture cost a whole block each however few pixels are in them. That is the part a hand-rolled
estimate gets wrong, and the number it produces — "what does this occupy on the GPU" — is the
answer to "why is this build nine hundred megabytes", which nothing in the editor could answer
before.

**What Studio still does not do.** Compress anything. Block compression is the content build's work
in CNA, and moving it into the editor would be the responsibility shift this project forbids. What
is new is that a user can see, before the build, what their settings will produce — including the
cases where what they asked for cannot be given to them.

**Verification.** `tests/TextureImportTests.cpp` covers the mip arithmetic, the defaults a missing
sidecar field reads back as (the failure being prevented is premultiplied alpha silently turning
*off* for every asset nobody has touched), each of the three refusals, the byte estimates against
worked constants, and the header reader across six colour types and depths including a renamed
file. `tests/StudioDetailsPanelTests.cpp` covers the inspector following a setting on the very next
frame with nothing reimported and nothing written — which is the property that makes the plan a
derivation rather than a fact.

**One existing test had to reach further, and was not relaxed.**
`AnOverriddenImportSettingCanBeResetFromTheInspector` sweeps the inspector for the reset control.
Four more property rows made the panel taller than its viewport, which gave it a scrollbar — and
the sweep had been running down the bare right edge, which is now the scrollbar's track. It now
sweeps both sides of the track and scrolls between screenfuls. The assertion is untouched; what
changed is that it can no longer pass or fail on how many settings the importer happens to declare
that week.

### `STUDIO-10004` — Model import: glTF (carried forward from the prototype)

**Acceptance.** The existing cgltf-based importer keeps working and gains import settings

**One of the settings it already had did nothing, and that is what this task mostly was.**
`ImporterIds::kModel` declared `importMaterials`, `loadModel` read it, and the two places that
build a `ModelImportSettings` out of a sidecar — `MeshCache` and `Detail::applyModelFacts` — each
read `scaleFactor` by hand and neither read `importMaterials`. So the checkbox was editable, was
persisted, and reached the importer from nowhere. Two hand-rolled readers that each forget a
different field is the whole argument for `ModelImportSettings::fromJson`, which is now the only
one and which both call sites use.

**`importAnimations` was removed rather than kept honest.** It was declared, documented as not read
and read by nothing. A tooltip explaining that a control does nothing is not the same as a user
finding that out, and a control that does nothing teaches them the editor lies — which is worth
more than the gap it stands in for. In its place the importer reports `animationCount`: how many
animations the file carries, which is a true statement, is the number somebody needs before
deciding whether Studio's lack of skeletal animation (`STUDIO-21001`) matters to them, and costs one
field read of something cgltf has already parsed. The setting comes back when the code that reads
it does.

**`skippedPrimitives` stopped being invisible.** `ModelImportResult` has counted primitives left out
for not being triangles since the importer was written, and `ModelImport.hpp`'s own header comment
says a model that silently loses a third of itself is the kind of bug found in a shipped game — but
the count went into a struct nobody read. It is now a fact in the sidecar and a row in the
inspector, so "0 triangles beside a 4 MB file" has a companion diagnosis for the partial case.
A *failed* import is still `STUDIO-10013`'s; this is the successful import that quietly dropped
something.

**The one genuinely new setting is `normals`.** `Import` keeps the file's own, computing flat ones
only where a part carries none — which is the old behaviour and the right default, since smoothing
groups and split edges are decisions an artist made. `Calculate` throws them away and computes
flat ones everywhere, for the file whose normals are *there* and wrong: exported inside-out, or
left as zeroes by a converter, which is otherwise a trip back through the authoring tool. The
ordering in `loadModel` matters and is asserted: `Calculate` must not merely act as a fallback for
a missing `NORMAL` attribute, because the file it exists for is the one that has one.

**The facts are gathered with the settings, and that is deliberate.** A vertex count taken with the
file's normals beside a `Normals: Calculate` that triples it, or a material count of one beside an
unticked `Import Materials`, would each be two answers to one question. The facts describe what
*this asset* imports as, not what the file would yield to somebody else's settings — the same rule
`scaleFactor` and `modelSize` already followed.

**Verification.** `tests/ModelImportTests.cpp`: the defaults a missing sidecar field reads back as
(`importMaterials` is `true` when absent, and a reader treating absence as zero would strip the
materials off every model nobody had touched); `importMaterials` off reaching both the facts and
the loader; `Calculate` fixing normals that point into the surface while `Import` leaves them
wrong; the animation and skipped-primitive counts through `readModelDescription` and into a
sidecar. `EverySettingTheModelImporterDeclaresIsOneTheImporterReads` walks the descriptor, changes
each editable property away from its default and requires `fromJson` to notice — the policy rather
than the one instance, so the next dead setting is caught too. Checked by causing it: adding an
`importAnimations` back fails that case by name.

**The glTF fixture gained animations.** Each is a valid one-channel animation whose sampler reads
its times out of the positions buffer view, so the fixture's buffer needs nothing added — the count
is the only thing under test, and an animation that failed `cgltf_validate` would fail the whole
load instead of testing anything.

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

