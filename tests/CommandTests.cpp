// SPDX-License-Identifier: MS-PL
/**
 * @file CommandTests.cpp
 * @brief Tests for the undo/redo stack and every concrete scene command.
 *
 * Undo is the feature that cannot be retrofitted (ANALYSIS.md decision D-06), so it gets the
 * densest coverage in the suite -- including the cases editors habitually get wrong: the saved
 * marker moving in both directions, merging collapsing a drag into one entry, and restoring a
 * property that was previously *absent* rather than previously zero.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

using namespace CNA::Studio;

namespace
{
    ComponentRegistry makeRegistry()
    {
        ComponentRegistry registry;
        registerBuiltinComponents(registry);
        return registry;
    }

    StudioEntity makeEntity(const ComponentRegistry& registry, std::string name)
    {
        StudioEntity entity{Uuid::generate(), std::move(name)};
        StudioComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
        entity.addComponent(std::move(transform));
        return entity;
    }

    /** @brief A command that only counts its own execute/undo calls. */
    class CountingCommand final : public StudioCommand
    {
    public:
        CountingCommand(int& executeCount, int& undoCount)
            : executeCount_(&executeCount), undoCount_(&undoCount) {}

        void execute() override { ++*executeCount_; }
        void undo() override { ++*undoCount_; }
        [[nodiscard]] std::string getDescription() const override { return "Counting"; }

    private:
        int* executeCount_;
        int* undoCount_;
    };
}

CNA_STUDIO_TEST(HistoryExecutesUndoesAndRedoes)
{
    int executeCount = 0;
    int undoCount = 0;
    CommandHistory history;

    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    CNA_STUDIO_EXPECT_EQ(executeCount, 1);
    CNA_STUDIO_EXPECT(history.canUndo());
    CNA_STUDIO_EXPECT(!history.canRedo());

    CNA_STUDIO_EXPECT(history.undo());
    CNA_STUDIO_EXPECT_EQ(undoCount, 1);
    CNA_STUDIO_EXPECT(!history.canUndo());
    CNA_STUDIO_EXPECT(history.canRedo());

    CNA_STUDIO_EXPECT(history.redo());
    CNA_STUDIO_EXPECT_EQ(executeCount, 2);
    CNA_STUDIO_EXPECT(!history.redo());
}

CNA_STUDIO_TEST(HistoryDiscardsRedoTailOnNewCommand)
{
    int executeCount = 0;
    int undoCount = 0;
    CommandHistory history;

    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    history.undo();
    CNA_STUDIO_EXPECT(history.canRedo());

    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    CNA_STUDIO_EXPECT(!history.canRedo());
    CNA_STUDIO_EXPECT_EQ(history.getCount(), std::size_t{2});
}

CNA_STUDIO_TEST(HistoryTracksDirtyStateInBothDirections)
{
    int executeCount = 0;
    int undoCount = 0;
    CommandHistory history;

    CNA_STUDIO_EXPECT(!history.isDirty());

    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    CNA_STUDIO_EXPECT(history.isDirty());

    history.markSaved();
    CNA_STUDIO_EXPECT(!history.isDirty());

    // Undoing back past the save point must mark the document dirty again -- the file on disk no
    // longer matches what is in memory, even though the change count went down.
    history.undo();
    CNA_STUDIO_EXPECT(history.isDirty());

    history.redo();
    CNA_STUDIO_EXPECT(!history.isDirty());
}

CNA_STUDIO_TEST(HistoryHonoursItsRetentionLimit)
{
    int executeCount = 0;
    int undoCount = 0;
    CommandHistory history;
    history.setLimit(3);

    for (int index = 0; index < 10; ++index)
    {
        history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    }
    CNA_STUDIO_EXPECT_EQ(history.getCount(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(history.getCursor(), std::size_t{3});
    CNA_STUDIO_EXPECT(history.canUndo());
}

CNA_STUDIO_TEST(HistoryDescribesEveryEntryAndSaysWhereSavedIs)
{
    int executeCount = 0;
    int undoCount = 0;
    CommandHistory history;

    // What the history panel is a view over: a label per entry, and a position for the file on
    // disk. Both have to survive undo, or the panel would relabel itself as the user navigates.
    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    history.markSaved();
    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));

    CNA_STUDIO_EXPECT_EQ(history.getDescriptionAt(0), std::string{"Counting"});
    CNA_STUDIO_EXPECT_EQ(history.getDescriptionAt(1), std::string{"Counting"});
    CNA_STUDIO_EXPECT(history.getDescriptionAt(2).empty());
    CNA_STUDIO_EXPECT_EQ(history.getSavedCursor(), std::ptrdiff_t{1});

    history.undo();
    CNA_STUDIO_EXPECT_EQ(history.getSavedCursor(), std::ptrdiff_t{1});
    CNA_STUDIO_EXPECT_EQ(history.getDescriptionAt(1), std::string{"Counting"});

    // A new command discards the redo tail. When the saved state lived in that tail, no sequence
    // of undo and redo can return to it, and the panel must stop claiming any row is the file.
    history.undo();
    history.execute(std::make_unique<CountingCommand>(executeCount, undoCount));
    CNA_STUDIO_EXPECT(history.getSavedCursor() < 0);
    CNA_STUDIO_EXPECT(history.isDirty());
}

