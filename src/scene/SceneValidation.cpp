// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneValidation.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/Scene/Tilemap.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Below this, a scale factor collapses the sprite to nothing on any display. */
        constexpr float kZeroScaleEpsilon = 1e-6f;

        SceneIssue makeIssue(SceneIssue::Severity severity,
                             std::string ruleId,
                             const EditorEntity& entity,
                             std::string componentTypeId,
                             std::string message)
        {
            SceneIssue issue;
            issue.severity = severity;
            issue.ruleId = std::move(ruleId);
            issue.entityId = entity.getId();
            issue.entityName = entity.getName();
            issue.componentTypeId = std::move(componentTypeId);
            issue.message = std::move(message);
            return issue;
        }

        /**
         * @brief True when @p entity and every one of its ancestors is enabled.
         *
         * A camera under a disabled parent is not going to render, so counting it as a competing
         * primary camera would report a conflict the user has already resolved -- by switching the
         * other one off, which is exactly how a person switches cameras.
         */
        bool isEffectivelyEnabled(const SceneDocument& scene, const EditorEntity& entity)
        {
            const EditorEntity* current = &entity;
            // Bounded by the entity count: SceneDocument forbids cycles, but a bound costs nothing
            // and turns a corrupted document into a wrong answer rather than a hang.
            for (std::size_t guard = 0; guard <= scene.getEntityCount(); ++guard)
            {
                if (!current->isEnabled()) { return false; }
                if (!current->getParentId().isValid()) { return true; }

                current = scene.findEntity(current->getParentId());
                if (current == nullptr) { return true; }
            }
            return true;
        }

        /** @brief True when the camera component is the one the game is meant to start with. */
        bool isPrimaryCamera(const EditorComponent& camera, const ComponentDescriptor* descriptor)
        {
            return camera.getPropertyOrDefault("isPrimary", descriptor).get<bool>(false);
        }

        void checkCameras(const SceneDocument& scene,
                          const ComponentRegistry& registry,
                          std::vector<SceneIssue>& issues)
        {
            const ComponentDescriptor* descriptor = registry.find(BuiltinComponentIds::kCamera);

            std::vector<const EditorEntity*> cameras;
            std::vector<const EditorEntity*> primaries;

            for (const EditorEntity& entity : scene.getEntities())
            {
                const EditorComponent* camera = entity.findComponent(BuiltinComponentIds::kCamera);
                if (camera == nullptr) { continue; }
                if (!isEffectivelyEnabled(scene, entity)) { continue; }

                cameras.push_back(&entity);
                if (isPrimaryCamera(*camera, descriptor)) { primaries.push_back(&entity); }
            }

            if (cameras.empty()) { return; }

            if (primaries.empty())
            {
                // Scene-wide rather than pinned to an entity: there is no offending entity to
                // select, and blaming an arbitrary camera for the absence of a decision would send
                // the user to the wrong row.
                SceneIssue issue;
                issue.severity = SceneIssue::Severity::Warning;
                issue.ruleId = "no-primary-camera";
                issue.message = "No camera is marked primary. The game has nothing to start with.";
                issues.push_back(std::move(issue));
                return;
            }

            if (primaries.size() == 1) { return; }

            // One issue per offending camera, so every row selects a real entity. A single
            // scene-wide issue would name them and select none of them.
            for (const EditorEntity* entity : primaries)
            {
                issues.push_back(makeIssue(
                    SceneIssue::Severity::Error, "duplicate-primary-camera", *entity,
                    BuiltinComponentIds::kCamera,
                    "Marked primary, and so are " + std::to_string(primaries.size() - 1) +
                        " other camera(s). Which one the game starts with is arbitrary."));
            }
        }

        /**
          * @brief Reports a scene with more than one enabled audio listener.
          *
          * XNA mixes 3D audio relative to one listener. Two is not louder or wider -- it is a
          * choice the runtime makes for the user, the same failure as two primary cameras, and
          * exactly as invisible until something sounds wrong from the wrong direction.
          */
        void checkListeners(const SceneDocument& scene, std::vector<SceneIssue>& issues)
        {
            std::vector<const EditorEntity*> listeners;
            for (const EditorEntity& entity : scene.getEntities())
            {
                if (entity.findComponent(BuiltinComponentIds::kAudioListener) == nullptr) { continue; }
                if (!isEffectivelyEnabled(scene, entity)) { continue; }
                listeners.push_back(&entity);
            }

            if (listeners.size() < 2) { return; }

            for (const EditorEntity* entity : listeners)
            {
                issues.push_back(makeIssue(
                    SceneIssue::Severity::Error, "duplicate-audio-listener", *entity,
                    BuiltinComponentIds::kAudioListener,
                    "One of " + std::to_string(listeners.size()) +
                        " enabled audio listeners. Which one the mix is relative to is arbitrary."));
            }
        }

        /**
         * @brief Reports fog that is switched on and cannot do anything (ED-407).
         *
         * The first rule here that belongs to no entity, which is why it builds its `SceneIssue`
         * by hand rather than through `makeIssue`: the scene's environment is a property of the
         * scene, so there is nothing for the panel's click-to-select to select. That is honest
         * rather than awkward -- an issue naming an entity that has nothing to do with it would be
         * worse.
         *
         * Both effects treat a zero-width fog band as no fog at all, so nothing is broken and
         * nothing needs guarding against. What is worth reporting is that a user who switched fog
         * on will otherwise keep checking the box they already checked.
         */
        void checkEnvironment(const SceneDocument& scene, std::vector<SceneIssue>& issues)
        {
            const SceneEnvironment& environment = scene.getEnvironment();
            if (!environment.fogEnabled) { return; }
            if (environment.fogEnd > environment.fogStart) { return; }

            SceneIssue issue;
            issue.severity = SceneIssue::Severity::Warning;
            issue.ruleId = "fog-band-is-empty";
            issue.message =
                "Fog is enabled, but it ends (" + std::to_string(environment.fogEnd)
                + ") at or before it starts (" + std::to_string(environment.fogStart)
                + "), so nothing is fogged.";
            issues.push_back(std::move(issue));
        }

        void checkCameraPlanes(const EditorEntity& entity,
                               const EditorComponent& camera,
                               const ComponentDescriptor* descriptor,
                               std::vector<SceneIssue>& issues)
        {
            const float nearPlane = camera.getPropertyOrDefault("nearPlane", descriptor).get<float>(0.0f);
            const float farPlane = camera.getPropertyOrDefault("farPlane", descriptor).get<float>(0.0f);

            if (nearPlane < farPlane) { return; }

            issues.push_back(makeIssue(
                SceneIssue::Severity::Error, "camera-planes-inverted", entity,
                BuiltinComponentIds::kCamera,
                "Near plane (" + std::to_string(nearPlane) + ") is not in front of the far plane (" +
                    std::to_string(farPlane) + "). The projection matrix is degenerate."));
        }

        void checkTransform(const EditorEntity& entity,
                            const EditorComponent& transform,
                            const ComponentDescriptor* descriptor,
                            std::vector<SceneIssue>& issues)
        {
            const EditorVector3 scale =
                transform.getPropertyOrDefault("scale", descriptor).get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});

            const bool collapsed = std::fabs(scale.x) < kZeroScaleEpsilon ||
                                   std::fabs(scale.y) < kZeroScaleEpsilon ||
                                   std::fabs(scale.z) < kZeroScaleEpsilon;
            if (!collapsed) { return; }

            // A warning, not an error: hiding something by scaling it to zero is legal, and the
            // scene still runs. It is reported because the usual cause is a scale that was never
            // set rather than one that was, and because the symptom -- nothing drawn -- looks
            // identical to a missing texture.
            issues.push_back(makeIssue(
                SceneIssue::Severity::Warning, "zero-scale", entity, BuiltinComponentIds::kTransform,
                "Scale is zero on at least one axis, so this entity and its children draw nothing. "
                "Disabling the entity says the same thing more clearly."));
        }

        void checkSprite(const EditorEntity& entity,
                         const EditorComponent& sprite,
                         std::vector<SceneIssue>& issues)
        {
            const PropertyValue texture = sprite.getProperty("texture");
            if (texture.getType() == PropertyType::AssetReference &&
                texture.get<PropertyValue::AssetReference>().id.isValid())
            {
                return;
            }

            // Deliberately separate from the missing-reference report: that one answers "this id
            // will not load", this one answers "there is no id at all". An empty slot is a normal
            // state while building a scene, so it is only ever a warning.
            issues.push_back(makeIssue(
                SceneIssue::Severity::Warning, "sprite-without-texture", entity,
                BuiltinComponentIds::kSpriteRenderer,
                "Sprite Renderer has no texture, so it draws nothing."));
        }

        /**
         * @brief Reports a tilemap that can never draw or be painted into.
         *
         * A tile size of zero is the trap the renderer's own comment warns about: the map draws
         * nothing, a brush lands on no cell, and neither says why. It is a *warning* rather than an
         * error because a tilemap added a moment ago and not yet configured is a normal state --
         * what is not normal is finding out about it by wondering why painting does nothing.
         */
        void checkTilemap(const EditorEntity& entity,
                          const EditorComponent& tilemap,
                          const ComponentDescriptor* descriptor,
                          std::vector<SceneIssue>& issues)
        {
            const auto width = tilemap.getPropertyOrDefault(TilemapKeys::kTileWidth, descriptor)
                                   .get<std::int64_t>(0);
            const auto height = tilemap.getPropertyOrDefault(TilemapKeys::kTileHeight, descriptor)
                                   .get<std::int64_t>(0);

            if (width > 0 && height > 0) { return; }

            issues.push_back(makeIssue(
                SceneIssue::Severity::Warning, "tilemap-without-tile-size", entity,
                BuiltinComponentIds::kTilemap,
                "Tilemap has a tile size of " + std::to_string(width) + "x" + std::to_string(height)
                    + ", so it draws nothing and cannot be painted into."));
        }

        /**
         * @brief Reports a sprite animation with no sheet, or with no frames to play.
         *
         * The animation drives the sprite it sits beside -- its sheet replaces the texture -- so an
         * animation without one leaves the entity drawing the placeholder and looking like a broken
         * asset reference, which is a different problem with a different fix.
         */
        void checkAnimation(const EditorEntity& entity,
                            const EditorComponent& animation,
                            std::vector<SceneIssue>& issues)
        {
            const PropertyValue sheet = animation.getProperty(SpriteAnimationKeys::kSheet);
            if (sheet.getType() != PropertyType::AssetReference
                || !sheet.get<PropertyValue::AssetReference>().id.isValid())
            {
                issues.push_back(makeIssue(
                    SceneIssue::Severity::Warning, "animation-without-sheet", entity,
                    BuiltinComponentIds::kSpriteAnimation,
                    "Sprite Animation has no sheet, so the sprite beside it draws a placeholder."));
            }

            const PropertyValue frames = animation.getProperty(SpriteAnimationKeys::kFrames);
            if (frames.getType() == PropertyType::List
                && !frames.get<PropertyValue::ListValue>().items.empty())
            {
                return;
            }

            issues.push_back(makeIssue(
                SceneIssue::Severity::Warning, "animation-without-frames", entity,
                BuiltinComponentIds::kSpriteAnimation,
                "Sprite Animation has no frames, so there is nothing to play."));
        }

        /**
         * @brief Reports a stored enum value that is not one of the declared options.
         *
         * General rather than layer-specific, though renaming a layer is what makes it fire in
         * practice: the project's layer list drives `CNA.Layer`'s options, so an entity left on
         * "Background" after that layer was renamed says so here instead of silently belonging to
         * nothing. The value is *kept*, not repaired -- rewriting it would decide for the user
         * which of the remaining layers they meant.
         */
        void checkEnums(const EditorEntity& entity,
                        const EditorComponent& component,
                        const ComponentDescriptor& descriptor,
                        std::vector<SceneIssue>& issues)
        {
            for (const PropertyDescriptor& property : descriptor.properties)
            {
                if (property.type != PropertyType::Enum || property.enumOptions.empty()) { continue; }

                const PropertyValue stored = component.getProperty(property.name);
                if (stored.getType() != PropertyType::Enum) { continue; }

                const std::string& name = stored.get<PropertyValue::EnumValue>().name;
                if (name.empty()) { continue; }
                if (std::find(property.enumOptions.begin(), property.enumOptions.end(), name)
                    != property.enumOptions.end())
                {
                    continue;
                }

                const std::string label =
                    property.displayName.empty() ? property.name : property.displayName;
                issues.push_back(makeIssue(
                    SceneIssue::Severity::Warning, "unknown-enum-value", entity, descriptor.typeId,
                    label + " is \"" + name + "\", which is not one of the choices this build "
                            "offers. It was kept rather than replaced."));
            }
        }

        void checkComponentSet(const EditorEntity& entity,
                               const ComponentRegistry& registry,
                               std::vector<SceneIssue>& issues)
        {
            std::vector<std::string> reportedDuplicates;

            for (const EditorComponent& component : entity.getComponents())
            {
                const ComponentDescriptor* descriptor = registry.find(component.getTypeId());
                if (descriptor == nullptr)
                {
                    issues.push_back(makeIssue(
                        SceneIssue::Severity::Warning, "unknown-component-type", entity,
                        component.getTypeId(),
                        "Component type \"" + component.getTypeId() +
                            "\" is not registered. Its data is preserved, but nothing can edit or "
                            "run it -- a plugin is probably missing."));
                    continue;
                }

                checkEnums(entity, component, *descriptor, issues);

                if (!descriptor->unique) { continue; }

                const std::size_t count = static_cast<std::size_t>(
                    std::count_if(entity.getComponents().begin(), entity.getComponents().end(),
                                  [&](const EditorComponent& other)
                                  { return other.getTypeId() == component.getTypeId(); }));
                if (count < 2) { continue; }

                if (std::find(reportedDuplicates.begin(), reportedDuplicates.end(), component.getTypeId()) !=
                    reportedDuplicates.end())
                {
                    continue;
                }
                reportedDuplicates.push_back(component.getTypeId());

                issues.push_back(makeIssue(
                    SceneIssue::Severity::Error, "duplicate-component", entity, component.getTypeId(),
                    "Has " + std::to_string(count) + " " + descriptor->displayName +
                        " components, but the type allows one. Which one wins is arbitrary."));
            }

            // Required means "cannot be removed", which is only enforceable on components an
            // entity already has. A scene file written by hand, or by an older build, can still
            // arrive without one -- and an entity with no Transform has no position at all.
            for (const std::string& typeId : registry.getTypeIds())
            {
                const ComponentDescriptor* descriptor = registry.find(typeId);
                if (descriptor == nullptr || !descriptor->required) { continue; }
                if (entity.findComponent(typeId) != nullptr) { continue; }

                issues.push_back(makeIssue(
                    SceneIssue::Severity::Error, "missing-required-component", entity, typeId,
                    "Has no " + descriptor->displayName +
                        " component, which every entity is required to have."));
            }
        }

        void checkEmptyEntity(const EditorEntity& entity,
                              const ComponentRegistry& registry,
                              const std::unordered_set<Uuid>& parents,
                              std::vector<SceneIssue>& issues)
        {
            // An entity with children is a group, and a group that carries nothing but a transform
            // is the normal way to move a set of things together.
            if (parents.count(entity.getId()) != 0) { return; }

            for (const EditorComponent& component : entity.getComponents())
            {
                const ComponentDescriptor* descriptor = registry.find(component.getTypeId());

                // An unknown type counts as doing something: the editor cannot see what it does,
                // which is not the same as knowing that it does nothing.
                if (descriptor == nullptr || !descriptor->required) { return; }
            }

            issues.push_back(makeIssue(
                SceneIssue::Severity::Warning, "empty-entity", entity, {},
                "Has no components beyond the required ones and no children, so it does nothing."));
        }
    }

    const char* toString(SceneIssue::Severity severity)
    {
        switch (severity)
        {
            case SceneIssue::Severity::Warning: return "warning";
            case SceneIssue::Severity::Error: return "error";
        }
        return "issue";
    }

    std::vector<SceneIssue> validateScene(const SceneDocument& scene, const ComponentRegistry& registry)
    {
        std::vector<SceneIssue> issues;

        checkEnvironment(scene, issues);
        checkCameras(scene, registry, issues);
        checkListeners(scene, issues);

        // Derived once rather than per entity: getChildren() is a scan, and asking it for every
        // entity would turn the report into O(n^2) on exactly the large scenes that need it most.
        std::unordered_set<Uuid> parents;
        for (const EditorEntity& entity : scene.getEntities())
        {
            if (entity.getParentId().isValid()) { parents.insert(entity.getParentId()); }
        }

        for (const EditorEntity& entity : scene.getEntities())
        {
            checkComponentSet(entity, registry, issues);

            if (const EditorComponent* transform = entity.findComponent(BuiltinComponentIds::kTransform))
            {
                checkTransform(entity, *transform, registry.find(BuiltinComponentIds::kTransform), issues);
            }

            if (const EditorComponent* sprite = entity.findComponent(BuiltinComponentIds::kSpriteRenderer))
            {
                checkSprite(entity, *sprite, issues);
            }

            if (const EditorComponent* tilemap = entity.findComponent(BuiltinComponentIds::kTilemap))
            {
                checkTilemap(entity, *tilemap, registry.find(BuiltinComponentIds::kTilemap), issues);
            }

            if (const EditorComponent* animation =
                    entity.findComponent(BuiltinComponentIds::kSpriteAnimation))
            {
                checkAnimation(entity, *animation, issues);
            }

            if (const EditorComponent* camera = entity.findComponent(BuiltinComponentIds::kCamera))
            {
                checkCameraPlanes(entity, *camera, registry.find(BuiltinComponentIds::kCamera), issues);
            }

            checkEmptyEntity(entity, registry, parents, issues);
        }

        return issues;
    }

    std::size_t countIssues(const std::vector<SceneIssue>& issues, SceneIssue::Severity severity)
    {
        return static_cast<std::size_t>(
            std::count_if(issues.begin(), issues.end(),
                          [severity](const SceneIssue& issue) { return issue.severity == severity; }));
    }
}

