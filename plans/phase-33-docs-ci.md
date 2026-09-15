# Phase 33 — Documentation, templates and CI

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-33001` … `STUDIO-33999` and are never reused.

**Purpose.** Real developer documentation, and the test infrastructure that keeps all of it true.

**Exit criteria.** A new contributor can build, test and extend Studio from the documentation alone.

**Progress:** 10 of 21 complete `██████░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-33001` | Getting-started documentation | ⬜ | `STUDIO-08011` |
| `STUDIO-33002` | User guide for the core authoring workflow | ⬜ | `STUDIO-12011` |
| `STUDIO-33003` | Architecture documentation kept current | 🔄 | `STUDIO-02001` |
| `STUDIO-33004` | Plugin SDK documentation | ⬜ | `STUDIO-28011` |
| `STUDIO-33005` | Public API documentation coverage | ⬜ | — |
| `STUDIO-33010` | Graphical CI with a real CNA build and a display | 🔄 | `STUDIO-33023` |
| `STUDIO-33022` | CI runs the sanitizer configuration | ✅ | — |
| `STUDIO-33023` | CI runs the CNA-backed configuration on a GPU-free renderer | ✅ | `STUDIO-02060` |
| `STUDIO-33024` | CI keeps the graphical captures as artifacts | ✅ | `STUDIO-33023` |
| `STUDIO-33011` | Screenshot and golden-image test infrastructure | ✅ | `STUDIO-04013` |
| `STUDIO-33025` | The software rasterizer keeps its textures between frames | ✅ | `STUDIO-33011` |
| `STUDIO-33012` | Canonical visual test scenes | ⬜ | `STUDIO-33011` |
| `STUDIO-33013` | Visual tests at multiple resolutions | ✅ | `STUDIO-33012` |
| `STUDIO-33014` | Visual tests at multiple DPI scales | ✅ | `STUDIO-33013`, `STUDIO-03028` |
| `STUDIO-33015` | Visual regressions surface as CI artifacts | ⬜ | `STUDIO-33011` |
| `STUDIO-33016` | Compressing PNG encoder for visual-test artifacts | ⬜ | `STUDIO-33011` |
| `STUDIO-33017` | The equality assertion copies its operands rather than binding references | ✅ | — |
| `STUDIO-33018` | The roadmap's own arithmetic is checked by the test suite | ✅ | — |
| `STUDIO-33019` | The handoff's own arithmetic is checked against the same phase files | ✅ | `STUDIO-33018` |
| `STUDIO-33020` | Headless test seams maintained for every core subsystem | ⬜ | — |
| `STUDIO-33021` | CI matrix: Linux, Windows, macOS as infrastructure allows | ⬜ | — |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-33013` — Visual tests at multiple resolutions

**Acceptance.** The shell is rasterised at 1280x720, 1600x900, 1920x1080, 2560x1440 and 3440x1440 —
including an ultrawide, where a layout that only ever divided a 16:9 area shows its assumptions

### `STUDIO-33014` — Visual tests at multiple DPI scales

**Acceptance.** 125% and 175% as well as 150% and 200%. The odd scales are the ones that matter:
they are what Windows laptops actually ship on, and they are where a layout that happened to round
cleanly at the even ones comes apart

### `STUDIO-33017` — The equality assertion copies its operands rather than binding references

**Acceptance.** `CNA_STUDIO_EXPECT_EQ` copies what it is given. Binding `const auto&` to a subobject
reached *through* a temporary — `evaluation.unmetRequired().front().subject`, say — extends
nothing's lifetime: the container dies at the end of the full expression and the reference dangles.
That reads as a perfectly ordinary assertion, passes under a normal build, and is only ever found
by a sanitizer. This session found exactly that, in a new test, under ASan; the prototype's
inherited code had the same class of defect in a recovery test. Copying costs nothing a test will
notice and removes the whole category

### `STUDIO-33010` — Graphical CI with a real CNA build and a display

**Acceptance.** Unblocks the screenshot tests and the real-device smoke tests

### `STUDIO-33011` — Screenshot and golden-image test infrastructure

**Acceptance.** Tolerant image comparison, because two renderers are never bit-identical and exact equality would make the tests useless

