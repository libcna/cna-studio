// SPDX-License-Identifier: MS-PL
/**
 * @file StudioNativeParityTests.cpp
 * @brief Behaviours the prototype's end-to-end suite covered, asserted without the prototype.
 *
 * `plan.md` STUDIO-07047. `ApplicationTests.cpp` runs the Dear ImGui prototype's application over
 * a null UI and asserts on what it does to the document. Most of what it asserts is *shared* —
 * gizmo arithmetic, undo merging, the selection rules, the shortcut vocabulary — and almost none
 * of it is about Dear ImGui. Deleting that file would therefore delete a third of the suite and
 * nothing would say which third.
 *
 * So the migration inventory now accounts for tests as well as for panels and controls, and this
 * file is where the rows with no native answer were given one. Each case names the prototype case
 * it stands in for, because "why does this test exist" is a question this file will be asked.
 *
 * What is **not** here is the set of things the native shell cannot do at all — a manipulator in
 * the 3D view, an asset reloaded after an external edit, a loaded plugin, `--scene`, a list
 * property that can be added to. Those are `STUDIO-07050` … `STUDIO-07057`, and a test asserting
 * them today would be asserting a feature nobody has written.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/TransformGizmos.hpp"
#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <cmath>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 800.0f;
    constexpr float kHeight = 600.0f;

    UiInputState at(float x, float y)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = 1.0f / 60.0f;
        return input;
    }

    /** @brief One sprite at a known world position, selected, with the translate gizmo up. */
    struct ViewportFixture
    {
        StudioContext context;
        StudioCamera2D camera;
        StudioViewportState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioViewportResult last;
        Uuid entity;

        ViewportFixture()
        {
            StudioEntity subject{Uuid::generate(), "Subject"};

            StudioComponent transform{BuiltinComponentIds::kTransform};
            transform.setProperty("position", PropertyValue{StudioVector3{100.0f, 200.0f, 0.0f}});
            transform.setProperty("scale", PropertyValue{StudioVector3{1.0f, 1.0f, 1.0f}});
            subject.addComponent(std::move(transform));
            subject.addComponent(StudioComponent{BuiltinComponentIds::kSpriteRenderer});

            entity = context.getScene().addEntity(std::move(subject));
            camera.setViewportSize(StudioVector2{kWidth, kHeight});
            context.select(entity);
        }

        [[nodiscard]] SpriteSizeProvider sizes() const
        {
            return [](const Uuid&) { return StudioVector2{64.0f, 64.0f}; };
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioViewportResult drawn =
                    studioViewportPanel(f, body, context, camera, state, sizes());
                if (f.isInputPass()) { last = drawn; }
            });
        }

        void settle() { run(at(5.0f, 5.0f)); }

        [[nodiscard]] StudioVector3 position() const
        {
            return context.getScene()
                .findEntity(entity)
                ->findComponent(BuiltinComponentIds::kTransform)
                ->getProperty("position")
                .get<StudioVector3>();
        }

        void setPosition(const StudioVector3& value)
        {
            context.getScene()
                .findEntityForEdit(entity)
                ->findComponent(BuiltinComponentIds::kTransform)
                ->setProperty("position", PropertyValue{value});
        }

        void setRotationDegrees(float degrees)
        {
            const float radians = degrees * 3.14159265358979323846f / 180.0f;
            context.getScene()
                .findEntityForEdit(entity)
                ->findComponent(BuiltinComponentIds::kTransform)
                ->setProperty("rotation",
                              PropertyValue{StudioQuaternion{0.0f, 0.0f, std::sin(radians * 0.5f),
                                                             std::cos(radians * 0.5f)}});
        }

        [[nodiscard]] std::optional<TranslateGizmoLayout> translateLayout()
        {
            return computeTranslateGizmoLayout(context.getScene(), camera, entity, state.space);
        }

        /** @brief Presses at @p from, moves to @p to and releases, with @p control held. */
        void drag(StudioVector2 from, StudioVector2 to, bool control = false)
        {
            UiInputState hover = at(from.x, from.y);
            hover.modifiers.control = control;
            run(hover);

            UiInputState down = at(from.x, from.y);
            down.setMouseDown(UiMouseButton::Left, true);
            down.modifiers.control = control;
            run(down);

            UiInputState move = at(to.x, to.y);
            move.setMouseDown(UiMouseButton::Left, true);
            move.modifiers.control = control;
            run(move);

            UiInputState up = at(to.x, to.y);
            up.modifiers.control = control;
            run(up);
        }
    };
}

