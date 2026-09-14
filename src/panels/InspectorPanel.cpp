// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/InspectorPanel.hpp"

#include <fstream>
#include <iterator>

#include "CNA/Editor/Assets/AssetCommands.hpp"
#include "CNA/Editor/Assets/MaterialDocument.hpp"
#include "CNA/Editor/Scene/SceneEnvironment.hpp"

#include <memory>
#include <optional>
#include <vector>

#include "CNA/Editor/Assets/AssetCommands.hpp"
#include "CNA/Editor/EditorContext.hpp"
#include "CNA/Editor/PrefabWorkflow.hpp"
#include "CNA/Editor/ProjectCommands.hpp"
#include "CNA/Editor/Scene/PrefabCommands.hpp"
#include "CNA/Editor/Scene/PrefabDocument.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    void InspectorPanel::draw()
    {
        if (!ui_.beginPanel("Inspector", DockSide::Right)) { ui_.endPanel(); return; }

        // The animation preview belongs to whatever the inspector is showing, and is reset here
        // rather than inside the preview itself -- that code is not reached when an asset, or
        // nothing, is selected, and a preview left running against an entity the user navigated
        // away from would resume mid-clip on a frame nobody chose.
        const Uuid showing =
            context_.getSelectedAsset().isValid() ? Uuid{} : context_.getPrimarySelection();
        if (previewEntity_ != showing)
        {
            previewEntity_ = showing;
            playback_ = AnimationPlayback{};
        }

        // Cleared here and set again below when there is something to preview, so the viewport
        // never keeps drawing a frame from a clip the inspector has stopped showing.
        actions_.setAnimationPreview(AnimationPreview{});

        if (context_.getSelectedAsset().isValid())
        {
            drawAssetInspector(context_.getSelectedAsset());
            ui_.endPanel();
            return;
        }

        const Uuid selectedId = context_.getPrimarySelection();
        const EditorEntity* entity = context_.getScene().findEntity(selectedId);
        if (entity == nullptr)
        {
            drawProjectInspector();
            ui_.endPanel();
            return;
        }

        ui_.text("Entity: " + entity->getName());
        ui_.text("Id: " + entity->getId().toString());
        ui_.separator();

        drawPrefabSection(selectedId);
        drawAnimationPreview(selectedId, frameDelta_);
        drawAudioPreview(selectedId);

        // Removal is deferred past the loop. Executing it here would mutate the very vector being
        // iterated, and invalidate the component reference the loop body is holding.
        std::optional<std::size_t> removeIndex;

        const std::vector<EditorComponent>& components = entity->getComponents();
        for (std::size_t index = 0; index < components.size(); ++index)
        {
            const EditorComponent& component = components[index];
            const ComponentDescriptor* descriptor = context_.getComponentRegistry().find(component.getTypeId());
            if (descriptor == nullptr)
            {
                // An unregistered component still has to be visible, or the user has no way to
                // discover that a scene depends on a plugin that failed to load. It is removable:
                // dropping a component whose plugin is gone is a legitimate way to fix a scene.
                ui_.text(component.getTypeId() + "  (unregistered -- data preserved, not editable)");
                ui_.sameLine();
                if (ui_.button("Remove##" + std::to_string(index))) { removeIndex = index; }
                ui_.separator();
                continue;
            }

            ui_.text(descriptor->displayName);

            // A required component is what makes the entity what it is -- removing the transform
            // would leave it with no position -- so it gets no button rather than a dead one.
            if (!descriptor->required)
            {
                ui_.sameLine();
                // The "##index" suffix is what keeps two components of the same type from sharing
                // one button identity; ImGui derives a widget's id from its label.
                if (ui_.button("Remove##" + std::to_string(index))) { removeIndex = index; }
            }

            for (const PropertyDescriptor& property : descriptor->properties)
            {
                const PropertyValue value = component.getPropertyOrDefault(property.name, descriptor);
                const std::optional<PropertyEdit> edited =
                    drawPropertyRow(selectedId, component.getTypeId(), property, value);
                if (!edited) { continue; }

                // Merging means an inspector drag collapses into a single undo entry that returns
                // to the value the drag started from. A structural change -- adding, removing or
                // moving a list element -- is one action per press and gets its own entry, or
                // pressing Add three times would undo in one.
                context_.execute(std::make_unique<SetPropertyCommand>(
                                     context_.getScene(), selectedId, component.getTypeId(),
                                     property.name, edited->value),
                                 edited->structural ? MergePolicy::NewEntry
                                                    : MergePolicy::MergeWithPrevious);
            }
            ui_.separator();
        }

        drawAddComponentControl(*entity);
        ui_.endPanel();

        // Past every reference into the entity's component vector, so the mutation is safe.
        if (removeIndex)
        {
            auto command = std::make_unique<RemoveComponentCommand>(
                context_.getScene(), context_.getComponentRegistry(), selectedId, *removeIndex);
            if (command->isValid()) { context_.execute(std::move(command)); }
        }
    }

    std::optional<InspectorPanel::PropertyEdit> InspectorPanel::drawPropertyRow(
        const Uuid& entityId,
        const std::string& componentTypeId,
        const PropertyDescriptor& property,
        const PropertyValue& value)
    {
        const std::string label = property.displayName.empty() ? property.name : property.displayName;

        if (value.getType() == PropertyType::List)
        {
            return drawListRows(componentTypeId, property, value);
        }

        if (value.getType() != PropertyType::Quaternion)
        {
            PropertyValue edited = value;
            const bool changed = ui_.propertyField(label, edited, property.enumOptions, property.readOnly);

            // An asset slot is also a drop target for the browser, which is the only way to fill
            // one without copying a Uuid by hand.
            if (property.type == PropertyType::AssetReference && !property.readOnly)
            {
                if (const std::optional<PropertyValue> dropped = acceptAssetDrop(property))
                {
                    // A drop is one action, not a drag: it must not fold into whatever edit
                    // happened to precede it.
                    return PropertyEdit{*dropped, true};
                }
            }

            return changed ? std::optional<PropertyEdit>{PropertyEdit{edited, false}} : std::nullopt;
        }

        // Quaternions are shown as degrees, because four raw components are not something anyone
        // can author: rotating a sprite by 45 degrees should not require working out a quaternion.
        const EditorQuaternion stored = value.get<EditorQuaternion>();

        // Reuse what the user typed for as long as the stored value is still exactly the one it
        // produced. Recomputing every frame would let the other two angles jump to an equivalent
        // spelling mid-edit; comparing against our own output means an undo, a gizmo drag or a
        // reload is picked up immediately, because none of those produce that exact quaternion.
        const bool cacheApplies = eulerEdit_.matches(entityId, componentTypeId, property.name)
                               && eulerEdit_.produced == stored;

        PropertyValue shown{cacheApplies ? eulerEdit_.degrees : eulerDegreesOf(stored)};
        if (!ui_.propertyField(label + " (deg)", shown, {}, property.readOnly))
        {
            return std::nullopt;
        }

        const EditorVector3 degrees = shown.get<EditorVector3>();
        const EditorQuaternion produced = quaternionFromEulerDegrees(degrees);

        eulerEdit_ = EulerEdit{entityId, componentTypeId, property.name, degrees, produced};
        return PropertyEdit{PropertyValue{produced}, false};
    }

    std::optional<InspectorPanel::PropertyEdit> InspectorPanel::drawListRows(
        const std::string& componentTypeId,
        const PropertyDescriptor& property,
        const PropertyValue& value)
    {
        const std::string label = property.displayName.empty() ? property.name : property.displayName;
        const std::string id = "list-" + componentTypeId + "-" + property.name;

        PropertyValue::ListValue list = value.get<PropertyValue::ListValue>();

        const UiTreeNodeResult node =
            ui_.treeNode(id, label + "  (" + value.toDisplayString() + ")", false, false);
        if (!node.expanded) { return std::nullopt; }

        std::optional<PropertyEdit> edit;

        // Structural changes are recorded and applied after the loop. Erasing or swapping an
        // element while iterating would draw the rest of the rows against a list the user has not
        // seen, and the widget ids would shift underneath them mid-frame.
        std::optional<std::size_t> removeIndex;
        std::optional<std::size_t> moveUpIndex;

        for (std::size_t index = 0; index < list.items.size(); ++index)
        {
            const std::string rowId = "##" + id + "-" + std::to_string(index);

            PropertyValue item = list.items[index];

            // A structure element is one row per declared field rather than one widget: which
            // fields exist is on the descriptor, and only the descriptor knows what each of them
            // takes -- an asset field needs its kind filter, an enum needs its options.
            if (property.elementType == PropertyType::Structure)
            {
                PropertyValue::StructureValue structure = item.get<PropertyValue::StructureValue>();
                bool structureChanged = false;

                for (const PropertyDescriptor& field : property.structureFields)
                {
                    const PropertyValue* current = structure.find(field.name);
                    PropertyValue fieldValue = current != nullptr ? *current : field.defaultValue;

                    const std::string fieldLabel =
                        (field.displayName.empty() ? field.name : field.displayName)
                        + rowId + "-" + field.name;

                    if (ui_.propertyField(fieldLabel, fieldValue, field.enumOptions,
                                          property.readOnly || field.readOnly))
                    {
                        structure.set(field.name, std::move(fieldValue));
                        structureChanged = true;
                    }
                }

                if (structureChanged)
                {
                    list.items[index] = PropertyValue{std::move(structure)};
                    edit = PropertyEdit{PropertyValue{list}, false};
                }
            }
            else if (ui_.propertyField(std::to_string(index) + rowId, item, property.enumOptions,
                                       property.readOnly))
            {
                list.items[index] = std::move(item);
                edit = PropertyEdit{PropertyValue{list}, false};
            }

            if (property.readOnly) { continue; }

            // Not drawn at all at the ends, rather than drawn and ignored. A button that does
            // nothing when pressed is a bug report waiting to be filed.
            if (index > 0)
            {
                ui_.sameLine();
                if (ui_.button("Up" + rowId)) { moveUpIndex = index; }
            }
            if (index + 1 < list.items.size())
            {
                ui_.sameLine();
                if (ui_.button("Down" + rowId)) { moveUpIndex = index + 1; }
            }

            ui_.sameLine();
            if (ui_.button("Remove" + rowId)) { removeIndex = index; }
        }

        if (!property.readOnly && ui_.button("Add##" + id))
        {
            // The declared element type, not the type of whatever is already in there: an empty
            // list has nothing to copy from, and that is exactly when Add is pressed.
            // A structure starts as its declared fields at their declared defaults rather than
            // as an empty one: an element with no fields cannot be edited into having any, since
            // which fields exist is the descriptor's answer and not the value's.
            if (property.elementType == PropertyType::Structure)
            {
                PropertyValue::StructureValue fresh;
                for (const PropertyDescriptor& field : property.structureFields)
                {
                    fresh.set(field.name, field.defaultValue);
                }
                list.items.push_back(PropertyValue{std::move(fresh)});
            }
            else
            {
                list.items.push_back(PropertyValue::defaultOf(property.elementType));
            }
            edit = PropertyEdit{PropertyValue{list}, true};
        }

        ui_.treePop();

        if (removeIndex && *removeIndex < list.items.size())
        {
            list.items.erase(list.items.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
            edit = PropertyEdit{PropertyValue{list}, true};
        }
        else if (moveUpIndex && *moveUpIndex > 0 && *moveUpIndex < list.items.size())
        {
            std::swap(list.items[*moveUpIndex], list.items[*moveUpIndex - 1]);
            edit = PropertyEdit{PropertyValue{list}, true};
        }

        return edit;
    }

    std::optional<PropertyValue> InspectorPanel::acceptAssetDrop(const PropertyDescriptor& property)
    {
        const std::optional<std::string> dropped = ui_.acceptDrop(kAssetDragType);
        if (!dropped) { return std::nullopt; }

        const Uuid assetId = Uuid::parse(*dropped);
        const AssetRecord* record = context_.getAssets().find(assetId);
        if (record == nullptr)
        {
            context_.log(LogSeverity::Warning, "Dropped asset is no longer in the database.");
            return std::nullopt;
        }

        // A slot that declares what it takes refuses everything else, and says which is which.
        // Silently accepting a sound into a texture slot would produce a scene that loads and a
        // sprite that never appears, with nothing anywhere to explain it.
        if (!property.assetType.empty() && property.assetType != toString(record->type))
        {
            context_.log(LogSeverity::Warning,
                         "'" + record->sourcePath + "' is a " + std::string{toString(record->type)}
                             + "; this field takes a " + property.assetType + ".");
            return std::nullopt;
        }

        return PropertyValue{PropertyValue::AssetReference{assetId}};
    }

    void InspectorPanel::drawSceneEnvironment()
    {
        // Here, in the idle inspector beside the project settings, because that is where a setting
        // belonging to no entity already lives. It is the *scene's* though, not the project's, and
        // the heading says so -- two levels of one game can be lit differently and a user who
        // assumed otherwise would light them all at once.
        const UiTreeNodeResult node =
            ui_.treeNode("scene-environment", "Scene Environment", false, false);
        if (!node.expanded) { return; }

        SceneEnvironment environment = context_.getScene().getEnvironment();

        const auto apply = [&](const SceneEnvironment& edited, const std::string& field)
        {
            auto command = std::make_unique<SetSceneEnvironmentCommand>(context_.getScene(), edited,
                                                                        field);
            context_.execute(std::move(command));
        };

        PropertyValue ambient{environment.ambientColor};
        if (ui_.propertyField("Ambient", ambient))
        {
            SceneEnvironment edited = environment;
            edited.ambientColor = ambient.get<EditorColor>();
            apply(edited, "ambient light");
        }

        PropertyValue fogEnabled{environment.fogEnabled};
        if (ui_.propertyField("Fog", fogEnabled))
        {
            SceneEnvironment edited = environment;
            edited.fogEnabled = fogEnabled.get<bool>();
            apply(edited, "fog");
        }

        // The three fog fields only when fog is on. A colour and two distances that do nothing are
        // three controls a user has to test to discover are inert.
        if (environment.fogEnabled)
        {
            PropertyValue fogColor{environment.fogColor};
            if (ui_.propertyField("Fog Color", fogColor))
            {
                SceneEnvironment edited = environment;
                edited.fogColor = fogColor.get<EditorColor>();
                apply(edited, "fog colour");
            }

            PropertyValue fogStart{environment.fogStart};
            if (ui_.propertyField("Fog Start", fogStart))
            {
                SceneEnvironment edited = environment;
                edited.fogStart = fogStart.get<float>(edited.fogStart);
                apply(edited, "fog start");
            }

            PropertyValue fogEnd{environment.fogEnd};
            if (ui_.propertyField("Fog End", fogEnd))
            {
                SceneEnvironment edited = environment;
                edited.fogEnd = fogEnd.get<float>(edited.fogEnd);
                apply(edited, "fog end");
            }

            if (environment.fogEnd <= environment.fogStart)
            {
                // Said here as well as in the Validation panel: this is the one place the two
                // numbers are next to each other, and a user who has just typed them is the one
                // who can fix it in a second.
                ui_.text("    Fog ends before it starts, so nothing is fogged.");
            }
        }

        ui_.treePop();
        ui_.separator();
    }

    void InspectorPanel::drawProjectInspector()
    {
        if (!context_.hasProject())
        {
            ui_.text("Nothing selected. Open a project to edit its settings here.");
            return;
        }

        const Project& project = context_.getProject();
        ui_.text("Project: " + project.getName());
        ui_.text("Root: " + project.getRootPath());
        ui_.separator();

        // Before the layer tree, because it is one field rather than a list and a user looking for
        // a setting should not have to collapse a tree to find out there is one below it.
        PropertyValue snap{project.getGridSnap()};
        if (ui_.propertyField("Grid Snap", snap))
        {
            const float step = snap.get<float>(0.0f);
            auto snapCommand = std::make_unique<SetProjectGridSnapCommand>(context_.getProject(), step);
            if (snapCommand->isValid())
            {
                const std::string summary = snapCommand->getDescription();
                context_.execute(std::move(snapCommand));
                context_.log(LogSeverity::Info, summary + ".");
            }
        }
        ui_.text(project.getGridSnap() > 0.0f
                     ? "    World units a Ctrl-drag rounds to."
                     : "    Zero: a Ctrl-drag rounds to the grid the viewport is drawing.");
        ui_.separator();

        drawSceneEnvironment();

        std::vector<std::string> layers = project.getLayers();
        const std::string id = "project-layers";

        const UiTreeNodeResult node =
            ui_.treeNode(id, "Layers  (" + std::to_string(layers.size()) + ")", false, false);
        if (!node.expanded) { return; }

        bool changed = false;
        std::optional<std::size_t> removeIndex;
        std::optional<std::size_t> moveUpIndex;

        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            const std::string rowId = "##" + id + "-" + std::to_string(index);

            PropertyValue name{layers[index]};
            if (ui_.propertyField(std::to_string(index) + rowId, name))
            {
                // A blank name is refused rather than stored: a layer nothing can refer to and
                // everything can be mistaken for is worse than the name it had.
                const std::string edited = name.get<std::string>();
                if (!edited.empty() && edited != layers[index])
                {
                    layers[index] = edited;
                    changed = true;
                }
            }

            if (index > 0)
            {
                ui_.sameLine();
                if (ui_.button("Up" + rowId)) { moveUpIndex = index; }
            }
            if (index + 1 < layers.size())
            {
                ui_.sameLine();
                if (ui_.button("Down" + rowId)) { moveUpIndex = index + 1; }
            }

            // The last layer has no Remove button. A project with none has nothing for an entity
            // to be on, and the command would refuse it anyway -- so it is not offered.
            if (layers.size() > 1)
            {
                ui_.sameLine();
                if (ui_.button("Remove" + rowId)) { removeIndex = index; }
            }
        }

        if (ui_.button("Add##" + id))
        {
            layers.push_back("Layer " + std::to_string(layers.size()));
            changed = true;
        }

        ui_.treePop();

        if (removeIndex && *removeIndex < layers.size())
        {
            layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
            changed = true;
        }
        else if (moveUpIndex && *moveUpIndex > 0 && *moveUpIndex < layers.size())
        {
            // The order is the meaning -- index 0 draws first -- so moving a layer is a real edit,
            // not a cosmetic reordering of a list.
            std::swap(layers[*moveUpIndex], layers[*moveUpIndex - 1]);
            changed = true;
        }

        if (!changed) { return; }

        auto command = std::make_unique<SetProjectLayersCommand>(
            context_.getProject(), context_.getComponentRegistry(), std::move(layers));
        if (!command->isValid()) { return; }

        const std::string summary = command->getDescription();
        context_.execute(std::move(command));
        context_.log(LogSeverity::Info, summary + ".");
    }

    void InspectorPanel::drawMaterialAsset(const AssetRecord& record)
    {
        const std::string path = context_.getAssets().resolvePath(record.sourcePath);

        std::ifstream stream{path};
        if (!stream)
        {
            ui_.text("This material's file cannot be opened.");
            return;
        }

        const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        const JsonParseResult parsed = Json::parse(text);

        MaterialDocument material;
        if (!parsed.succeeded || !material.loadFromJson(parsed.value))
        {
            // Refused rather than shown as defaults. Offering an editable form over a file this
            // build could not read is offering to overwrite it with less than it holds.
            ui_.text("This material was written by a newer editor, or is not valid JSON.");
            return;
        }

        const auto apply = [&](const MaterialDocument& edited, const std::string& field)
        {
            auto command = std::make_unique<SetMaterialCommand>(path, edited, field);
            context_.execute(std::move(command));

            // Nothing to invalidate. The material provider reads the file on every ask rather than
            // caching it (see `EditorContext::makeMaterialProvider`), which is exactly what makes
            // an edit visible on the next frame with no cache to tell.
        };

        PropertyValue name{material.name};
        if (ui_.propertyField("Name", name))
        {
            MaterialDocument edited = material;
            edited.name = name.get<std::string>(edited.name);
            apply(edited, "name");
        }

        PropertyValue diffuse{material.diffuseColor};
        if (ui_.propertyField("Base Color", diffuse))
        {
            MaterialDocument edited = material;
            edited.diffuseColor = diffuse.get<EditorVector3>();
            apply(edited, "base colour");
        }

        PropertyValue emissive{material.emissiveColor};
        if (ui_.propertyField("Emissive", emissive))
        {
            MaterialDocument edited = material;
            edited.emissiveColor = emissive.get<EditorVector3>();
            apply(edited, "emissive");
        }

        PropertyValue metallic{material.metallic};
        if (ui_.propertyField("Metallic", metallic))
        {
            MaterialDocument edited = material;
            edited.metallic = metallic.get<float>(edited.metallic);
            apply(edited, "metallic");
        }

        PropertyValue roughness{material.roughness};
        if (ui_.propertyField("Roughness", roughness))
        {
            MaterialDocument edited = material;
            edited.roughness = roughness.get<float>(edited.roughness);
            apply(edited, "roughness");
        }

        PropertyValue alpha{material.alpha};
        if (ui_.propertyField("Alpha", alpha))
        {
            MaterialDocument edited = material;
            edited.alpha = alpha.get<float>(edited.alpha);
            apply(edited, "alpha");
        }

        // Said plainly rather than left to be discovered: which effect a build got decides whether
        // metallic and roughness reach the screen at all, and on a BasicEffect build they are
        // still not wasted -- the specular colour and power are derived from them.
        ui_.separator();
        ui_.text("Drawn through " + actions_.getViewport().getModelEffectName() + ".");
    }

    void InspectorPanel::drawAssetInspector(const Uuid& assetId)
    {
        const AssetRecord* record = context_.getAssets().find(assetId);
        if (record == nullptr)
        {
            ui_.text("This asset is no longer in the database.");
            return;
        }

        ui_.text("Asset: " + record->sourcePath);
        ui_.text("Id: " + record->id.toString());
        ui_.text(std::string{"Type: "} + toString(record->type));

        // Offered on the asset itself as well as on a component that references it. Hearing a clip
        // is most often wanted right after importing it, when no entity uses it yet.
        if (record->type == AssetType::SoundEffect || record->type == AssetType::Song)
        {
            if (ui_.button("Play##assetAudio"))
            {
                // Neutral settings here, unlike the component preview: this is the file as
                // imported, with nothing an entity chose applied to it.
                const bool started = actions_.getAudio().play(assetId, 1.0f, 0.0f, 0.0f);
                context_.log(started ? LogSeverity::Info : LogSeverity::Warning,
                             started ? "Playing '" + record->sourcePath + "'."
                                     : "Cannot play '" + record->sourcePath + "'.");
            }
            ui_.sameLine();
            if (ui_.button("Stop##assetAudio")) { actions_.getAudio().stop(); }
        }

        // A material is the editor's own document rather than something imported, so it gets its
        // fields here instead of an importer's settings form (ED-403).
        if (record->type == AssetType::Material)
        {
            ui_.separator();
            drawMaterialAsset(*record);
            return;
        }

        ui_.separator();

        const ComponentDescriptor* descriptor = context_.getImporterRegistry().find(record->importerId);
        if (descriptor == nullptr)
        {
            // An importer with no declared settings is not a fault -- most have nothing worth
            // choosing. Saying so beats an empty form the user waits for something to appear in.
            ui_.text(record->importerId.empty() ? "No importer for this file type."
                                                : record->importerId + " has no settings.");
            return;
        }

        ui_.text(descriptor->displayName);

        for (const PropertyDescriptor& property : descriptor->properties)
        {
            // The stored setting when the sidecar carries one, the declared default otherwise.
            // Writing every default into the sidecar on first sight would make each asset's diff
            // noise, so absent stays absent until the user actually chooses something.
            const JsonValue& stored = record->importerSettings[property.name];
            PropertyValue value = stored.isNull() ? property.defaultValue
                                                  : PropertyValue::fromJson(stored, property.type);

            const std::string label = property.displayName.empty() ? property.name : property.displayName;
            if (!ui_.propertyField(label, value, property.enumOptions, property.readOnly)) { continue; }

            auto command = std::make_unique<SetImporterSettingCommand>(
                context_.getAssets(), assetId, property.name, value);
            if (!command->isValid()) { continue; }

            // Merging, like every other inspector field: dragging a slider is one undo entry that
            // returns to the value the drag started from.
            context_.execute(std::move(command), MergePolicy::MergeWithPrevious);
        }
    }

    void InspectorPanel::drawAnimationPreview(const Uuid& entityId, double deltaSeconds)
    {
        const EditorEntity* entity = context_.getScene().findEntity(entityId);
        const EditorComponent* animation =
            entity != nullptr ? entity->findComponent(BuiltinComponentIds::kSpriteAnimation) : nullptr;
        if (animation == nullptr) { return; }

        const SpriteAnimationClip clip = readSpriteAnimationClip(
            *animation, context_.getComponentRegistry().find(BuiltinComponentIds::kSpriteAnimation));

        // The frame list is editable while the preview runs, and shortening it can leave the
        // position past the end.
        playback_.clampTo(clip);
        playback_.advance(clip, static_cast<float>(deltaSeconds));

        if (clip.frames.empty())
        {
            ui_.text("Animation: no frames yet.");
            ui_.separator();
            return;
        }

        // Published so the viewport draws the same frame this preview shows. The playback stays
        // here; only the result travels.
        actions_.setAnimationPreview(AnimationPreview{entityId, playback_.position});

        std::string heading = "Frame " + std::to_string(playback_.position + 1) + " of "
                            + std::to_string(clip.frames.size()) + "  ("
                            + std::to_string(static_cast<int>(clip.getDuration() * 1000.0f)) + " ms)";

        // The current frame's own hold, but only when the frames differ. Repeating one number for
        // every frame of a uniform clip is noise.
        if (clip.hasFrameDurations())
        {
            heading += "  this frame "
                     + std::to_string(static_cast<int>(clip.getFrameDuration(playback_.position) * 1000.0f))
                     + " ms";
        }
        ui_.text(heading);

        if (ui_.button(playback_.playing ? "Pause##anim" : "Play##anim"))
        {
            playback_.playing = !playback_.playing;
        }
        ui_.sameLine();
        if (ui_.button("<##anim")) { playback_.step(clip, -1); }
        ui_.sameLine();
        if (ui_.button(">##anim")) { playback_.step(clip, 1); }

        const Uuid sheetId =
            animation->getProperty(SpriteAnimationKeys::kSheet).get<PropertyValue::AssetReference>().id;
        const AssetRecord* record = context_.getAssets().find(sheetId);
        if (record == nullptr)
        {
            ui_.text("Assign a sheet to see the frames.");
            ui_.separator();
            return;
        }

        // The sheet's size comes from the importer's recorded fact rather than from the renderer:
        // it is the one place that already knows, and asking the renderer would mean the preview
        // could not be drawn at all in a headless run.
        const EditorVector2 sheetSize =
            PropertyValue::fromJson(record->importerSettings["pixelSize"], PropertyType::Vector2)
                .get<EditorVector2>();

        const UiTextureId texture = actions_.getViewport().getAssetThumbnail(sheetId);
        const EditorRectangle frame = clip.getFrameRectangle(playback_.position);

        constexpr float kPreviewSize = 128.0f;
        ui_.imageRegion("animation-preview", texture, frame, sheetSize, kPreviewSize, kPreviewSize);

        ui_.separator();
    }

    void InspectorPanel::drawAudioPreview(const Uuid& entityId)
    {
        const EditorEntity* entity = context_.getScene().findEntity(entityId);
        const EditorComponent* source =
            entity != nullptr ? entity->findComponent(BuiltinComponentIds::kAudioSource) : nullptr;
        if (source == nullptr) { return; }

        const ComponentDescriptor* descriptor =
            context_.getComponentRegistry().find(BuiltinComponentIds::kAudioSource);

        const Uuid clipId = source->getProperty("clip").get<PropertyValue::AssetReference>().id;
        const AssetRecord* record = context_.getAssets().find(clipId);
        if (record == nullptr)
        {
            ui_.text("Assign a clip to hear it.");
            ui_.separator();
            return;
        }

        if (ui_.button("Play##audio"))
        {
            // The component's own settings, not neutral ones: a preview at some other level is a
            // preview of a different sound, and "why is this quiet in game" is exactly the question
            // a preview exists to answer.
            const bool started = actions_.getAudio().play(
                clipId,
                source->getPropertyOrDefault("volume", descriptor).get<float>(1.0f),
                source->getPropertyOrDefault("pitch", descriptor).get<float>(0.0f),
                source->getPropertyOrDefault("pan", descriptor).get<float>(0.0f));

            // Reported rather than swallowed: a clip that will not load and a clip of silence
            // sound identical, and only one of them is the user's problem to fix.
            context_.log(started ? LogSeverity::Info : LogSeverity::Warning,
                         started ? "Playing '" + record->sourcePath + "'."
                                 : "Cannot play '" + record->sourcePath + "'.");
        }

        ui_.sameLine();
        if (ui_.button("Stop##audio")) { actions_.getAudio().stop(); }

        ui_.separator();
    }

    void InspectorPanel::drawPrefabSection(const Uuid& entityId)
    {
        // Answered for the *instance*, not for the entity: selecting a child of an instance should
        // still tell the user what it is part of and let them act on it.
        const Uuid instanceRoot = findInstanceRoot(context_.getScene(), entityId);
        if (!instanceRoot.isValid()) { return; }

        const Uuid assetId = getPrefabAssetOf(context_.getScene(), instanceRoot);
        const AssetRecord* record = context_.getAssets().find(assetId);
        if (record == nullptr)
        {
            // The link survives the asset going away, so it can be reported instead of vanishing.
            // An instance whose prefab was deleted is exactly what a user needs told.
            ui_.text("Prefab: missing (" + assetId.toString() + ")");
            ui_.separator();
            return;
        }

        PrefabDocument prefab;
        const PrefabLoadResult loaded =
            prefab.loadFromFile(context_.getAssets().resolvePath(record->sourcePath),
                                context_.getComponentRegistry());
        if (!loaded.succeeded)
        {
            ui_.text("Prefab: '" + record->sourcePath + "' will not load");
            ui_.separator();
            return;
        }

        const std::vector<PrefabOverride> overrides =
            findPrefabOverrides(context_.getScene(), instanceRoot, prefab,
                                context_.getComponentRegistry());

        ui_.text("Prefab: " + prefab.getName());
        ui_.text(overrides.empty() ? "No changes from the prefab."
                                   : std::to_string(overrides.size()) + " change(s) from the prefab");

        // Three at most, and a count. The list exists to make the divergence recognisable, not to
        // enumerate it -- the same reason the missing-reference report shows three users.
        for (std::size_t index = 0; index < overrides.size() && index < 3; ++index)
        {
            const PrefabOverride& entry = overrides[index];
            std::string line = std::string{"    "} + toString(entry.kind) + ": " + entry.entityName;
            if (!entry.propertyName.empty()) { line += "." + entry.propertyName; }
            ui_.text(line);
        }
        if (overrides.size() > 3)
        {
            ui_.text("    and " + std::to_string(overrides.size() - 3) + " more");
        }

        bool revert = false;
        bool apply = false;
        if (!overrides.empty())
        {
            revert = ui_.button("Revert##prefab");
            ui_.sameLine();
            apply = ui_.button("Apply##prefab");
        }

        ui_.separator();

        if (revert)
        {
            auto command = std::make_unique<RevertPrefabInstanceCommand>(
                context_.getScene(), instanceRoot, prefab);
            if (command->isValid())
            {
                const std::string summary = command->getDescription();
                context_.execute(std::move(command));
                context_.pruneSelection();
                context_.log(LogSeverity::Info, summary + ".");
            }
            return;
        }

        if (!apply) { return; }

        auto command = std::make_unique<ApplyPrefabInstanceCommand>(
            context_.getScene(), context_.getAssets(), context_.getComponentRegistry(), instanceRoot);
        if (!command->isValid())
        {
            context_.log(LogSeverity::Warning, "Cannot apply: " + command->getError());
            return;
        }

        const std::string summary = command->getDescription();
        const ApplyPrefabInstanceCommand* raw = command.get();
        context_.execute(std::move(command));

        if (!raw->getError().empty())
        {
            context_.log(LogSeverity::Error, "Cannot write the prefab: " + raw->getError());
            return;
        }
        context_.log(LogSeverity::Info, summary + ".");
    }

    void InspectorPanel::drawAddComponentControl(const EditorEntity& entity)
    {
        std::vector<std::string> labels;
        std::vector<std::string> typeIds;

        for (const std::string& typeId : context_.getComponentRegistry().getTypeIds())
        {
            const ComponentDescriptor* descriptor = context_.getComponentRegistry().find(typeId);
            if (descriptor == nullptr) { continue; }

            // A unique component the entity already has cannot be added again, so listing it would
            // be listing an entry that does nothing -- AddComponentCommand would refuse it anyway.
            if (descriptor->unique && entity.findComponent(typeId) != nullptr) { continue; }

            labels.push_back(descriptor->category.empty()
                                 ? descriptor->displayName
                                 : descriptor->category + " / " + descriptor->displayName);
            typeIds.push_back(typeId);
        }

        if (labels.empty())
        {
            ui_.text("Every component type is already on this entity.");
            return;
        }

        // The choice is remembered as a *type id*, not as an index: the list shortens the moment a
        // unique component is added, and an index would then silently point at a different type.
        std::size_t chosen = 0;
        for (std::size_t index = 0; index < typeIds.size(); ++index)
        {
            if (typeIds[index] == addComponentChoice_) { chosen = index; break; }
        }
        addComponentChoice_ = typeIds[chosen];

        ui_.setNextItemWidth(190.0f);

        PropertyValue value{PropertyValue::EnumValue{labels[chosen]}};
        if (ui_.propertyField("##addComponentType", value, labels))
        {
            const std::string picked = value.get<PropertyValue::EnumValue>().name;
            for (std::size_t index = 0; index < labels.size(); ++index)
            {
                if (labels[index] == picked) { addComponentChoice_ = typeIds[index]; break; }
            }
        }

        ui_.sameLine();

        if (ui_.button("Add Component"))
        {
            auto command = std::make_unique<AddComponentCommand>(
                context_.getScene(), context_.getComponentRegistry(), entity.getId(), addComponentChoice_);
            if (command->isValid()) { context_.execute(std::move(command)); }
        }
    }
}
