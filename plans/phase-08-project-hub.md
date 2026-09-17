# Phase 8 — Project Hub

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-08001` … `STUDIO-08999` and are never reused.

**Purpose.** A professional entry point: create, open, recent projects and a small set of well-maintained templates.

**Exit criteria.** Studio opens on a hub that can create a working project and open an existing one, with validation that catches mistakes before they become confusing failures.

**Progress:** 12 of 12 complete `████████████`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-08001` | Project Hub window and layout | ✅ | `STUDIO-06015` |
| `STUDIO-08002` | Recent projects list with validity checking | ✅ | `STUDIO-08001` |
| `STUDIO-08003` | Open Project flow | ✅ | `STUDIO-08001` |
| `STUDIO-08004` | New Project flow with path, name and validation | ✅ | `STUDIO-08001` |
| `STUDIO-08005` | Template model | ✅ | `STUDIO-08004` |
| `STUDIO-08006` | Template: Empty 3D | ✅ | `STUDIO-08005` |
| `STUDIO-08007` | Template: Empty 2D | ✅ | `STUDIO-08005` |
| `STUDIO-08008` | Template: XNA-compatible | ✅ | `STUDIO-08005` |
| `STUDIO-08009` | Template: basic sample | ✅ | `STUDIO-08005` |
| `STUDIO-08010` | Renderer and platform defaults per template | ✅ | `STUDIO-08005`, `STUDIO-02040` |
| `STUDIO-08011` | Every template produces a project that builds and runs without Studio | ✅ | `STUDIO-08009` |
| `STUDIO-08012` | Project validation on open with actionable diagnostics | ✅ | `STUDIO-08003` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-08001` — Project Hub window and layout

**Done.** A `projecthub` panel docked beside the viewport, with two pages — Recent and New Project
— and the two File menu commands that were greyed out since `STUDIO-06001` now open it with the
right one in front.

**A panel rather than a modal window, and that was the decision.** The obvious shape is a splash
window in front of the editor, and it is the wrong one three times over: the headless preview
cannot photograph it, so the one visual test this project has on a machine with no GPU would never
see the Hub; it cannot be reached again once dismissed, so "open a second project" becomes a
different gesture from "open the first"; and it needs the modal window Studio does not have yet.
A tab is a tab, it docks and floats like every other panel, and it is in the shell preview above.

**The Hub knows nothing about how a project is built**, and that is checked rather than intended:
`STUDIO-02085`'s guard refuses this file the vocabulary to name a compiler, a build system or a
build file. The language chooser is filled from `StudioLanguageRegistry` rather than from a list,
so it holds one entry today and will hold two without this panel changing. Doing `STUDIO-02080`
before this tranche is what made that possible — a Hub written first would have had
`CMakeLists.txt` in it by the third row.

**Verification.** `tests/StudioProjectHubTests.cpp` for the form's own rules, and the shell preview
for the picture: `--shell-panel-only=projecthub` captures either page from a build with no GPU and
no display.

### `STUDIO-08002` — Recent projects list with validity checking

**Acceptance.** A moved or deleted project is shown as unavailable rather than failing on click

**Done, and the obvious implementation gets it wrong.** Filtering missing entries out when the list
is read makes a project on an unmounted drive *disappear* — so somebody who unplugs a disk loses
their history rather than seeing it greyed out until they plug it back in. A row that is present
and unavailable is also the only place there is to say *why*, and the two reasons have different
fixes: a project whose directory is gone was deleted or is on a drive that is not mounted, and one
whose directory is there but whose file is not was renamed.

Availability is answered when the list is **read**, not stored. A stored flag is a cache of the
filesystem, and the filesystem changes while Studio is not running — which is precisely the case
this feature exists for.

The list is bounded at twenty and a row can be taken off it, because the one thing to do about a
project that is gone for good is to remove it, and a user who cannot is left with a hub that is
mostly wrong. A corrupt file reads as an empty list rather than as a failure: nothing about this is
worth refusing to open Studio over.

### `STUDIO-08003` — Open Project flow

**Done, without waiting for a file dialog.** Two routes, and neither needs the modal window Studio
does not have: the recent list, and a path field beside it on the same page.

A Hub that could only reopen what it had already opened could not open anything the first time,
which would have made this row wait on `STUDIO-03022` for a window rather than on anything about
opening projects. A path field is what a terminal user would type anyway, Enter opens it, and it
does not become dead weight when a file dialog arrives — a dialog fills the same field in.

Opening runs the project's diagnostics (`STUDIO-08012`) into the Output Log, remembers it in the
recent list, switches to the view the project asks for (`STUDIO-11014`) and brings the viewport
forward. A project that will not open leaves the Hub where it is with the reason on the field,
because sending somebody to an empty viewport to be told nothing is the worse answer.

### `STUDIO-08004` — New Project flow with path, name and validation

**Acceptance.** Rejects invalid names, non-empty directories and unwritable paths, each with a specific message

**Done.** Nine refusals, each naming the field it is about and what to do, because "invalid
project" is the message that makes somebody try the same thing again. Every one of them is
provoked in a test rather than described here.

**Two of them are less obvious than they look.** A name that is reserved on Windows — `CON`,
`LPT1` and the rest — is refused *on Linux*, because a project created on one machine and unable to
be checked out on another is a trap that springs on somebody else. And writability is checked by
writing a file and deleting it, not by reading permission bits: permissions are one of several
reasons a write fails, and a check that models only that one passes right before the failure it
was there to predict.

**Validation leaves nothing behind.** The Hub validates as the user types, so a check that created
the directory it was asked about would create one per keystroke — and then refuse the next
keystroke because the directory it had just made was in the way.

**Nothing is written until every check has passed.** A creation that fails halfway leaves a
directory that is neither a project nor empty, and the user is left working out which files were
theirs. The `.cnaproject` is written *last*, because a directory with one in it is one the Hub
lists as a project.

### `STUDIO-08005` — Template model

**Acceptance.** A template is data plus a file tree, not code. Adding one does not require changing Studio

**Done, and meant literally.** A template is a directory holding a `template.json` and a `content/`
subtree. Nothing about it is compiled, registered or listed in a C++ table, and the ids in this
repository's `templates/` directory are the directory names. Dropping a directory on a search path
is the whole of adding one — and `STUDIO-08011` globs the same directory, so a template added that
way is a template CI creates and builds without anybody editing a test.

**A template carries no build file, no entry point and no runtime.** Those are the *language's*
answer and come from the project's `StudioLanguageAdapter` (`STUDIO-02083`), which is what lets one
template serve every language that declares it can host it. A template carrying a `CMakeLists.txt`
would be a template secretly about C++, and there would be four copies of it to keep in step.

**A template's own file wins** over the one the language would have written, and says so in a
warning. The template is the more specific answer, and silently overwriting what it provided would
make a template unable to customise its own entry point.

**Search paths are most-specific-first**: beside the executable for an installed Studio, and the
source tree baked in at configure time for a development build. The second is what lets
`cna-studio` run out of a build directory offer the same templates a user gets, which is what stops
the templates being a thing only an installed Studio has ever exercised.

**Content is copied byte for byte, UUIDs included.** Two projects created from one template
therefore share their scene and entity ids, which is harmless — those ids are scoped to a scene —
and it is what makes creation reproducible. Generating fresh ids would make the same template
produce a different tree every time, which is a worse property than the one it fixes, and it would
make `STUDIO-08011`'s build unreproducible.

### `STUDIO-08008` — Template: XNA-compatible

**Acceptance.** Produces a project with its own `Initialize`/`LoadContent`/`Update`/`Draw` and no entity model

**Done, and it is the one place `ProjectKind` reaches all the way into generated source.** The
created `Source/Main.cpp` is a plain CNA `Game` with those four members and nothing of Studio's
scene model in it. It gets no scene runtime either: a hand-written game compiling a loader for a
format it never reads would be carrying a file nobody can explain, and its build file stages only
its assets, because copying a `Scenes/` directory that is not there is a build failure that says
nothing about project kinds.

**Writing it found a defect in project creation.** `Project::createDefault` fills in a conventional
startup scene path, which is right for a project that has scenes and a dangling reference for one
that does not — so an XNA-compatible project was created already reporting a missing startup scene,
which is a generator bug shown to the user as their fault.
`EveryShippedTemplateProducesAProjectStudioCanOpenAgain` caught it, because it runs `STUDIO-08012`'s
diagnostics over every freshly created project and fails on an error. Creation now clears the
startup scene when the template names none, and validation asks an XnaCompatible project for
neither a scene directory nor a startup scene — asking would be forcing the entity model on exactly
the project kind that exists so it cannot be forced (`docs/ARCHITECTURE.md` §8).

### `STUDIO-08010` — Renderer and platform defaults per template

**Done.** Each manifest names the renderer and the platform its project starts on, and creation
writes both onto the **active target profile** — which is what Build and Play read — as well as
onto the legacy `defaultGraphicsBackend` key that an older Studio reads. Empty 3D starts on
`opengl33` and the 2D templates on `opengles3`.

Asserted against the manifest rather than against a constant, so a template whose renderer is
changed is a template whose test changes with it.

### `STUDIO-08011` — Every template produces a project that builds and runs without Studio

**Acceptance.** Tested for each template, in CI, as the concrete form of the central invariant

**Done, as a real build.** `cmake/TemplateBuildTest.cmake` drives five steps per template and fails
on the first that does not work:

1. create the project through `cna-studio --new-project`, which is the same code the Hub's button
   runs;
2. reopen it with `cna-studio --project= --headless`, so "these bytes parse" and "the editor
   accepts them" are not confused;
3. configure it with nothing but CMake and a CNA checkout — no feature options, no
   `CNA_BUILD_TESTS`, nothing this script knows about CNA that the project has not asked for
   itself;
4. compile it;
5. run it, and require it to *say* what it did.

**One CTest case per template, discovered by globbing `templates/*/template.json`.** A list of test
names in `tests/CMakeLists.txt` would be exactly the change `STUDIO-08005` says adding a template
must not require — and it would be the change nobody makes, so the fifth template would be the one
CI never built.

**The assertion is read from the project Studio wrote, not from a list in the script.** A
CnaNative game must report `loaded N entities` with N ≥ 1; an XnaCompatible one has no scene to
load and must report `ran N frames`. The script reads `kind` out of the `.cnaproject` to decide
which, so a template added tomorrow is asserted correctly without this file being edited.

**The project is created under a name with a space in it** — `Template Probe` — on purpose. A
generator that composed a build target name from the project name without reducing it produces a
`CMakeLists.txt` that will not configure, and the obvious test name would never have found out.

**Why this and not `STUDIO-02051`.** The export test already proves an *exported* project builds.
Every project a user actually makes comes out of the Project Hub instead, and until this existed
that path had never been compiled by anything: a template producing a tree that does not build
would have been found by the first person to press New Project.

### `STUDIO-08012` — Project validation on open with actionable diagnostics

**Done.** A project whose startup scene is missing, whose renderer no longer exists or whose
language this build cannot author **still opens**. That is not leniency: a broken project is
exactly the project somebody needs the editor for, and an editor that refuses to open it has
removed the only tool that could have fixed it. Loading reports what it could not read, validation
reports what is wrong, and neither refuses.

Every diagnostic names its subject and carries a remedy, and a test requires both — "invalid
project" is the message that makes somebody try the same thing again. They are sorted worst-first,
because a pane that buried the one thing stopping a build under four remarks would be sorted by the
order the checks happen to run in, which is not an order anybody reads.

The target-profile rules are **forwarded** from `validateStudioTargetProfile` rather than
re-implemented, on a copy — that function migrates a legacy renderer name in place, and rewriting
the open project as a side effect of *looking* at it would make a diagnostics pane that silently
edits.