// ------------------------------------------------------------------------------------------------
// The gizmo, driven through the native viewport panel
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheCentreHandleMovesOnBothAxesAtOnce)
{
    // Stands in for the prototype's AGizmoDragOnTheCentreHandleMovesOnBothAxes. The arms constrain
    // and the centre does not, which is the whole reason the centre is there.
    ViewportFixture fixture;
    fixture.settle();

    const std::optional<TranslateGizmoLayout> layout = fixture.translateLayout();
    CNA_STUDIO_EXPECT(layout.has_value());
    if (!layout.has_value()) { return; }

    const StudioVector2 centre = layout->origin;
    fixture.drag(centre, StudioVector2{centre.x + 30.0f, centre.y - 20.0f});

    // Both axes, which is the whole reason the centre handle exists. Which *way* each one went is
    // the camera's business and `CameraRoundTripsBetweenWorldAndScreen` already asserts it.
    const StudioVector3 moved = fixture.position();
    CNA_STUDIO_EXPECT(moved.x != 100.0f);
    CNA_STUDIO_EXPECT(moved.y != 200.0f);
}

CNA_STUDIO_TEST(ADragSurvivesThePointerLeavingTheViewport)
{
    // Stands in for AGizmoDragSurvivesThePointerLeavingTheViewport. Ending the drag at the edge
    // would drop the entity wherever the pointer happened to cross it, which is the single most
    // annoying thing a manipulator can do.
    ViewportFixture fixture;
    fixture.settle();

    const std::optional<TranslateGizmoLayout> layout = fixture.translateLayout();
    CNA_STUDIO_EXPECT(layout.has_value());
    if (!layout.has_value()) { return; }

    const StudioVector2 arm = layout->getXTip();
    UiInputState down = at(arm.x, arm.y);
    down.setMouseDown(UiMouseButton::Left, true);
    fixture.run(at(arm.x, arm.y));
    fixture.run(down);
    CNA_STUDIO_EXPECT(fixture.state.dragging());

    // Still held, far outside the panel.
    UiInputState outside = at(kWidth + 200.0f, kHeight + 200.0f);
    outside.setMouseDown(UiMouseButton::Left, true);
    fixture.run(outside);
    CNA_STUDIO_EXPECT(fixture.state.dragging());

    // And it is still moving the entity rather than having stopped at the edge.
    UiInputState further = at(kWidth + 260.0f, kHeight + 200.0f);
    further.setMouseDown(UiMouseButton::Left, true);
    fixture.run(further);
    CNA_STUDIO_EXPECT(fixture.position().x > 100.0f);

    fixture.run(at(kWidth + 260.0f, kHeight + 200.0f));
    CNA_STUDIO_EXPECT(!fixture.state.dragging());
}

CNA_STUDIO_TEST(APressOnAHandleThatMovesNothingLeavesTheHistoryAlone)
{
    // Stands in for PressingAHandleWithoutMovingLeavesTheHistoryAlone. A user who grabs an arm and
    // thinks better of it must not find an undo entry that does nothing waiting for them.
    ViewportFixture fixture;
    fixture.settle();
    const std::size_t before = fixture.context.getHistory().getCount();

    const std::optional<TranslateGizmoLayout> layout = fixture.translateLayout();
    CNA_STUDIO_EXPECT(layout.has_value());
    if (!layout.has_value()) { return; }

    const StudioVector2 arm = layout->getXTip();
    fixture.drag(arm, arm);

    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), before);
    CNA_STUDIO_EXPECT_EQ(fixture.position().x, 100.0f);
}

