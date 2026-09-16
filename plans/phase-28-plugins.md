# Phase 28 — Plugins and SDK

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-28001` … `STUDIO-28999` and are never reused.

**Purpose.** Third-party extensibility that fails safely.

**Exit criteria.** A plugin can contribute real capability, and a bad plugin produces a message rather than a crash.

**Progress:** 0 of 12 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-28001` | Plugin API versioning and explicit ABI checks | ⬜ | — |
| `STUDIO-28002` | A bad plugin fails to load with a useful message and never crashes discovery | ⬜ | `STUDIO-28001` |
| `STUDIO-28003` | Plugins contribute asset importers | ⬜ | `STUDIO-10002` |
| `STUDIO-28004` | Plugins contribute component descriptors | ⬜ | `STUDIO-15002` |
| `STUDIO-28005` | Plugins contribute panels | ⬜ | `STUDIO-07001` |
| `STUDIO-28006` | Plugins contribute commands and menu items | ⬜ | `STUDIO-06001` |
| `STUDIO-28007` | Plugins contribute inspectors | ⬜ | `STUDIO-14001` |
| `STUDIO-28008` | Plugins contribute gizmos | ⬜ | `STUDIO-12001` |
| `STUDIO-28009` | Plugins contribute exporters and validators | ⬜ | `STUDIO-18002` |
| `STUDIO-28010` | Plugins contribute build integration | ⬜ | `STUDIO-17009` |
| `STUDIO-28011` | Plugin SDK documentation and a worked example | ⬜ | `STUDIO-28009` |
| `STUDIO-28015` | A plugin is announced as *about to* unload, not noticed after it has | ⬜ | `STUDIO-28002` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-28001` — Plugin API versioning and explicit ABI checks

**Acceptance.** Carried forward; the `editorApiVersion` manifest key stays pinned for compatibility


## `STUDIO-28015` — A plugin is announced as about to unload

**Acceptance.** Anything holding a callable a plugin supplied is told to let go *before* that
plugin's library is closed, and a hot reload with a bound menu command does not crash.

**Found by `STUDIO-07052`**, which loaded a plugin on the native shell for the first time and
segfaulted on the way out. Binding a plugin command copies its `std::function` into the shell's
`StudioActionRegistry`, and destroying that copy runs a manager function living in the plugin's
library — so a registry cleared after `dlclose` jumps into unmapped memory rather than failing to
find a command.

**The shutdown path is correct now** and does not need this: the host clears the registry, then
unloads. What this covers is the *middle* of `PluginHost::deactivate`, which removes the plugin's
extensions from the context and then closes the library. Between those two statements the shell
still holds its copies, and the only thing that would drop them is
`StudioShellPanels::pollPlugins` — which notices an extension *revision*, on the next frame, after
the library has gone.

**Nothing calls `reload` today**, which is why this is a task rather than a fix: the window is real
and unreachable. It becomes reachable the moment a plugin offers a "Reload" command, or the
Plugins panel grows a button, and it would present as a crash somewhere else entirely.

**The shape of the answer** is a notification before the close rather than a revision noticed after
it — `PluginExtensionRegistry` telling its observers that an owner is going away while the owner's
code is still mapped. Not a callback into the plugin: a callback *about* the plugin, to the things
holding its callables.