CNA_STUDIO_TEST(CreateEntityCommandUndoesCleanly)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    auto command = std::make_unique<CreateEntityCommand>(scene, makeEntity(registry, "Player"));
    const Uuid id = command->getEntityId();
    CNA_STUDIO_EXPECT(id.isValid());

    history.execute(std::move(command));
    CNA_STUDIO_EXPECT_EQ(scene.getEntityCount(), std::size_t{1});

    history.undo();
    CNA_STUDIO_EXPECT_EQ(scene.getEntityCount(), std::size_t{0});

    // Redo must restore the same id, or every reference to the entity would break on each redo.
    history.redo();
    CNA_STUDIO_EXPECT(scene.findEntity(id) != nullptr);
}

CNA_STUDIO_TEST(DeleteEntityCommandRestoresTheWholeSubtree)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid root = scene.addEntity(makeEntity(registry, "Root"));
    const Uuid middle = scene.addEntity(makeEntity(registry, "Middle"));
    const Uuid leaf = scene.addEntity(makeEntity(registry, "Leaf"));
    scene.reparentEntity(middle, root);
    scene.reparentEntity(leaf, middle);

    history.execute(std::make_unique<DeleteEntityCommand>(scene, root));
    CNA_STUDIO_EXPECT_EQ(scene.getEntityCount(), std::size_t{0});

    history.undo();
    CNA_STUDIO_EXPECT_EQ(scene.getEntityCount(), std::size_t{3});
    CNA_STUDIO_EXPECT(scene.findEntity(leaf) != nullptr);
    // The hierarchy has to come back intact, not as three loose roots.
    CNA_STUDIO_EXPECT(scene.findEntity(leaf)->getParentId() == middle);
    CNA_STUDIO_EXPECT(scene.findEntity(middle)->getParentId() == root);
    CNA_STUDIO_EXPECT_EQ(scene.getRootEntities().size(), std::size_t{1});
}

CNA_STUDIO_TEST(RenameAndReparentCommandsUndo)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid parent = scene.addEntity(makeEntity(registry, "Parent"));
    const Uuid child = scene.addEntity(makeEntity(registry, "Child"));

    history.execute(std::make_unique<RenameEntityCommand>(scene, child, "Renamed"));
    CNA_STUDIO_EXPECT_EQ(scene.findEntity(child)->getName(), std::string{"Renamed"});
    history.undo();
    CNA_STUDIO_EXPECT_EQ(scene.findEntity(child)->getName(), std::string{"Child"});

    history.execute(std::make_unique<ReparentEntityCommand>(scene, child, parent));
    CNA_STUDIO_EXPECT(scene.findEntity(child)->getParentId() == parent);
    history.undo();
    CNA_STUDIO_EXPECT(!scene.findEntity(child)->getParentId().isValid());
}

CNA_STUDIO_TEST(SetPropertyCommandUndoesToTheOriginalValue)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid id = scene.addEntity(makeEntity(registry, "Player"));

    history.execute(std::make_unique<SetPropertyCommand>(
        scene, id, BuiltinComponentIds::kTransform, "position",
        PropertyValue{StudioVector3{10.0f, 20.0f, 0.0f}}));

    const StudioComponent* transform = scene.findEntity(id)->findComponent(BuiltinComponentIds::kTransform);
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().x, 10.0f);

    history.undo();
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().x, 0.0f);
}