### `STUDIO-33012` — Canonical visual test scenes

**Acceptance.** Project Hub, empty Studio, full scene, selected entity, Inspector, Content Browser, menus, modal, Build dialog, Play state, errors and warnings

### `STUDIO-33013` — Visual tests at multiple resolutions

**Acceptance.** 1280x720, 1600x900, 1920x1080, 2560x1440 and an ultrawide

### `STUDIO-33016` — Compressing PNG encoder for visual-test artifacts

**Acceptance.** The current encoder uses stored (uncompressed) deflate, which is correct, tiny and reviewable but produces roughly 8 MB for a 1920x1080 capture. Six captures per run is enough artifact traffic to be worth a real deflate once the visual suite grows

**Verification.** Encoded output still decodes in a standard viewer, and is an order of magnitude smaller

### `STUDIO-33020` — Headless test seams maintained for every core subsystem

**Acceptance.** Document model, undo, asset database, serialization, migration, project model, UI layout and state, command system, player protocol and build planning all testable with no GPU

### `STUDIO-33021` — CI matrix: Linux, Windows, macOS as infrastructure allows

**Acceptance.** Linux development is never blocked waiting for macOS or Windows infrastructure

### `STUDIO-33018` — The roadmap's own arithmetic is checked by the test suite

**Acceptance.** Three guards, run with every build: each phase file's `**Progress:** N of M` header
matches its own table; `plan.md`'s phase table and headline total match the files they summarise;
and no task id appears twice or in the wrong phase's file

**Why.** The roadmap is only worth reading if its status is true, and the failure mode is drift
rather than dishonesty -- a tick goes in, the two summary numbers above it keep the value they had,
and nobody adds up a column of thirty-six rows by hand. The guard found one on the commit that
introduced it: phase 5 had nine ✅ rows and said eight, in both places

**What it deliberately does not check.** Whether a ✅ is *deserved*. That is a judgement no test can
make, and a test that pretended to make it would be worse than no test

**Verification.** `EveryPhaseFileAgreesWithItsOwnProgressHeader`,
`TheMasterPlanTableAgreesWithEveryPhaseFile`, `NoTaskIdIsUsedTwiceAcrossTheWholePlan`. Each was
confirmed to fail on a deliberately introduced drift, not merely to pass

### `STUDIO-33022` — CI runs the sanitizer configuration

**Acceptance.** ASan and UBSan, Debug, on every push, with `halt_on_error` set — a run that reports
undefined behaviour and then prints "all tests passed" is a run somebody will believe

**Why it earns a job of its own.** Not belt and braces: a dangling reference to a subobject of a
temporary passed Debug, passed Release with warnings as errors, and was caught only here
(`STUDIO-33017`). Nothing else in the matrix would have found it

### `STUDIO-33023` — CI runs the CNA-backed configuration on a GPU-free renderer

**Acceptance.** A job that checks out CNA and sharp-runtime, builds SDL3 from CNA's vendored
submodule, builds Studio against real CNA, and runs the whole CTest suite on `SOFTWARE` with SDL's
dummy video driver — the window hosts, the native shell, and the standalone export test included.
No renderer that needs a GPU, because this runner has none and a failure caused by that would look
like a failure in Studio

**On the slow case.** The standalone export builds CNA a second time and takes minutes. It stays in,
because it is the concrete form of "CNA Studio produces CNA games, not CNA Studio games", and an
invariant excluded from CI for being slow is an invariant that rots

**On skipping honestly.** The sibling checkouts can be unavailable — a fork, or a token without
access to them. The job then reports, as a workflow warning and in the run summary, that the CNA
configuration went *unexercised*, rather than failing on every push for a reason unrelated to the
change. A job that quietly passes by doing nothing would be worse than one that is not there

**Verification.** Every command in the job was run locally, in the configuration it specifies,
before it was written down — including the two CNA options a consumer must set that nothing in CNA
mentions (CNA gap G-09) and the Draco default that wants a submodule this build has no use for

### `STUDIO-33024` — CI keeps the graphical captures as artifacts