CNA_STUDIO_TEST(APressAwayFromEveryHandleStillPicks)
{
    // Stands in for APressAwayFromEveryHandleStillSelects. The manipulator must not swallow presses
    // that missed it -- a viewport where selecting only works with nothing selected is unusable.
    ViewportFixture fixture;

    StudioEntity other{Uuid::generate(), "Other"};
    StudioComponent transform{BuiltinComponentIds::kTransform};
    transform.setProperty("position", PropertyValue{StudioVector3{-200.0f, -150.0f, 0.0f}});
    transform.setProperty("scale", PropertyValue{StudioVector3{1.0f, 1.0f, 1.0f}});
    other.addComponent(std::move(transform));
    other.addComponent(StudioComponent{BuiltinComponentIds::kSpriteRenderer});
    const Uuid otherId = fixture.context.getScene().addEntity(std::move(other));

    fixture.settle();

    const StudioVector2 screen = fixture.camera.worldToScreen(StudioVector2{-200.0f, -150.0f});
    fixture.drag(screen, screen);

    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() == otherId);
}

CNA_STUDIO_TEST(TheSnapModifierRoundsADragToTheVisibleGrid)
{
    // Stands in for HoldingTheSnapModifierRoundsAGizmoDragToTheGrid. The arithmetic is
    // ViewportTests' SnappingRoundsTheResultRatherThanTheMovement; what is asserted here is that
    // the modifier reaches it through the native panel at all.
    ViewportFixture fixture;
    fixture.setPosition(StudioVector3{103.0f, 200.0f, 0.0f});
    fixture.settle();

    const std::optional<TranslateGizmoLayout> layout = fixture.translateLayout();
    CNA_STUDIO_EXPECT(layout.has_value());
    if (!layout.has_value()) { return; }

    const StudioVector2 arm = layout->getXTip();
    fixture.drag(arm, StudioVector2{arm.x + 37.0f, arm.y}, /*control=*/true);

    // Whatever it landed on, it is a whole number of grid steps rather than 140.
    const float x = fixture.position().x;
    CNA_STUDIO_EXPECT(x != 140.0f);
    CNA_STUDIO_EXPECT(std::abs(x - std::round(x / 50.0f) * 50.0f) < 0.001f);
}

CNA_STUDIO_TEST(AProjectsOwnSnapStepWinsOverTheVisibleGrid)
{
    // Stands in for the prototype's case of the same name. A project that lays out on a 16-unit
    // tile grid says so once, in its own file, rather than in every drag.
    ViewportFixture fixture;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("cna-parity-" + Uuid::generate().toString());
    std::filesystem::create_directories(root);
    fixture.context.getProject() = Project::createDefault("Tiles", root.generic_string());
    CNA_STUDIO_EXPECT(
        fixture.context.getProject().saveToFile((root / "Tiles.cnaproject").generic_string()));
    fixture.context.getProject().setGridSnap(16.0f);

    fixture.setPosition(StudioVector3{103.0f, 200.0f, 0.0f});
    fixture.settle();

    const std::optional<TranslateGizmoLayout> layout = fixture.translateLayout();
    CNA_STUDIO_EXPECT(layout.has_value());
    if (layout.has_value())
    {
        const StudioVector2 arm = layout->getXTip();
        fixture.drag(arm, StudioVector2{arm.x + 37.0f, arm.y}, /*control=*/true);

        const float x = fixture.position().x;
        CNA_STUDIO_EXPECT(std::abs(x - std::round(x / 16.0f) * 16.0f) < 0.001f);
    }

    std::error_code code;
    std::filesystem::remove_all(root, code);
}

