# Phase 15 — C++ gameplay component workflow

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-15001` … `STUDIO-15999` and are never reused.

**Purpose.** Make project-defined C++ behaviour a first-class authoring concept — without an embedded IDE and without parsing all of C++.

**Exit criteria.** A user can create a C++ component, expose properties to the Inspector, edit them, build, and run it in Play mode.

**Progress:** 0 of 12 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-15001` | Decide the reflection mechanism for project-defined components | 🔬 | `STUDIO-02050` |
| `STUDIO-15002` | Component type identity and registration | ⬜ | `STUDIO-15001` |
| `STUDIO-15003` | Property metadata: names, types, defaults, ranges, enums, categories, tooltips | ⬜ | `STUDIO-15002` |
| `STUDIO-15004` | Asset and entity reference properties from project code | ⬜ | `STUDIO-15003` |
| `STUDIO-15005` | Serialisation of project-defined component data | ⬜ | `STUDIO-15003` |
| `STUDIO-15006` | Studio reads project component metadata without loading game code into Studio | ⬜ | `STUDIO-15002` |
| `STUDIO-15007` | Inspector editing of project-defined component properties | ⬜ | `STUDIO-15006`, `STUDIO-14001` |
| `STUDIO-15008` | Create a C++ component from Studio, generating boilerplate | ⬜ | `STUDIO-15002` |
| `STUDIO-15009` | Attach a component to an entity | ⬜ | `STUDIO-15007` |
| `STUDIO-15010` | Open project in IDE, open source file, open component source | ⬜ | `STUDIO-06009` |
| `STUDIO-15011` | Runtime synchronisation of edited properties into the running game | ⬜ | `STUDIO-16001` |
| `STUDIO-15012` | Build errors from project code surfaced in Studio with the original log retained | ⬜ | `STUDIO-17010` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-15001` — Decide the reflection mechanism for project-defined components

**Acceptance.** A recorded decision among registration functions, metadata descriptors, lightweight macros, generated descriptor files or a small project-side generator — chosen on maintainability and ABI safety, with the rejected options and their reasons written down

**Verification.** Decision recorded in `docs/ARCHITECTURE.md` before implementation begins

### `STUDIO-15010` — Open project in IDE, open source file, open component source

**Where the answer lives, decided in advance by `STUDIO-02080`.** *Which* files a user edits and
*where* they are is the language adapter's: `StudioLanguageDescriptor::sourceDirectory` and
`sourceFileExtensions` (`docs/ARCHITECTURE.md` §13.3). What to launch is
`StudioPreferences::externalEditor`, which already exists and has no caller yet.

Studio does not build an IDE (`plan.md`, *Deliberately not built*). This opens the user's.

### `STUDIO-15001` — the decision is narrower than it was

The language seam changed what this is a decision *about*. Gameplay-component metadata is one of
the boundaries the C++ adapter owns, so the reflection mechanism is **that adapter's answer** rather
than a Studio-wide one — a second CNA binding would answer it differently and would not be waiting
on this. Still 🔬, still to be recorded in `docs/ARCHITECTURE.md` before implementation, and still
the row that gates the rest of this phase.

### `STUDIO-15006` — Studio reads project component metadata without loading game code into Studio

**Acceptance.** The game process remains where game C++ executes

### `STUDIO-15008` — Create a C++ component from Studio, generating boilerplate

**Acceptance.** Generated files are deterministic, reviewable and clearly separated; handwritten code is never overwritten by regeneration

**Verification.** Test: regeneration after a user edit preserves the user edit

