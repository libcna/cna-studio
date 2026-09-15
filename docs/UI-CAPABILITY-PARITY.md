# UI capability parity — what the prototype's UI layer can do, and what answers it natively

`plan.md` STUDIO-07021, STUDIO-07022.

`docs/MIGRATION-INVENTORY.md` accounts for the prototype's *surface*: its panels, menu items,
toolbar controls and shortcuts. This accounts for what is underneath them — the `StudioUi` interface
every prototype panel is written against. Each of its methods is something a panel can ask the UI
to do, so a method with no native answer is a thing the ported panels cannot do, whatever the
inventory says about the panel that used it.

The two lists answer different questions and both are needed. A panel can be ported, appear in the
inventory as ✅, and still be poorer than the one it replaced because the capability it leaned on
does not exist natively. That is the failure this list exists to catch.

## How to read it

| Column | Meaning |
|--------|---------|
| Capability | The `StudioUi` method, exactly as `include/CNA/Studio/Ui/StudioUi.hpp` declares it |
| Native | What answers it in the native shell |
| Proven by | A test that exercises the native answer. **Must exist**: the suite checks the name |
| Status | ✅ answered and proven · 🔄 answered differently on purpose · ⬜ no native answer |

The test suite checks this file in both directions, as `STUDIO-07020` does for the inventory:
every `virtual` method on `StudioUi` must be listed, every listed capability must be a method that
exists, and every name in *Proven by* must be a test case the suite actually registers. A row
claiming proof by a test nobody wrote fails the build.

---

## Frame and application

| Capability | Native | Proven by | Status |
|------------|--------|-----------|--------|
| `getBackendName` | `StudioStatusModel::renderer`, set by the host | `TheBarSaysWhatIsOpenAndWhatTheProjectShipsOn` | ✅ |
| `isRunning` | The host's own loop; the shell does not own the window | `AShellFrameCommitsNoPhaseViolationsAndNoIdCollisions` | 🔄 |
| `beginFrame` / `endFrame` | `runStudioFrame`, two passes over one description | `AFrameWalksItsPhasesInOrder` | ✅ |
| `isAnyItemActive` | `StudioInputRouter::wantsTextInput` and the active widget | `AFocusedFieldTellsTheRouterThatAKeyMeansText` | ✅ |
| `requestExit` | `StudioShell::setQuitHandler` and `studio.file.quit` | `QuittingWithUnsavedChangesAsksBeforeAnythingCloses` | ✅ |

`isRunning` is 🔄 rather than ✅ because it is not a UI capability at all: it asks whether the
application's window is still open, which in the native shell is the host's question and never the
shell's. `CnaStudioShellHost` answers it for CNA and the headless preview has no window to ask about.

## Docking and panels

| Capability | Native | Proven by | Status |
|------------|--------|-----------|--------|
| `beginDockSpace` / `endDockSpace` | `StudioDockTree`, explicit and serializable | `ASplitsMinimumIsTheSumOfItsChildrensAlongItsAxis` | ✅ |
| `beginPanel` / `endPanel` | `StudioShell::registerPanel` and `setPanelContent` | `AClosedPanelReopensSomewhereVisible` | ✅ |
| `requestPanelFocus` | `StudioShell::activatePanel`, and `studio.window.showPanel.<id>` | `ShowingAPanelNeverHidesOne` | ✅ |
| `getContentRegion` | The `UiRect` a panel's content callback is given | `EveryShellRegionIsVisiblyDistinct` | ✅ |

`DockSide`, the preferred side a prototype panel asks for, has no native counterpart and needs
none: the native arrangement is a saved layout rather than a hint each panel repeats, which is what
`STUDIO-05009` is about. A panel opened into a shell with no saved layout lands somewhere visible,
which is the only promise `DockSide` was really making.

## Widgets

| Capability | Native | Proven by | Status |
|------------|--------|-----------|--------|
| `text` | `studioDrawText` | `DrawingTextEmitsGlyphQuadsAgainstTheAtlas` | ✅ |
| `button` | `studioButton` | `AButtonActivatesOncePerClickDespiteBeingDescribedTwice` | ✅ |
| `checkbox` | `studioCheckbox` | `ACheckboxFlipsOncePerClickAndReportsTheChange` | ✅ |
| `propertyField` | `studioPropertyRow` and the Details panel's editors | `EveryPropertyKindTheSchemaDeclaresGetsAControlRatherThanASummary` | ✅ |
| `inputText` | `studioTextField` | `ATextFieldCommitsOnEnterAndNotBefore` | ✅ |
| `treeNode` / `treePop` | `studioTreeView` over a flat row list | `ATreeStartsOpenRatherThanMakingTheUserFindItsContents` | ✅ |
| `image` | `studioImage` through `UiDrawData`'s texture protocol | `TheViewportCompositesASceneWhenOneIsHandedToItAndTheGridWhenNot` | ✅ |
| `imageRegion` | The same, with a source rectangle | `AClipTurnsAFrameIndexIntoASourceRectangle` | ✅ |
| `separator` | `StudioDrawList::drawHorizontalSeparator` | `EveryShellRegionIsVisiblyDistinct` | ✅ |
| `sameLine` | `UiRect::splitLeft` — layout is arithmetic, not a cursor | `TheBarDrawsAtAnyWidthWithoutOverflowing` | 🔄 |
| `setNextItemWidth` | The same: a caller passes the rectangle it wants | `TheBarDrawsAtAnyWidthWithoutOverflowing` | 🔄 |