CNA_STUDIO_TEST(ALocalSpaceDragFollowsTheEntitysOwnAxis)
{
    // Stands in for the prototype's case of the same name. In world space a drag along X moves the
    // entity right whatever it is doing; in local space the same drag moves it *forwards*.
    ViewportFixture fixture;
    fixture.setRotationDegrees(90.0f);
    fixture.state.space = GizmoSpace::Local;
    fixture.settle();

    const std::optional<TranslateGizmoLayout> layout = fixture.translateLayout();
    CNA_STUDIO_EXPECT(layout.has_value());
    if (!layout.has_value()) { return; }

    // The X arm points along the entity's own X, which a quarter turn has put along world Y.
    const StudioVector2 arm = layout->getXTip();
    fixture.drag(arm, StudioVector2{arm.x + layout->xAxis.x * 40.0f,
                                    arm.y + layout->xAxis.y * 40.0f});

    const StudioVector3 moved = fixture.position();
    CNA_STUDIO_EXPECT(std::abs(moved.x - 100.0f) < 1.0f);
    CNA_STUDIO_EXPECT(std::abs(moved.y - 200.0f) > 10.0f);
}

// ------------------------------------------------------------------------------------------------
// The document and the shell, without the prototype's application around them
// ------------------------------------------------------------------------------------------------

namespace
{
    /** @brief A shell with every action and every panel bound, over a scene with two entities. */
    struct ShellFixture
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell{StudioTheme::dark()};
        StudioCamera2D camera;
        StudioCamera3D camera3D;
        StudioShellPanels panels{shell, context, log};

        Uuid parent;
        Uuid child;

        ShellFixture()
        {
            StudioEntity top{Uuid::generate(), "Parent"};
            top.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
            parent = context.getScene().addEntity(std::move(top));

            StudioEntity below{Uuid::generate(), "Child"};
            below.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
            child = context.getScene().addEntity(std::move(below));
            context.getScene().reparentEntity(child, parent);

            shell.resetLayout();
            CNA_STUDIO_EXPECT(bindStudioShellActions(shell, context, log) >= 4);
            panels.setViewportServices(camera, camera3D, {});
            shell.renderFrame(at(-1.0f, -1.0f));
        }

        /** @brief Presses a chord for one frame and releases it on the next. */
        void press(UiKey key, UiKeyModifiers modifiers = {})
        {
            UiInputState input = at(-1.0f, -1.0f);
            input.modifiers = modifiers;
            input.setKeyDown(key, true);
            shell.renderFrame(input);
            shell.renderFrame(at(-1.0f, -1.0f));
        }

        /** @brief Holds a chord for @p frames frames, then releases it. */
        void hold(UiKey key, int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                UiInputState input = at(-1.0f, -1.0f);
                input.setKeyDown(key, true);
                shell.renderFrame(input);
            }
            shell.renderFrame(at(-1.0f, -1.0f));
        }
    };
}

CNA_STUDIO_TEST(AStudioOpenedWithNoProjectStartsOnASceneSomethingCanBeSeenIn)
{
    // Stands in for ApplicationStartsWithAUsableEmptyScene, and it was a real gap: a bare
    // `StudioContext` holds an *empty* scene, the prototype's application called `newScene` when it
    // was given no project, and the native hosts did not — so `cna-studio` with no arguments opened
    // on a document with no camera in it, which renders nothing and reads as a broken editor.
    //
    // Asserted over the composition rather than over a host, because the hosts are the two places
    // that need a graphics device. Both call this now: `CnaStudioShellHost` and the headless shell
    // preview.
    StudioContext context;
    CNA_STUDIO_EXPECT_EQ(context.getScene().getEntityCount(), std::size_t{0});

    context.newScene("Untitled");
    CNA_STUDIO_EXPECT(context.getScene().getEntityCount() >= std::size_t{1});

    bool sawCamera = false;
    for (const StudioEntity& entity : context.getScene().getEntities())
    {
        if (entity.findComponent(BuiltinComponentIds::kCamera) != nullptr) { sawCamera = true; }
    }
    CNA_STUDIO_EXPECT(sawCamera);

    // And the World Outliner shows it, which is the half a user actually meets.
    StudioLog log;
    StudioShell shell{StudioTheme::dark()};
    StudioCamera2D camera;
    StudioCamera3D camera3D;
    StudioShellPanels panels{shell, context, log};
    panels.setViewportServices(camera, camera3D, {});
    shell.resetLayout();
    CNA_STUDIO_EXPECT(shell.activatePanel("outliner"));
    shell.renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(panels.counts().outlinerRowsTotal >= std::size_t{1});
}