namespace CNA::Editor
{
    std::vector<SceneIssue> validateModelPartMaterials(const SceneDocument& scene,
                                                       const MeshProvider& meshes)
    {
        std::vector<SceneIssue> issues;
        if (!meshes) { return issues; }

        for (const EditorEntity& entity : scene.getEntities())
        {
            const EditorComponent* renderer =
                entity.findComponent(BuiltinComponentIds::kModelRenderer);
            if (renderer == nullptr) { continue; }

            const PropertyValue& listValue = renderer->getProperty("materials");
            if (listValue.getType() != PropertyType::List) { continue; }

            const auto& items = listValue.get<PropertyValue::ListValue>().items;
            if (items.empty()) { continue; }

            const Uuid modelId =
                renderer->getProperty("model").get<PropertyValue::AssetReference>().id;
            const MeshData* mesh = modelId.isValid() ? meshes(modelId) : nullptr;

            // Not imported yet is not wrong. A rule that fired while a scan was still running
            // would report every model in the project, once, for no reason a user could act on.
            if (mesh == nullptr) { continue; }

            for (const PropertyValue& item : items)
            {
                if (item.getType() != PropertyType::Structure) { continue; }

                // Bound to a named reference rather than used inline. `get<T>()` hands back a
                // *copy*, so a pointer from `find` into `item.get<...>().find(...)` points into a
                // temporary that dies at the end of the expression -- and the read that follows
                // returns whatever is left, which here was a silently empty part name. It cost a
                // debugging session; `SceneModels` gets it right by binding the same way.
                const auto& structure = item.get<PropertyValue::StructureValue>();
                const PropertyValue* partName = structure.find("part");
                if (partName == nullptr) { continue; }

                const std::string name = partName->get<std::string>();
                if (name.empty()) { continue; }

                bool found = false;
                for (const MeshPart& part : mesh->parts)
                {
                    if (part.name == name) { found = true; break; }
                }
                if (found) { continue; }

                issues.push_back(makeIssue(
                    SceneIssue::Severity::Warning, "model-part-not-found", entity,
                    BuiltinComponentIds::kModelRenderer,
                    "A material is assigned to a part named '" + name
                        + "', which this model does not have. It was probably renamed or removed."));
            }
        }

        return issues;
    }
}
