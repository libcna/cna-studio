# Phase 0 — Audit and baseline

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-00001` … `STUDIO-00999` and are never reused.

**Purpose.** Establish exactly what was inherited, prove it works, and record the measurements everything later is compared against.

**Exit criteria.** The imported tree builds clean and green from an empty build directory, its numbers are written down, and the provenance of every file is recorded.

**Progress:** 14 of 15 complete `███████████░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-00001` | Clone `cna-lab` at `develop` and record the exact source commit SHA | ✅ | — |
| `STUDIO-00002` | Inventory the prototype source tree: modules, LOC, file counts, third-party vendoring | ✅ | `STUDIO-00001` |
| `STUDIO-00003` | Read the prototype architecture record (`ANALYSIS.md`, `plan.md`, `NEXT.md`, `docs/`) | ✅ | `STUDIO-00001` |
| `STUDIO-00004` | Build the prototype in its default dependency-free configuration | ✅ | `STUDIO-00001` |
| `STUDIO-00005` | Run the prototype test suite and record the baseline counts | ✅ | `STUDIO-00004` |
| `STUDIO-00006` | Record the baseline executable and target names before any rename | ✅ | `STUDIO-00004` |
| `STUDIO-00007` | Verify the headless project-open path against the bundled example project | ✅ | `STUDIO-00004` |
| `STUDIO-00008` | Import the `cna-editor` tree into the `cna-studio` repository root | ✅ | `STUDIO-00005`, `STUDIO-00006` |
| `STUDIO-00009` | Write the provenance record | ✅ | `STUDIO-00008` |
| `STUDIO-00010` | Configure the repository-local commit identity | ✅ | — |
| `STUDIO-00011` | Re-audit current CNA (`next`) for renderers, platforms and capability reporting | ✅ | `STUDIO-00003` |
| `STUDIO-00012` | Re-verify the prototype-era CNA gaps against current CNA | ✅ | `STUDIO-00011` |
| `STUDIO-00013` | Capture reference screenshots of the prototype UI before migration begins | ✅ | `STUDIO-00004` |
| `STUDIO-00014` | Record the prototype panel, menu and shortcut inventory as the migration checklist | ✅ | `STUDIO-00003` |
| `STUDIO-00015` | Measure baseline start-up time and headless frame cost | ⬜ | `STUDIO-00004` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-00001` — Clone `cna-lab` at `develop` and record the exact source commit SHA

**Acceptance.** SHA `3bce82dd74e9a201a21e31308d43d2ee7761d641` recorded in `docs/ORIGIN.md`

**Verification.** `git rev-parse HEAD` in the source checkout

### `STUDIO-00002` — Inventory the prototype source tree: modules, LOC, file counts, third-party vendoring

**Acceptance.** 12 modules, 3 executables, ~54k LOC across src/include/tests identified

**Verification.** Recorded in this plan and `docs/ORIGIN.md`

### `STUDIO-00003` — Read the prototype architecture record (`ANALYSIS.md`, `plan.md`, `NEXT.md`, `docs/`)

**Acceptance.** The 16 decisions and 117 legacy tasks are understood and classified

### `STUDIO-00004` — Build the prototype in its default dependency-free configuration

**Acceptance.** Clean configure and build, zero warnings, GCC 13.3 / C++23 / CMake 3.28

**Verification.** `cmake -S . -B build -G Ninja && cmake --build build -j4`

### `STUDIO-00005` — Run the prototype test suite and record the baseline counts

**Acceptance.** 442 unit assertions and 12 CTest suites, all passing, in ~2.5s

**Verification.** `ctest --test-dir build --output-on-failure`

### `STUDIO-00006` — Record the baseline executable and target names before any rename

**Acceptance.** `cna-editor`, `cna-player`, `cna-editor-tests`, 12 `cna-editor-*` libraries

### `STUDIO-00007` — Verify the headless project-open path against the bundled example project

**Acceptance.** Opens HelloSprites, scans 4 assets, loads a 5-entity scene, draws a frame

**Verification.** `./build/cna-editor --headless --project=examples/HelloSprites/HelloSprites.cnaproject`