CNA_STUDIO_TEST(DeletingAnEntityTakesItOutOfTheSelection)
{
    // Stands in for SelectionIsPrunedWhenItsEntityIsDeleted. A selection holding an id the scene no
    // longer has is a panel about to dereference nothing -- and it is not hypothetical: the
    // Details panel, the outliner and the gizmo all resolve the selection every frame.
    ShellFixture fixture;
    fixture.context.select(fixture.child);
    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() == fixture.child);

    fixture.context.execute(
        std::make_unique<DeleteEntityCommand>(fixture.context.getScene(), fixture.child));
    fixture.context.pruneSelection();

    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());

    // And undo brings both the entity and a usable selection back.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT(fixture.context.getScene().findEntity(fixture.child) != nullptr);
}

CNA_STUDIO_TEST(AProjectThatIsNotThereIsReportedRatherThanOpened)
{
    // Stands in for ApplicationReportsAMissingProjectRatherThanCrashing.
    StudioContext context;
    std::vector<std::string> messages;
    context.setLogSink([&messages](LogSeverity severity, const std::string& message) {
        if (severity == LogSeverity::Error) { messages.push_back(message); }
    });

    CNA_STUDIO_EXPECT(!context.openProject("/definitely/not/here/Missing.cnaproject"));
    CNA_STUDIO_EXPECT(!context.hasProject());
    CNA_STUDIO_EXPECT(!messages.empty());
}

CNA_STUDIO_TEST(DuplicateCopiesTheSubtreeWithFreshIdsAndSelectsTheCopy)
{
    // Stands in for the prototype's case of the same name. Duplicating a parent must bring its
    // children, give every copy a new id, and leave the *copy* selected -- otherwise the next drag
    // moves the original.
    ShellFixture fixture;
    fixture.context.select(fixture.parent);
    const std::size_t before = fixture.context.getScene().getEntityCount();

    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.edit.duplicate")
                      == StudioActionResult::Invoked);

    CNA_STUDIO_EXPECT_EQ(fixture.context.getScene().getEntityCount(), before * 2);
    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection().isValid());
    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() != fixture.parent);

    // The copy has a child of its own rather than sharing the original's.
    const std::vector<Uuid> children =
        fixture.context.getScene().getChildren(fixture.context.getPrimarySelection());
    CNA_STUDIO_EXPECT_EQ(children.size(), std::size_t{1});
    if (!children.empty()) { CNA_STUDIO_EXPECT(children.front() != fixture.child); }

    // One undo entry for the whole subtree.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.context.getScene().getEntityCount(), before);
}

CNA_STUDIO_TEST(DeletingASelectionIsOneUndoEntry)
{
    // Stands in for the prototype's case of the same name. Deleting three entities is one action to
    // the user, so undoing it must be one press rather than three.
    ShellFixture fixture;
    fixture.context.select(fixture.parent);
    fixture.context.toggleSelection(fixture.child);
    const std::size_t before = fixture.context.getHistory().getCount();

    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.edit.delete")
                      == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getScene().getEntityCount(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), before + 1);

    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.context.getScene().getEntityCount(), std::size_t{2});
}

