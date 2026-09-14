// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/InspectorPanel.hpp
 * @brief The property editor for the primary selection.
 */

#include <optional>
#include <string>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Panels/EditorPanel.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"

namespace CNA::Editor
{
    struct AssetRecord;
    class EditorEntity;

    /**
     * @brief Draws every component on the selected entity, and adds or removes them.
     *
     * Generic over component types it was never compiled against: one propertyField() call per
     * declared property is all it takes, which is the payoff of the descriptor system (D-05).
     */
    class InspectorPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

        /**
         * @brief Sets the frame time the animation preview advances on.
         *
         * Passed in rather than read from a clock, so a test steps the preview exactly and never
         * sleeps -- the same rule the asset watcher follows.
         */
        void setFrameDelta(double seconds) { frameDelta_ = seconds; }

    private:
        /** @brief Draws the selected asset's import settings, in place of the entity view. */
        void drawAssetInspector(const Uuid& assetId);

        /**
         * @brief Draws the open project's settings, shown when nothing is selected.
         *
         * The idle inspector used to say "Nothing selected" and stop. Project-level settings have
         * to be editable somewhere, there is no dialog system to put them in, and a panel that is
         * blank half the time is a panel with room in it.
         */
        void drawProjectInspector();

        /** @brief The scene's ambient and fog (ED-407), in the idle inspector. */
        void drawSceneEnvironment();

        /** @brief A `.cnamaterial`'s own fields, which no importer declares (ED-403). */
        void drawMaterialAsset(const AssetRecord& record);

        /**
         * @brief Draws the animation preview for an entity carrying `CNA.SpriteAnimation`.
         *
         * Playback lives here, in the panel, and is thrown away with it. A scene that recorded
         * which frame an artist happened to be paused on would carry that into every save and
         * every diff (D-07).
         */
        void drawAnimationPreview(const Uuid& entityId, double deltaSeconds);

        /**
         * @brief Draws Play and Stop for an entity carrying `CNA.AudioSource`.
         *
         * Uses the component's own volume, pitch and pan, so what the editor plays is what the
         * game will play -- a preview at some other level is a preview of a different sound.
         */
        void drawAudioPreview(const Uuid& entityId);

        /**
         * @brief Draws the prefab block for an entity that is part of an instance.
         *
         * Shows what the instance has changed, and offers the two answers to that: Revert puts the
         * instance back the way the prefab has it, Apply makes the prefab match the instance.
         */
        void drawPrefabSection(const Uuid& entityId);

        /** @brief Draws the "Add Component" picker for @p entity. */
        void drawAddComponentControl(const EditorEntity& entity);

        /** @brief A property change the user made this frame. */
        struct PropertyEdit
        {
            PropertyValue value;

            /**
             * @brief True when the change is a discrete action rather than a continuous one.
             *
             * Adding, removing or moving a list element is one action per press and must be one
             * undo entry per press. Dragging a slider is one action however many frames it spans,
             * and merges. The distinction cannot be made from the value alone -- both arrive as
             * "this property is now that" -- so the row that drew it says which it was.
             */
            bool structural = false;
        };

        /**
         * @brief Draws one property row, returning the new value when the user changed it.
         *
         * Quaternions are shown as Euler angles in degrees; lists get a collapsible block of rows;
         * everything else goes straight to the matching widget.
         */
        [[nodiscard]] std::optional<PropertyEdit> drawPropertyRow(const Uuid& entityId,
                                                                  const std::string& componentTypeId,
                                                                  const PropertyDescriptor& property,
                                                                  const PropertyValue& value);

        /**
         * @brief Draws a List property as a collapsible block of element rows.
         *
         * Every change -- an element edited, added, removed or moved -- comes back as the whole new
         * list. Rewriting the list wholesale is what makes each of them a plain SetPropertyCommand
         * that undoes correctly with no new command type; the lists an editor actually holds are
         * tens of elements, not millions.
         */
        [[nodiscard]] std::optional<PropertyEdit> drawListRows(const std::string& componentTypeId,
                                                               const PropertyDescriptor& property,
                                                               const PropertyValue& value);

        /**
         * @brief Takes a dropped asset for @p property, if one landed on the widget just drawn.
         * @return The new reference, or std::nullopt when nothing was dropped or the kind is wrong.
         */
        [[nodiscard]] std::optional<PropertyValue> acceptAssetDrop(const PropertyDescriptor& property);

        /** @brief Payload type for an asset dragged out of the browser. */
        static constexpr const char* kAssetDragType = "asset";

        /** @brief Which entity's animation is being previewed, and where the preview has got to. */
        Uuid previewEntity_;
        AnimationPlayback playback_;
        double frameDelta_ = 0.0;

        /**
         * @brief The component type the Add picker is showing.
         *
         * A type id rather than a list index: the list shortens the moment a unique component is
         * added, and an index would then quietly refer to a different type than the one on screen.
         */
        std::string addComponentChoice_;

        /**
         * @brief The Euler angles the user last typed, and the quaternion they produced.
         *
         * A quaternion has more than one Euler spelling, so converting back and forth every frame
         * would let the numbers jump while they are being edited -- type 90 into pitch and the yaw
         * and roll beside it flip to an equivalent pair. Remembering what was typed, and reusing it
         * for as long as the stored quaternion is still the one it produced, keeps the field steady
         * while still following an undo, a gizmo drag or a reload the instant one lands.
         */
        struct EulerEdit
        {
            Uuid entityId;
            std::string componentTypeId;
            std::string propertyName;
            EditorVector3 degrees;
            EditorQuaternion produced;

            [[nodiscard]] bool matches(const Uuid& entity, const std::string& component,
                                       const std::string& property) const
            {
                return entityId == entity && componentTypeId == component && propertyName == property;
            }
        };

        EulerEdit eulerEdit_;
    };
}