**Acceptance.** Every PNG the CNA-backed job produces is uploaded, on failure as well as success.
The graphical cases assert on counts, which says a frame was drawn and not what was in it; the
captures are what a person looks at when a change to the shell needs reviewing

### `STUDIO-33010` — Graphical CI with a real CNA build and a display

**Acceptance.** The CNA-backed job extended to a renderer that needs a real graphics context, on a
runner that has one — the cases already labelled `needs-display`

**What holds today.** `STUDIO-33023` covers the CNA seam, the window hosts and the native shell on
`SOFTWARE`, which needs no display at all. What it cannot cover is anything a GPU does differently:
`STUDIO-29005`'s renderer matrix, and the `needs-display` cases that exist and are excluded.

**What is actually blocking it**, established by trying rather than by reasoning. The display half
is done: `CNA_STUDIO_TEST_DISPLAY` exists, Xvfb works, and configuring against a renderer that
needs a context makes fourteen `needs-display` ctests appear and run. What does not exist is such a
renderer. `SOFTWARE` and `HEADLESS` need no display; `SDL_RENDERER` needs one and cannot host
Studio at all — the capability contract refuses it for having no 3D pipeline and no depth buffer;
`SDL_GPU` and `VULKAN` configure but need a Vulkan ICD a bare runner does not have; and every
OpenGL family needs `easy-gl` and `meta-gl` sibling checkouts that CNA does not vendor. So the job
needs those two checkouts, Mesa's software GL and Xvfb — a shopping list, recorded as `G-10` in
`docs/CNA-GAPS.md`, rather than a change to this repository.

**Worth keeping from the attempt.** `SDL_RENDERER` builds a complete Studio and Studio refuses to
start on it, naming `ThreeDimensionalPipeline` and `DepthStencilBuffer` and saying what each is
for. That is `STUDIO-02021`'s capability contract exercised against a real inadequate renderer for
the first time rather than a synthetic capability set, and it behaved as designed

### `STUDIO-33025` — The software rasterizer keeps its textures between frames

**Acceptance.** `UiTextureTable` holds what has been uploaded, and `rasterizeUiDrawData` takes one,
the way a real renderer keeps an upload rather than re-reading a request every frame

**What it fixes.** A `UiDrawData` carries a texture *request* only on the frame the texture changed.
The font atlas is rasterised once, so from frame two onwards the draw data names an atlas it does
not carry — and a rasterizer that rebuilt its table per frame had no font. Every glyph drew as a
solid rectangle. Text came out as a row of blocks in every multi-frame capture, which is every
`--shell-preview` since the flag existed

**Why nothing caught it.** The golden images render a single frame, where the request is present;
the real CNA renderer keeps its uploads, so the window was always right. The two paths that could
have disagreed never compared. Found by looking at a screenshot of something else

**Verification.** `TheSecondFrameOfAStaticShellLooksExactlyLikeTheFirst` — a shell nobody touched
must look the same on its second frame as its first — and it asserts the *shape* of the failure too:
without the table strictly more of the image is covered, because a filled box covers more than the
glyph inside it. Plus `ATextureTableKeepsWhatItIsGivenAndForgetsWhatIsDestroyed`

### `STUDIO-33019` — The handoff's own arithmetic is checked against the same phase files

**Acceptance.** `HANDOFF.md`'s headline task count and every per-phase count it claims are checked
against the phase files, and the test fails naming the phase that disagrees.

**Why it needed to exist.** The handoff is what somebody reads first, and a count in it that is one
session stale is worse than no count at all: it is a number they will quote. `plan.md`'s arithmetic
has been checked since `STUDIO-33018`; the handoff was not, and drifted by nineteen tasks and a
hundred and fifty-eight test cases before anybody noticed — which is precisely the failure
`STUDIO-33018` was written to prevent in the other file.

**Only the numbers.** The prose is a judgement about what was built and no test can hold it to
anything. "159 of 486" is a fact, and facts are checkable; it also refuses a handoff that mentions no
phase at all, which would otherwise pass every check by saying nothing.

**Verification.** `TheHandoffsOwnArithmeticMatchesThePhaseFiles`, checked by changing a count in the
handoff and watching it name the phase