CNA_STUDIO_TEST(AnArmedShortcutFiresExactlyOnce)
{
    // Stands in for the prototype's case of the same name. A key held down for twenty frames is one
    // press to the user; an editor that repeated the command would undo twenty steps from one tap.
    ShellFixture fixture;
    fixture.context.execute(
        std::make_unique<RenameEntityCommand>(fixture.context.getScene(), fixture.parent, "One"));
    fixture.context.execute(
        std::make_unique<RenameEntityCommand>(fixture.context.getScene(), fixture.parent, "Two"));
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCursor(), std::size_t{2});

    UiKeyModifiers control;
    control.control = true;
    for (int frame = 0; frame < 20; ++frame)
    {
        UiInputState input = at(-1.0f, -1.0f);
        input.modifiers = control;
        input.setKeyDown(UiKey::Z, true);
        fixture.shell.renderFrame(input);
    }
    fixture.shell.renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCursor(), std::size_t{1});
}

CNA_STUDIO_TEST(TheGizmoSpaceShortcutTogglesBothWays)
{
    // Stands in for the prototype's case of the same name. A toggle that only goes one way is a
    // control the user has to find another route back from.
    ShellFixture fixture;
    const GizmoSpace before = fixture.panels.viewportSpace();

    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.view.toggleGizmoSpace")
                      == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT(fixture.panels.viewportSpace() != before);

    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.view.toggleGizmoSpace")
                      == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT(fixture.panels.viewportSpace() == before);
}

CNA_STUDIO_TEST(TheDigitsSelectTheViewRatherThanOnlyTheMenu)
{
    // Stands in for TheDigitsSelectTheViewAndKeepWorkingWhileFlying, minus the flying half, which
    // is the case below.
    ShellFixture fixture;
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::TwoD);

    fixture.press(UiKey::Digit3);
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::ThreeD);

    fixture.press(UiKey::Digit2);
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::TwoD);
}

CNA_STUDIO_TEST(TheAddPickerDropsAUniqueComponentTheEntityAlreadyHas)
{
    // Stands in for the prototype's case of the same name. A unique component the entity already
    // carries cannot be added again -- `AddComponentCommand` refuses it -- and a picker that
    // offered it anyway would offer an entry that does nothing, which is indistinguishable from
    // one that is broken.
    StudioContext context;
    StudioEntity subject{Uuid::generate(), "Subject"};
    subject.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
    const Uuid entity = context.getScene().addEntity(std::move(subject));
    context.select(entity);

    // The transform is declared unique and required, so it must be missing from the list.
    const ComponentDescriptor* transform =
        context.getComponentRegistry().find(BuiltinComponentIds::kTransform);
    CNA_STUDIO_EXPECT(transform != nullptr);
    if (transform != nullptr) { CNA_STUDIO_EXPECT(transform->unique); }

    auto refused = std::make_unique<AddComponentCommand>(
        context.getScene(), context.getComponentRegistry(), entity,
        BuiltinComponentIds::kTransform);
    CNA_STUDIO_EXPECT(!refused->isValid());

    // And a non-unique one is not dropped: two audio sources on one entity is an ordinary thing.
    auto allowed = std::make_unique<AddComponentCommand>(
        context.getScene(), context.getComponentRegistry(), entity,
        BuiltinComponentIds::kAudioSource);
    CNA_STUDIO_EXPECT(allowed->isValid());
    context.execute(std::move(allowed));

    auto again = std::make_unique<AddComponentCommand>(
        context.getScene(), context.getComponentRegistry(), entity,
        BuiltinComponentIds::kAudioSource);
    CNA_STUDIO_EXPECT(again->isValid());
}

CNA_STUDIO_TEST(ARequiredComponentGetsNoRemoveButton)
{
    // Stands in for the prototype's case of the same name. A transform is what makes an entity an
    // entity, and the panel asks the *command* rather than deciding for itself -- two places
    // deciding what may be removed is one place that will eventually say a thing the other refuses.
    StudioContext context;
    StudioEntity subject{Uuid::generate(), "Subject"};
    subject.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
    subject.addComponent(StudioComponent{BuiltinComponentIds::kSpriteRenderer});
    const Uuid entity = context.getScene().addEntity(std::move(subject));

    RemoveComponentCommand required{context.getScene(), context.getComponentRegistry(), entity, 0};
    CNA_STUDIO_EXPECT(!required.isValid());

    RemoveComponentCommand optional{context.getScene(), context.getComponentRegistry(), entity, 1};
    CNA_STUDIO_EXPECT(optional.isValid());
}

