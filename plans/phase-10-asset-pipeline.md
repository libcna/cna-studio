# Phase 10 — Asset pipeline and importing

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-10001` … `STUDIO-10999` and are never reused.

**Purpose.** Import the content a real game needs, incrementally, without absorbing large third-party parsers into Studio itself.

**Exit criteria.** Each supported category imports, reimports without losing settings, and reports failure usefully.

**Progress:** 10 of 15 complete `████████░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-10001` | Import settings model, persisted per asset and preserved across reimport | ✅ | `STUDIO-09014` |
| `STUDIO-10002` | Importer plugin interface | ✅ | `STUDIO-10001` |
| `STUDIO-10003` | Texture import: formats, sRGB, mips, compression settings | ✅ | `STUDIO-10001` |
| `STUDIO-10004` | Model import: glTF (carried forward from the prototype) | ✅ | `STUDIO-10002` |
| `STUDIO-10005` | Audio import | ✅ | `STUDIO-10002` |
| `STUDIO-10006` | Font import | ✅ | `STUDIO-04005` |
| `STUDIO-10007` | Material assets | ⬜ | `STUDIO-19001` |
| `STUDIO-10008` | Shader and effect assets | ⬜ | `STUDIO-22001` |
| `STUDIO-10009` | Animation import | ⬜ | `STUDIO-21001` |
| `STUDIO-10010` | Environment map import and processing | ⬜ | `STUDIO-20001` |
| `STUDIO-10011` | Import jobs are cancellable and report progress accurately | ✅ | `STUDIO-30001` |
| `STUDIO-10012` | Provenance record for every third-party dependency | ✅ | — |
| `STUDIO-10013` | A failed import reports why, and does not leave a half-imported asset | ✅ | `STUDIO-10011` |
| `STUDIO-10014` | Decide how Studio decodes an image without a graphics device | ✅ | `STUDIO-10012` |
| `STUDIO-10015` | Measure an MP3 and a FLAC as exactly as a WAV and an Ogg | ⬜ | `STUDIO-10005` |

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

### `STUDIO-10005` — Audio import

**Acceptance.** A clip's own facts are read without a sound device, both audio types have an
importer, and no declared setting is one nothing reads.

**There was no audio importer at all.** `registerBuiltinAssetImporters` held Texture, SpriteFont and
Model, so a `.wav` and an `.ogg` were tracked, given an importer id and never asked a single
question. And `ImporterIds::kSong` had a type id with no descriptor behind it: `AssetDatabase` has
assigned `CNA.SongImporter` to every `.ogg`, `.mp3` and `.flac` since it was written, and nothing
registered one — so the inspector told a user that the importer for their music "is not registered
in this build", which is true and reads as a broken installation.

**The facts are the same for both types, so they are one list.** The split between `SoundEffect` and
`Song` is about how a *game* uses a clip — one is loaded and fired, the other streamed through the
media player — and not about what is in the file, which answers identical questions either way.

**`decodedBytes` is the number nothing in the editor could answer.** A file size does not say what a
clip costs: a three-minute Ogg is four megabytes on disk and forty in memory. "Load Into Memory" is
a decision somebody has been making without it, and the fact next to the setting is what makes it an
informed one — the same move the texture importer's "On The GPU" makes.

**`importVolume` was a number that changed nothing.** Declared, editable, persisted, and read by
nothing. The inspector's preview now plays at it, so auditioning a clip plays it at the volume the
setting claims — which is the one thing an audio editor is least able to get away with being wrong
about. `loadIntoMemory` stays a recorded instruction for the content build, and its tooltip now says
so rather than implying Studio streams.

**WAV and Ogg Vorbis, and MP3 and FLAC said to be unmeasurable.** A WAV's chunks are *walked*, not
assumed: `fmt ` and `data` are not at fixed offsets and a file written by a DAW routinely carries a
`LIST` or `bext` between them, so a reader that assumed `data` at offset 36 reads its length out of
the middle of somebody's metadata. A Vorbis stream's length is not stated anywhere at all — it is
the granule position of the *last* page, so the reader takes the end of the file and scans backwards
for the last page belonging to the first page's stream, rather than walking tens of thousands of
pages from the front for one number. An Ogg carrying something that is not Vorbis (Opus, Theora) is
left unmeasured rather than read as Vorbis. MP3's length genuinely is not in its header when it is
variable-bitrate, and a duration wrong by a factor of two is worse than one that is absent —
`STUDIO-10015` is the rest.

**`bitsPerSample` is zero for a compressed format, and that is not "unknown".** A Vorbis stream
stores coefficients rather than samples; reporting "16" because that is what it decodes to would be
an answer to a question nobody asked.

**Verification.** `tests/AudioImportTests.cpp`: a WAV's rate, channels, depth and length; the same
clip with 2 KB of junk between its chunks reading identically, which is the case a fixed-offset
reader fails; an Ogg's length coming from the last page rather than the first or an intermediate
one; an Opus-in-Ogg and an MP3 reported as unmeasured; a renamed file reporting what it is; the
defaults a missing sidecar field reads back as (an `importVolume` that read back as zero would make
every untouched clip preview as silence, which is indistinguishable from a broken device); both
types' facts reaching their sidecars and not being rewritten on a second pass.
`EverySettingAnAudioImporterDeclaresIsOneSomethingReads` holds both to the policy the model importer
is held to. `tests/StudioAudioPreviewTests.cpp` covers the preview playing at the asset's import
volume. Checked by causing each: a backward scan turned forwards, a bit-depth offset moved by two, a
declared setting nothing reads, and the preview put back to 1.0 each fail by name.

### `STUDIO-10015` — Measure an MP3 and a FLAC as exactly as a WAV and an Ogg

**Found while doing `STUDIO-10005`**, filed rather than half-done. Both are `AssetType::Song`, both
are formats a project really holds, and both are reported as unmeasured today — which is honest and
is not the same as answered.

**FLAC is the easy half and was still left out**, deliberately: its `STREAMINFO` block states the
sample rate, the channel count and the total sample count outright, so it is a bit-unpacking
exercise and nothing more. It was not folded into `STUDIO-10005` because that task's shape was "read
the two formats the two asset types are actually made of", and a third parser added on momentum is
how a task stops having an edge.

**MP3 is the hard half and is where the design question is.** A constant-bitrate MP3's length is
arithmetic on the file size; a variable-bitrate one's is in a `Xing` or `VBRI` header *if the
encoder wrote one*, and is otherwise only knowable by walking every frame — which for a long track
is the whole file. So the task has to decide what to do when the header is absent: walk it (exact,
slow, and on the scan path), estimate from the first frame (fast and wrong for exactly the files
VBR is used for), or report it unmeasured (what happens now). A guess stated as a fact is the one
option ruled out.

### `STUDIO-10013` — A failed import reports why, and does not leave a half-imported asset

**Acceptance.** A file that cannot be read produces a reason a person can act on, in a place they
will see it, and the asset is left exactly as it was.

**There were two answers where there needed to be three.** `gatherFacts` returned facts or nothing,
and "nothing" covered both *I do not claim this file* and *I claim it and it is broken*. Those are
opposite situations: the first is most of the files in a project and reporting each of them makes a
list nobody reads; the second is a file somebody put there on purpose and needs to hear about. So a
texture truncated by a failed copy imported as quietly as a readme. `StudioImportedFacts` now says
which of the three it is — read, declined, or failed with a reason.

**The reasons mostly already existed and were being thrown away.** `loadModel` has said "a .bin file
beside it may be missing" since it was written, and `readModelDescription` collapsed that to a
`std::nullopt`. Carrying the first warning out is most of what the model side of this task was.
The image and audio readers needed the distinction *made*: both now set a reason only when the file
announced itself as one of their formats and then could not be read — a PNG signature with no IHDR
behind it, a RIFF/WAVE with no `fmt ` chunk, an Ogg whose stream is Opus rather than Vorbis. A file
that never claimed to be one of those leaves the reason empty and is declined in silence.

**Three things are deliberately *not* failures.** A format Studio cannot measure yet (MP3, FLAC —
`STUDIO-10015`) is a gap in the editor, and a warning would blame the file. An asset whose file is
not on disk is skipped entirely: that is a state of its own, marked on its row and repairable from
the inspector (`STUDIO-09013`), and saying it again in different words would put two complaints in
front of somebody about one problem with one fix. And a glTF that loads while dropping some
primitives is a *success* with a `skippedPrimitives` count — the geometry that did come across is
real and worth having.

**The sprite-font importer has no failure mode, and that is the honest answer rather than a gap.** A
`.spritefont` is XML the content pipeline writes; a file without `<Asset` and `FontDescription` is
not a broken sprite font, it is not a sprite font. Anything past that structural check reads as
absent fields, not as an error. Inventing a failure for symmetry would add a warning nobody can act
on.

**A failed import leaves the record untouched, including its stale facts.** Nothing is half-written
because the smallest thing the queue applies is one asset's whole facts object — the property comes
from `STUDIO-10011`'s split rather than from care at the call site. Keeping the old facts is a
decision: they describe the file as it last read, which is more use than nothing, and the failure is
*reported*, which is what makes keeping them honest rather than misleading. Clearing them would
answer "how big was this texture" with silence at exactly the moment somebody is trying to work out
what broke.

**The reason travels rather than being logged where it was found.** A worker is handed a job context
and nothing else, so it has no log and no business having one. The queue keeps failures and
`takeFailures` hands them over once — a caller that read the list every frame would write the same
broken texture to the log sixty times a second.

**Verification.** `tests/ImportJobTests.cpp` covers the declined/failed split through the queue, a
failed import leaving both the stale facts and the user's own settings exactly as they were, an
asset with no file being skipped rather than failed, and — end to end through the real shell — a
truncated PNG producing one warning naming the file and the reason, and still one warning fifty
frames later. `tests/TextureImportTests.cpp`, `tests/AudioImportTests.cpp` and
`tests/ModelImportTests.cpp` each cover their own format's broken-versus-not-mine cases. Checked by
causing it: making the image reader drop its reason again fails eight cases across three files.

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

### `STUDIO-10006` — Font import

**Acceptance.** A `.ttf` or `.otf` in a project is recognised as a typeface, reports what its own
tables say, and offers the decisions a build has to make about it.

**What it was doing before is the point.** `.ttf` was `AssetType::SpriteFont`, so a font dropped in a
project was handed the sprite-font importer — which looks for `<Asset … FontDescription>` in what is
a binary file, found none, and declined. Nothing failed. Nothing was reported. The inspector showed
five empty read-only fields, and the font simply did not import.

**A `.spritefont` and a `.ttf` are opposite kinds of thing**, which is why one type could never
serve both. A `.spritefont` is the content pipeline's own *description*: it already declares the
font, the size, the spacing and the character range, so every one of its fields is read-only and an
editable copy would be a second answer to a settled question. A `.ttf` settles none of that — it is
the typeface, and the size and the range are decisions nobody has made. One is all facts; the other
is mostly settings. So `AssetType::Font` and `CNA.FontImporter` exist, and `.spritefont` and `.fnt`
keep theirs.

**The tables are read by hand rather than through stb_truetype.** `StudioFontAtlas.cpp` owns that
library and is one of four translation units allowed to reach a vendored one at all, which
`tests/AssetImporterTests.cpp` enforces. Widening that list to read four numbers out of a
fixed-layout table directory would be the wrong trade: the sfnt directory is as simple as a PNG's
IHDR, and this sits beside `readImageDescription` and `readAudioDescription` doing exactly what they
do. Rasterising a glyph is the part that needs a library, and nothing here rasterises.

**`hasKerning` is reported because the setting beside it is otherwise untestable by eye.** A font
with no `kern` and no `GPOS` kerns identically whichever way the box is ticked, and a user toggling
it and seeing nothing happen has no way to tell that from a bug.

**Names are decoded, not copied.** The `name` table is UTF-16BE; a reader that took one byte a
character would turn a family name into interleaved nulls — a silent corruption of somebody's name,
in the one field of that inspector that is theirs rather than the editor's. Windows entries are
preferred and Macintosh ones are the fallback, which is the order fonts carry them in.

**An existing project's fonts are retyped on load, and that is not a format migration.** A sidecar
written by an older build says `"SpriteFont"` for a `.ttf`. Left alone it would keep the wrong
importer for ever — this task's own bug, preserved by the fix for it. The correction is in
`recordFromJson` rather than in `getAssetFormatMigrator`, for two reasons: nothing about the *file
format* changed, so there is no version to bump, and a migration step sees only the parsed JSON —
the sidecar does not record the path, because it sits beside the file. The id survives, which is the
thing scenes reference (D-08); the importer id follows only when it is still the old type's default,
so a project pointing a font at something else on purpose keeps doing so.

**The settings are recorded, not acted on, and the plan says so rather than implying otherwise.**
Studio rasterises nothing: `pointSize`, the character range, `spacing` and `useKerning` are the
instruction a content build needs, exactly as a `.spritefont`'s `<Size>` is. `fromJson` is their
single reader and a policy case holds the importer to declaring nothing it does not read — the same
bar `STUDIO-10004` set for the model importer.

**Verification.** `tests/FontImportTests.cpp` builds real sfnt files rather than committing binary
fixtures: the family, style, glyph count and design grid out of the tables; `OTTO` and `ttcf` read as
OpenType and a collection; a non-ASCII family name surviving the UTF-16 decode; a Macintosh-only
name table read rather than reported as nameless; a truncated font reported with a reason while a
readme is declined in silence (`STUDIO-10013`); a `.ttf` typed as `Font` while a `.spritefont` keeps
its own; an older build's sidecar retyped with its id intact and a deliberate importer choice left
alone; the settings' defaults and an inverted character range counting zero rather than four
billion. Checked by causing each: copying name bytes instead of decoding them, and skipping the
retype, each fail by name.

**One bound is deliberately not covered by a discriminating case**, and pretending otherwise would
be worse than saying it. Table offsets and lengths are checked against the file's real size before a
buffer is sized from them, but a font claiming a four-gigabyte table fails the read either way — a
short read is refused too. What the arithmetic buys is that four gigabytes are never *asked for*,
and making that observable would mean putting a counter in a file with no other reason for one. The
case that exists pins the outcome instead: a font whose name table cannot be read is a font with no
family, not a crash and not a refusal of the whole file.

### `STUDIO-10011` — Import jobs are cancellable and report progress accurately

**Acceptance.** Importing happens off the frame, can be stopped, and the fraction it reports is a
fraction of work actually done.

**The enabling change is the importer signature.** A job body is given a `StudioJobContext` and
nothing else — that is the rule that makes background work safe rather than merely encouraged — so
`StudioAssetImporter::readFacts(AssetDatabase&, const AssetRecord&)` could not be called from a
worker at all. It is now `gatherFacts(absolutePath, settings) -> JsonValue`: a path, a copy of the
settings, and no way to reach the document even by accident. A signature that took the database and
promised not to touch it would be a promise nothing checks.

That splits an import where the job system already cuts it: **on a worker**, read a file and produce
facts; **on the main thread**, put them on the record, compare before writing, write the sidecar.
The second half is `studioApplyImporterFacts`, and it is where the "only write when something
actually changed" rule stayed — identical for every importer, and an importer that got it wrong
would fill a repository with spurious diffs.

**Chunks of thirty-two, not one job and not one job per asset.** One job for a whole run would apply
two thousand sidecar writes in a single completion, which is the long frame this exists to prevent
with the location changed. One job per asset would make the progress fraction per-asset — every bar
full, two thousand times — and spend a bounded queue slot each (`STUDIO-30002`). A chunk is both: a
completion short enough for a frame, and a run whose fraction is real.

**The fraction is `applied / asked for`, and both numbers are honest.** The denominator is known when
the run starts, so it is a fraction rather than a guess; a *finished* run keeps its numbers until
the next request, so a caller can show a full bar before taking it away, and "nothing has ever been
asked for" is negative rather than zero. Asking for more assets mid-run extends it and the fraction
can fall — which is the true answer, because the work really did grow, and a bar kept monotonic by
hiding that would be the lie this task removes.

**Cancelling keeps what was read.** Those files really were read and their facts really are what the
files say, so throwing them away would undo work nobody asked to undo. A chunk whose worker had
already finished is applied rather than dropped for the same reason. Nothing is left half-imported
because the smallest thing the queue applies is one asset's whole facts object — which is also why
`STUDIO-10013` is about *failure*, not about interruption.

**Every asset is accounted for exactly once**, and the four outcomes are told apart because they are
different answers to "why is this number not what I expected": read, unreadable (a file no importer
claims, which a project is full of and which is not a failure), cancelled, or deleted from under the
run — the last leaving the run's total rather than counting as anything, since a file that no longer
exists cannot be read.

**What it replaced was worse than synchronous.** `StudioAssetReload` answered a single changed file
by calling `applyImporterFacts(assets)` — *every tracked asset in the project*, on the frame. On a
project of a thousand models that is a full glTF parse of each of them every time somebody saves a
texture. The reload now *reports* which assets went stale ("panels report, the binder acts", one
layer down) and `StudioShellPanels` queues those, so the work is proportional to what changed and
happens on a worker. Nothing tested the old behaviour, which is part of why it survived.

**Verification.** `tests/ImportJobTests.cpp`: a run reading every asset and saying so exactly; the
fraction never going backwards within a run and never running ahead of what has been read; a cancel
mid-run leaving what was read on the records and accounting for the rest; an unreadable file counted
rather than failed; a second run of the same assets reading everything and writing nothing; a
refused submission offering the same work again next pump rather than dropping it or waiting; an
asset deleted while queued leaving the run. All in immediate mode, which runs the same bodies,
completions and ordering as the threaded one — so nothing sleeps and nothing races.
`AFileChangedOnDiskHasItsFactsReadAgainOffTheFrame` is the wiring end to end, through the real
shell, and it polls until the queue has *read* something rather than for a fixed number of frames —
counting the producer's side of the boundary, per `STUDIO-33026`. Checked by causing it: dropping
the one line that queues the reload's report fails that case by name.

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