CNA_STUDIO_TEST(SetPropertyCommandRestoresAbsenceNotJustValue)
{
    // A scene file that omitted an optional field must be able to go back to omitting it, or an
    // undone edit would silently start writing a field the file never had.
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    StudioEntity entity{Uuid::generate(), "Sparse"};
    entity.addComponent(StudioComponent{BuiltinComponentIds::kTransform});
    const Uuid id = scene.addEntity(std::move(entity));

    const StudioComponent* transform = scene.findEntity(id)->findComponent(BuiltinComponentIds::kTransform);
    CNA_STUDIO_EXPECT(!transform->hasProperty("position"));

    history.execute(std::make_unique<SetPropertyCommand>(
        scene, id, BuiltinComponentIds::kTransform, "position", PropertyValue{StudioVector3{1.0f, 0.0f, 0.0f}}));
    CNA_STUDIO_EXPECT(transform->hasProperty("position"));

    history.undo();
    CNA_STUDIO_EXPECT(!transform->hasProperty("position"));
}

CNA_STUDIO_TEST(SetPropertyCommandMergesAcrossADrag)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid id = scene.addEntity(makeEntity(registry, "Player"));

    // Simulates a gizmo drag: many small property changes in one interaction.
    for (int step = 1; step <= 50; ++step)
    {
        history.execute(std::make_unique<SetPropertyCommand>(
                            scene, id, BuiltinComponentIds::kTransform, "position",
                            PropertyValue{StudioVector3{static_cast<float>(step), 0.0f, 0.0f}}),
                        MergePolicy::MergeWithPrevious);
    }

    // One drag must be one undo step, not fifty.
    CNA_STUDIO_EXPECT_EQ(history.getCount(), std::size_t{1});

    const StudioComponent* transform = scene.findEntity(id)->findComponent(BuiltinComponentIds::kTransform);
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().x, 50.0f);

    // ...and undoing it must return to where the drag started, not to step 49.
    history.undo();
    CNA_STUDIO_EXPECT_EQ(transform->getProperty("position").get<StudioVector3>().x, 0.0f);
}

CNA_STUDIO_TEST(SetPropertyCommandDoesNotMergeAcrossDifferentTargets)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid first = scene.addEntity(makeEntity(registry, "First"));
    const Uuid second = scene.addEntity(makeEntity(registry, "Second"));

    history.execute(std::make_unique<SetPropertyCommand>(
                        scene, first, BuiltinComponentIds::kTransform, "position",
                        PropertyValue{StudioVector3{1.0f, 0.0f, 0.0f}}),
                    MergePolicy::MergeWithPrevious);
    history.execute(std::make_unique<SetPropertyCommand>(
                        scene, second, BuiltinComponentIds::kTransform, "position",
                        PropertyValue{StudioVector3{2.0f, 0.0f, 0.0f}}),
                    MergePolicy::MergeWithPrevious);

    // Different entities: alternating between two objects must stay two undo steps.
    CNA_STUDIO_EXPECT_EQ(history.getCount(), std::size_t{2});

    history.execute(std::make_unique<SetPropertyCommand>(
                        scene, second, BuiltinComponentIds::kTransform, "scale",
                        PropertyValue{StudioVector3{2.0f, 2.0f, 2.0f}}),
                    MergePolicy::MergeWithPrevious);

    // Same entity, different property: still a separate step.
    CNA_STUDIO_EXPECT_EQ(history.getCount(), std::size_t{3});
}

CNA_STUDIO_TEST(AddAndRemoveComponentCommandsUndo)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid id = scene.addEntity(makeEntity(registry, "Player"));

    auto add = std::make_unique<AddComponentCommand>(scene, registry, id, BuiltinComponentIds::kSpriteRenderer);
    CNA_STUDIO_EXPECT(add->isValid());
    history.execute(std::move(add));

    const StudioEntity* entity = scene.findEntity(id);
    CNA_STUDIO_EXPECT(entity->findComponent(BuiltinComponentIds::kSpriteRenderer) != nullptr);
    // The new component must arrive populated with its declared defaults, not empty.
    CNA_STUDIO_EXPECT(entity->findComponent(BuiltinComponentIds::kSpriteRenderer)->hasProperty("tint"));

    history.undo();
    CNA_STUDIO_EXPECT(scene.findEntity(id)->findComponent(BuiltinComponentIds::kSpriteRenderer) == nullptr);

    history.redo();
    auto remove = std::make_unique<RemoveComponentCommand>(scene, registry, id, BuiltinComponentIds::kSpriteRenderer);
    CNA_STUDIO_EXPECT(remove->isValid());
    history.execute(std::move(remove));
    CNA_STUDIO_EXPECT(scene.findEntity(id)->findComponent(BuiltinComponentIds::kSpriteRenderer) == nullptr);

    history.undo();
    CNA_STUDIO_EXPECT(scene.findEntity(id)->findComponent(BuiltinComponentIds::kSpriteRenderer) != nullptr);
}