CNA_STUDIO_TEST(CtrlClickExtendsTheOutlinerSelection)
{
    // Stands in for CtrlClickExtendsTheHierarchySelection. Selecting several entities from the
    // tree is how a user sets up the multi-entity drags the viewport tests already cover.
    ShellFixture fixture;
    fixture.context.select(fixture.parent);

    fixture.context.toggleSelection(fixture.child);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{2});

    // And ctrl-clicking one that is already in takes it back out, which is what makes it a toggle
    // rather than an add.
    fixture.context.toggleSelection(fixture.child);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getSelection().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(fixture.context.getPrimarySelection() == fixture.parent);
}

CNA_STUDIO_TEST(FlyingWithTheGizmoKeysDoesNotAlsoSwitchTheManipulator)
{
    // Stands in for TheGizmoKeysFlyRatherThanSwitchingManipulatorInTheThreeDimensionalView. W and
    // E are the translate and rotate shortcuts everywhere in Studio, and they are also two of the
    // six keys that fly the 3D camera. A W that flew *and* switched the manipulator would leave the
    // user turning things they meant to walk past.
    ShellFixture fixture;
    fixture.press(UiKey::Digit3);
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::ThreeD);
    CNA_STUDIO_EXPECT(fixture.panels.viewportMode() == GizmoMode::Translate);

    // Hold the right button over the viewport -- which is what arms flying -- and press E.
    const UiRect viewport = fixture.shell.panelBounds("viewport");
    CNA_STUDIO_EXPECT(!viewport.isEmpty());

    UiInputState flying = at(viewport.centerX(), viewport.centerY());
    flying.setMouseDown(UiMouseButton::Right, true);
    fixture.shell.renderFrame(flying);

    flying.setKeyDown(UiKey::E, true);
    fixture.shell.renderFrame(flying);

    CNA_STUDIO_EXPECT(fixture.panels.viewportMode() == GizmoMode::Translate);
}

CNA_STUDIO_TEST(TheGridPlaneCommandIsOfferedOnlyWhereItChangesSomething)
{
    // `STUDIO-07056`. The prototype offers *Grid on Ground Plane* in the View menu and offers it
    // only in the 3D view, where it does something. The 2D view has one plane and no choice to
    // make about it -- a command that visibly does nothing there is a bug report waiting to be
    // filed.
    //
    // Greyed out rather than hidden, which is the difference worth testing: a user who went looking
    // for the setting should find it and see why it is unavailable. A row that disappears teaches
    // them the feature does not exist.
    ShellFixture fixture;

    const StudioAction* action = fixture.shell.actions().find("studio.view.gridOnGroundPlane");
    CNA_STUDIO_EXPECT(action != nullptr);
    CNA_STUDIO_EXPECT(action->checkable);

    // The 2D view is where a Studio opens.
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::TwoD);
    CNA_STUDIO_EXPECT(action->isEnabled && !action->isEnabled());

    // Present, not absent: found by id above and still findable here.
    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.view.gridOnGroundPlane")
                      == StudioActionResult::Disabled);
    CNA_STUDIO_EXPECT(!fixture.panels.viewportGridOnGroundPlane());

    fixture.press(UiKey::Digit3);
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::ThreeD);

    const StudioAction* inThreeD = fixture.shell.actions().find("studio.view.gridOnGroundPlane");
    CNA_STUDIO_EXPECT(inThreeD != nullptr);
    CNA_STUDIO_EXPECT(inThreeD->isEnabled && inThreeD->isEnabled());
}