### `STUDIO-00008` — Import the `cna-editor` tree into the `cna-studio` repository root

**Acceptance.** 214 tracked files at the repository root; no nested `cna-editor/` directory; no build artifacts

**Verification.** `git archive <sha> cna-editor | tar -x --strip-components=1`; `git status`

### `STUDIO-00009` — Write the provenance record

**Acceptance.** `docs/ORIGIN.md` names source repo, branch, directory, SHA, date, baseline measurements, and states that no ongoing synchronisation exists

### `STUDIO-00010` — Configure the repository-local commit identity

**Acceptance.** `user.name`/`user.email` set locally; every commit authored by Robert Vokac

**Verification.** `git log --format=fuller`

### `STUDIO-00011` — Re-audit current CNA (`next`) for renderers, platforms and capability reporting

**Acceptance.** 49 renderer identities, 4 implemented platforms, `RendererCapabilityProfile` with 32 features / 22 limits / per-format masks, 98 CNAEXT headers — all recorded in `docs/ARCHITECTURE.md`

### `STUDIO-00012` — Re-verify the prototype-era CNA gaps against current CNA

**Acceptance.** G-01 closed, G-04 narrowed, G-02/G-03/G-05 still open, G-06 newly filed — in `docs/CNA-GAPS.md`

### `STUDIO-00013` — Capture reference screenshots of the prototype UI before migration begins

**Acceptance.** The documented panel layout captured at 1280x720 and 1920x1080 through a real CNA device, stored as the "before" reference for the UI migration

**Verification.** `docs/reference/prototype-1280x720.png` and `prototype-1920x1080.png`, with the
native shell beside each at the same size, and `TheReferenceCapturesExistAtTheSizesTheReviewClaims`
checking the dimensions from the PNG headers rather than from the filenames

**It was never blocked on a display.** This said "blocked on CI graphics (`STUDIO-33010`)" and had
said so since before `STUDIO-33023` existed. The `SOFTWARE` renderer *is* a real CNA device and
needs no display at all, which is exactly why CI runs on it — so the reference could have been taken
at any point after that job landed. Answered by trying it rather than by reading the note.

**What it needed instead** was `--window-size`: both UIs open a real window through CNA and neither
could be asked for one of a given size, so "the same screen at the same resolution on both" was not
capturable. That is the comparison the whole reference exists for

### `STUDIO-00014` — Record the prototype panel, menu and shortcut inventory as the migration checklist

**Acceptance.** Every panel, menu item, toolbar control and keyboard shortcut listed with its owning source file, so Phase 7 can prove parity item by item rather than by impression

**A checklist rather than a document.** `docs/MIGRATION-INVENTORY.md` would have been a record of
intentions; the test suite makes it a *check*. An item marked answered must resolve — a panel id to
a registered panel that draws something, a command id to a command that exists and has a handler,
and a chord to a command bound to that same chord. An item with no native answer carries the reason,
in the same discipline as the unimplemented-command and empty-panel guards.

**What writing it down found, which is the point.** Three chords silently disagreed with the
prototype's: `Ctrl+N` meant New *Scene* there and New *Project* natively (and there was no native
New Scene at all); `Ctrl+D` was Duplicate there and Open Project natively, with Duplicate pushed to
`Ctrl+Shift+D`; and `X`, the gizmo-space toggle, had no native command whatsoever. Each would have
shipped as "the shortcut I have used for a year does something else now" — the worst kind of
regression, because it works and so nothing reports it. All three are fixed, which needed a new key
in the vocabulary (`O`, so Open could take the conventional `Ctrl+O` and give `Ctrl+D` back).

**What it cannot check** is whether the two *behave* the same, which is `STUDIO-07020`–`07023`. It
checks that the counterpart exists, and that is where these were found.

**Verification.** `tests/StudioMigrationInventoryTests.cpp`: the file exists and lists enough to be
a checklist, every answered panel is registered and draws, every answered chord is bound to that
chord, and every answered menu item's command exists with a handler — each checked by pointing a row
at the wrong thing and watching it name the line

### `STUDIO-00015` — Measure baseline start-up time and headless frame cost

**Acceptance.** Numbers recorded for later comparison; measured cheaply, not instrumented

