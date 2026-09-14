# Phase 33 — Documentation, templates and CI

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-33001` … `STUDIO-33999` and are never reused.

**Purpose.** Real developer documentation, and the test infrastructure that keeps all of it true.

**Exit criteria.** A new contributor can build, test and extend Studio from the documentation alone.

**Progress:** 5 of 16 complete `███░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-33001` | Getting-started documentation | ⬜ | `STUDIO-08011` |
| `STUDIO-33002` | User guide for the core authoring workflow | ⬜ | `STUDIO-12011` |
| `STUDIO-33003` | Architecture documentation kept current | 🔄 | `STUDIO-02001` |
| `STUDIO-33004` | Plugin SDK documentation | ⬜ | `STUDIO-28011` |
| `STUDIO-33005` | Public API documentation coverage | ⬜ | — |
| `STUDIO-33010` | Graphical CI with a real CNA build and a display | ⬜ | — |
| `STUDIO-33011` | Screenshot and golden-image test infrastructure | ✅ | `STUDIO-04013` |
| `STUDIO-33012` | Canonical visual test scenes | ⬜ | `STUDIO-33011` |
| `STUDIO-33013` | Visual tests at multiple resolutions | ✅ | `STUDIO-33012` |
| `STUDIO-33014` | Visual tests at multiple DPI scales | ✅ | `STUDIO-33013`, `STUDIO-03028` |
| `STUDIO-33015` | Visual regressions surface as CI artifacts | ⬜ | `STUDIO-33011` |
| `STUDIO-33016` | Compressing PNG encoder for visual-test artifacts | ⬜ | `STUDIO-33011` |
| `STUDIO-33017` | The equality assertion copies its operands rather than binding references | ✅ | — |
| `STUDIO-33018` | The roadmap's own arithmetic is checked by the test suite | ✅ | — |
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