CNA_STUDIO_TEST(TheGridPlaneIsAPreferenceSoItSurvivesARestart)
{
    // Written to the preferences rather than to the viewport state, and the reason is mechanical:
    // the state is copied *from* the preferences every poll, so a command that wrote to the state
    // would be writing to something about to be overwritten and the setting would last one frame.
    //
    // It is also what makes the choice survive a restart without inventing a second place to keep
    // it -- which is the acceptance condition, and the half a user notices.
    ShellFixture fixture;
    fixture.press(UiKey::Digit3);
    CNA_STUDIO_EXPECT(fixture.panels.viewportView() == StudioViewportView::ThreeD);

    CNA_STUDIO_EXPECT(!fixture.panels.userPreferences().model().gridOnGroundPlane);

    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.view.gridOnGroundPlane")
                      == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT(fixture.panels.userPreferences().model().gridOnGroundPlane);

    // And the viewport state follows on the next poll, which is what the wireframe reads.
    fixture.shell.renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(fixture.panels.viewportGridOnGroundPlane());

    // Checkable means it says which answer is current, and pressing it again is the other one.
    const StudioAction* action = fixture.shell.actions().find("studio.view.gridOnGroundPlane");
    CNA_STUDIO_EXPECT(action != nullptr && action->isChecked && action->isChecked());

    CNA_STUDIO_EXPECT(fixture.shell.actions().invoke("studio.view.gridOnGroundPlane")
                      == StudioActionResult::Invoked);
    CNA_STUDIO_EXPECT(!fixture.panels.userPreferences().model().gridOnGroundPlane);
}

CNA_STUDIO_TEST(TheGridPlanePreferenceRoundTripsThroughItsFile)
{
    // The other half of "survives a restart": the setting has to reach disk in a form that reads
    // back. A preference the editor honours in memory and drops on save is one that works for
    // exactly as long as the session.
    StudioPreferences saved;
    saved.gridOnGroundPlane = true;

    StudioPreferences loaded;
    CNA_STUDIO_EXPECT(loaded.gridOnGroundPlane == false);

    loaded = studioPreferencesFromJson(studioPreferencesToJson(saved));
    CNA_STUDIO_EXPECT(loaded.gridOnGroundPlane);
    CNA_STUDIO_EXPECT(loaded == saved);
}

CNA_STUDIO_TEST(TwoSeparateScrubsOfOneFieldAreTwoUndoEntries)
{
    // `STUDIO-07055`. A merge key answers "is this the same *edit*" -- entity, component, property
    // -- and cannot answer "is this the same *interaction*", because two drags of one field are
    // identical by every property the key can see and differ only in that the user let go in
    // between. So the boundary has to be marked from outside, on a frame where nothing is being
    // dragged.
    //
    // The prototype has done that since gizmo drags existed (`isAnyItemActive`). The native shell
    // never did, because until now nothing on it merged -- except the material editor, which was
    // already pushing `MergeWithPrevious`, so two separate material edits were already folding into
    // one. A live defect with nothing to report it.
    ShellFixture fixture;

    const auto setPosition = [&](float y) {
        fixture.context.execute(
            std::make_unique<SetPropertyCommand>(
                fixture.context.getScene(), fixture.parent, "CNA.Transform", "position",
                PropertyValue{StudioVector3{0.0f, y, 0.0f}}),
            MergePolicy::MergeWithPrevious);
    };

    // One gesture: three pushes with no quiet frame between them.
    setPosition(1.0f);
    setPosition(2.0f);
    setPosition(3.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{1});

    // The frame the button came up on. `poll` is what the host runs every frame, and it is where
    // the chain is closed -- reading the frame the shell drew last, because poll runs before the
    // next one.
    fixture.panels.poll(0.0);

    // A second gesture, which must not fold into the first.
    setPosition(4.0f);
    setPosition(5.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{2});

    // And undoing once takes back the second gesture in full rather than one of its steps.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    const StudioEntity* entity = fixture.context.getScene().findEntity(fixture.parent);
    CNA_STUDIO_EXPECT(entity != nullptr);
    const StudioComponent* transform = entity->findComponent("CNA.Transform");
    CNA_STUDIO_EXPECT(transform != nullptr);
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().y, 3.0f);
}