CNA_STUDIO_TEST(AddComponentCommandRefusesADuplicateUniqueComponent)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = scene.addEntity(makeEntity(registry, "Player"));

    const AddComponentCommand duplicate{scene, registry, id, BuiltinComponentIds::kTransform};
    CNA_STUDIO_EXPECT(!duplicate.isValid());

    const AddComponentCommand unknown{scene, registry, id, "Nope.NotRegistered"};
    CNA_STUDIO_EXPECT(!unknown.isValid());
}

CNA_STUDIO_TEST(RemoveComponentCommandRefusesARequiredComponent)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;

    const Uuid id = scene.addEntity(makeEntity(registry, "Player"));

    // Transform is marked required: an entity without one has no position at all.
    const RemoveComponentCommand removeTransform{scene, registry, id, BuiltinComponentIds::kTransform};
    CNA_STUDIO_EXPECT(!removeTransform.isValid());

    const RemoveComponentCommand removeAbsent{scene, registry, id, BuiltinComponentIds::kCamera};
    CNA_STUDIO_EXPECT(!removeAbsent.isValid());
}

CNA_STUDIO_TEST(AudioSourceIsRepeatableUnlikeTransform)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid id = scene.addEntity(makeEntity(registry, "Noisy"));

    auto first = std::make_unique<AddComponentCommand>(scene, registry, id, BuiltinComponentIds::kAudioSource);
    CNA_STUDIO_EXPECT(first->isValid());
    history.execute(std::move(first));

    auto second = std::make_unique<AddComponentCommand>(scene, registry, id, BuiltinComponentIds::kAudioSource);
    CNA_STUDIO_EXPECT(second->isValid());
    history.execute(std::move(second));

    CNA_STUDIO_EXPECT_EQ(scene.findEntity(id)->getComponents().size(), std::size_t{3});

    // Undo must remove exactly the one this command added, leaving the first in place.
    history.undo();
    CNA_STUDIO_EXPECT_EQ(scene.findEntity(id)->getComponents().size(), std::size_t{2});
    CNA_STUDIO_EXPECT(scene.findEntity(id)->findComponent(BuiltinComponentIds::kAudioSource) != nullptr);
}

CNA_STUDIO_TEST(RemovingByIndexTakesTheInstanceThatWasAskedFor)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid id = scene.addEntity(makeEntity(registry, "Noisy"));

    for (int instance = 0; instance < 2; ++instance)
    {
        auto add = std::make_unique<AddComponentCommand>(scene, registry, id,
                                                         BuiltinComponentIds::kAudioSource);
        CNA_STUDIO_EXPECT(add->isValid());
        history.execute(std::move(add));
    }

    // Tell the two apart, so "the wrong one was removed" is visible rather than merely plausible.
    scene.findEntityForEdit(id)->getComponents()[1].setProperty("volume", PropertyValue{0.25f});
    scene.findEntityForEdit(id)->getComponents()[2].setProperty("volume", PropertyValue{0.75f});

    // A type-based remove takes the first instance. The inspector needs the one the user clicked,
    // and the two look identical afterwards, so getting this wrong is silent.
    auto remove = std::make_unique<RemoveComponentCommand>(scene, registry, id, std::size_t{2});
    CNA_STUDIO_EXPECT(remove->isValid());
    history.execute(std::move(remove));

    const StudioEntity* entity = scene.findEntity(id);
    CNA_STUDIO_EXPECT_EQ(entity->getComponents().size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(entity->getComponents()[1].getProperty("volume").get<float>(), 0.25f);

    // Undo puts it back where it was, so the inspector's ordering survives.
    history.undo();
    CNA_STUDIO_EXPECT_EQ(scene.findEntity(id)->getComponents().size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(scene.findEntity(id)->getComponents()[2].getProperty("volume").get<float>(),
                         0.75f);
}

CNA_STUDIO_TEST(RemovingByIndexRefusesARequiredComponentAndAPastTheEndIndex)
{
    const ComponentRegistry registry = makeRegistry();
    SceneDocument scene;
    CommandHistory history;

    const Uuid id = scene.addEntity(makeEntity(registry, "Solid"));

    // Index 0 is the transform, which is required: an entity with no position is not an entity.
    RemoveComponentCommand removeTransform{scene, registry, id, std::size_t{0}};
    CNA_STUDIO_EXPECT(!removeTransform.isValid());

    RemoveComponentCommand removePastEnd{scene, registry, id, std::size_t{99}};
    CNA_STUDIO_EXPECT(!removePastEnd.isValid());

    CNA_STUDIO_EXPECT_EQ(scene.findEntity(id)->getComponents().size(), std::size_t{1});
}