`sameLine` and `setNextItemWidth` are 🔄 because they are answered by *not existing*. They are a
cursor-based layout's way of saying "put the next thing beside this one" and "make it this wide",
and the native UI has no cursor: a caller splits the rectangle it was given and passes the pieces
in. That is a deliberate difference rather than a gap — the failure they exist to work around,
a control whose width depends on what was drawn before it, cannot happen.

## Input

| Capability | Native | Proven by | Status |
|------------|--------|-----------|--------|
| `isKeyDown` | `UiInputState::isKeyDown`, the same snapshot | `AHeldShortcutFiresOnceRatherThanEveryFrame` | ✅ |
| `isShortcutPressed` | `StudioInputRouter::keyPressed` and the action registry | `AShortcutInvokesItsActionThroughTheRegistry` | ✅ |
| `getModifiers` | `UiInputState::modifiers` | `AModifierOnItsOwnIsNotAChord` | ✅ |
| `setDragSource` | `studioDragSource` and `StudioFrame::StudioDragPayload` | `ADragWithNoTypeIsRefused` | ✅ |
| `acceptDrop` | `StudioFrame::acceptDrop` | `ADragKeepsReceivingInputAfterLeavingItsBounds` | ✅ |
| `setClipboardText` | `StudioFrame`'s clipboard seam | `TheClipboardWorksWithoutAPlatformAndDefersToOneWhenThereIs` | ✅ |

## Menus

| Capability | Native | Proven by | Status |
|------------|--------|-----------|--------|
| `beginMenu` / `endMenu` | `StudioMenuDefinition`, built from the action registry | `ArrowKeysMoveTheHighlightSkippingSeparatorsAndEnterInvokes` | ✅ |
| `menuItem` | A `StudioMenuEntry` naming an action id | `ADisabledMenuItemDoesNotActivate` | ✅ |
| `beginContextMenu` / `endContextMenu` | `StudioShell::openContextMenu` | `AContextMenuOpensWithItsCornerOnThePoint` | ✅ |

## Logging

| Capability | Native | Proven by | Status |
|------------|--------|-----------|--------|
| `log` | `StudioLog::append` — one model, read by both consoles | `TheConsoleScrollLockIsRememberedAcrossFrames` | ✅ |
| `drawLogView` | `studioLogPanel` | `FollowingNewOutputStopsTheMomentTheUserScrollsAway` | ✅ |
| `getLogText` | `StudioLog` and the panel's Copy | `TheClipboardWorksWithoutAPlatformAndDefersToOneWhenThereIs` | ✅ |
| `clearLog` | `StudioLog::clear` and the panel's Clear | `TheConsoleScrollLockIsRememberedAcrossFrames` | ✅ |

---

## Docking parity (`STUDIO-07022`)

The capability table above says the native shell *has* docking. This says it puts things in the
same places.

Every prototype panel asks for a side when it opens — `beginPanel("Inspector", DockSide::Right)` —
and that is the whole of what the prototype's docking promises a panel. The native shell's default
arrangement has to honour those four sides for the panels the inventory pairs them with, or a user
who knows where things are would find them somewhere else after the migration:

| Prototype panel | Asks for | Native panel | Where the default layout puts it |
|-----------------|----------|--------------|----------------------------------|
| Scene Hierarchy | `Left` | `outliner` | Left group, with `layers` |
| Inspector | `Right` | `details` | Right group, with `material` and `history` |
| History | `Right` | `history` | Right group |
| Assets | `Bottom` | `content` | Bottom strip, full width |
| Console | `Bottom` | `output` | Bottom strip |
| Build | `Bottom` | `build` | Bottom strip |
| Validation | `Bottom` | `problems` | Bottom strip |
| Backends | `Bottom` | `comparison` | Bottom strip |
| Diagnostics | `Bottom` | `diagnostics` | Bottom strip |
| Viewport | `Center` | `viewport` | Centre |

`ThePrototypesDockSidesAreWhereTheNativeDefaultLayoutPutsThem` checks this against the *resolved
geometry* rather than against the calls that built the tree: it asks where each panel's rectangle
actually lands relative to the dock area, which is the claim a user can check by looking.

Everything the native model can do beyond this — dragging a tab to any edge, tabbing panels
together, floating one into its own window, saving an arrangement under a name, and restoring it
from a file — is more than the prototype offers rather than parity with it, and is covered by
`STUDIO-05004` … `STUDIO-05012`.

---

## What writing this list down found

Nothing missing, which is itself the answer worth recording: every capability the prototype's panels
are written against has a native counterpart, and the four that differ (`isRunning`, `sameLine`,
`setNextItemWidth`, and `DockSide` on `beginPanel`) differ because the native design does not have
the problem they solve rather than because it has not got there yet.

That is the half of parity `STUDIO-07020` could not reach. The inventory proves the *surface* is
covered; this proves the panels had nothing underneath them that has been left behind.

What neither can prove is that the two *look* the same, which is `STUDIO-07023` and the reference
screenshots.
