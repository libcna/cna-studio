// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioDetailsPanel.hpp
 * @brief The Details panel — the selected entity's components and their properties.
 *
 * `plan.md` STUDIO-07007.
 *
 * The third panel ported, and the first that *writes* to the document. That is the whole difference
 * between it and the outliner: showing a scene wrong is a bad afternoon, and editing one wrong is a
 * lost afternoon's work, so every edit here goes through the command history rather than touching
 * an entity directly.
 *
 * ### Committed, not continuous
 *
 * A field writes on Enter or on losing focus, never on each keystroke. A property bound to a field
 * that wrote per character would put one undo entry per letter, and would parse a number while it
 * is half-typed — `1e` on the way to `1e-3` is not a number, and rejecting it mid-word is how an
 * inspector becomes impossible to type into.
 *
 * ### What is editable, and what is honestly not yet
 *
 * Booleans, integers, floats, strings, enumerations and the two- and three-component vectors are
 * editable. Colours, quaternions, rectangles, references, lists and structures are *shown* with
 * what they hold and labelled as not editable yet — because a property nobody can see is worse than
 * one nobody can change, and a control that looked editable and silently did nothing would be worse
 * than both. `STUDIO-07018` is the rest, and it wants pickers rather than more text fields: a
 * colour typed as four numbers and a rotation typed as four is how an inspector gets a reputation.
 *
 * ### The audio preview belongs to the component, not to the entity
 *
 * `STUDIO-07044`. The prototype draws one preview per *entity*, found with `findComponent` — the
 * first audio source on it. `CNA.AudioSource` is declared `unique = false`, so an entity may carry
 * several, and the prototype's preview can only ever hear one of them. Here each source draws its
 * own, under its own properties, playing its own clip at its own volume, pan and pitch.
 */

#pragma once

#include "CNA/Studio/Core/PropertyValue.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <string>

namespace CNA::Studio
{
    class StudioAudio;
    class StudioContext;

    /**
     * @brief What an audio preview control did this frame.
     *
     * `plan.md` STUDIO-07044. Reported rather than logged here, like every other result this panel
     * returns: a panel that wrote to the Output Log would be one that has to be given a log, and
     * the binder that already has one is where the other panels' messages come from.
     */
    struct StudioAudioPreviewResult
    {
        /** @brief How many preview controls were drawn -- one per audio source, plus the asset's. */
        std::size_t controls = 0;

        /** @brief Play was pressed this frame. Input pass only. */
        bool played = false;

        /** @brief Stop was pressed this frame. Input pass only. */
        bool stopped = false;

        /**
         * @brief Whether the device actually took the clip.
         *
         * Separate from @ref played because the two differ and the difference is the user's to
         * know: a clip that will not load and a clip of silence sound identical, and only one of
         * them is something to fix.
         */
        bool started = false;

        /** @brief The clip a Play was asked for, by source path, for the message. */
        std::string clip;
    };

    /**
     * @brief What the Details panel may reach beyond the document.
     *
     * `plan.md` STUDIO-07044. The same shape as the viewport's services and for the same reason:
     * playing a sound needs CNA, exactly one module may link CNA, and the panel has to keep
     * working in a headless run where there is no audio device at all.
     */
    struct StudioDetailsServices
    {
        /**
         * @brief Plays one clip at a time. Unset means this build cannot play anything.
         *
         * A null seam draws the control *disabled and says why*, rather than hiding it or
         * offering a button that does nothing. A preview that is simply absent on a build without
         * audio is a feature the user cannot tell from one that was never written.
         */
        StudioAudio* audio = nullptr;
    };

    /** @brief What the Details panel did this frame. */
    struct StudioDetailsResult
    {
        /** @brief How many property rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many components the selected entity has. */
        std::size_t componentCount = 0;

        /**
         * @brief How many properties were shown as a summary because no editor handles their kind.
         *
         * Reported rather than left to be noticed. A kind that falls through to "(not editable
         * yet)" looks deliberate and reads as a decision, which is how one stays unimplemented
         * long after the widget it needed arrived — so a test can assert on this number instead of
         * on a screenshot nobody will look at twice.
         */
        std::size_t readOnlyProperties = 0;

        /** @brief A property was committed to the document this frame. Input pass only. */
        bool edited = false;

        /** @brief What was edited, for the log: `Transform.position`, say. */
        std::string editedProperty;

        /** @brief What the audio preview controls did. */
        StudioAudioPreviewResult audio;
    };

    /**
     * @brief Draws the Details panel and applies what the user committed.
     *
     * @param frame The frame.
     * @param bounds The panel's content rectangle.
     * @param context The editor. Its selection decides what is shown; its history receives edits.
     * @param services What the panel cannot reach itself -- the audio seam the preview plays
     *        through. Defaulted, so a build with no audio draws the same panel with the preview
     *        disabled rather than a different one.
     * @return What happened.
     */
    /**
     * @brief What a property editor may reach beyond the value it is editing.
     *
     * `plan.md` STUDIO-07045. Only the reference pickers need anything: an asset reference offers
     * every asset in the project and an entity reference every entity in the scene, so the editor
     * has to be able to see them. A null context offers neither and falls back to showing the id,
     * which is what an editor over a value with no document behind it can honestly do.
     */
    struct StudioPropertyEditContext
    {
        /** @brief The document, for the reference pickers. Null offers no picker. */
        const StudioContext* context = nullptr;

        /** @brief An entity that must not appear in an entity-reference picker -- itself. */
        Uuid excludeEntity;
    };

    /** @brief What one property row's editor produced. */
    struct StudioPropertyEditResult
    {
        /** @brief The new value, when the user committed one. */
        std::optional<PropertyValue> edited;

        /**
         * @brief Whether this kind has no editor and was shown as text.
         *
         * Reported rather than silently drawn, because "this property cannot be edited here" is
         * the answer a caller counting editable rows needs and is invisible in a capture.
         */
        bool readOnlyKind = false;
    };

    /**
     * @brief Draws the control for one property value and reports an edit.
     *
     * Extracted from `studioDetailsPanel` by `STUDIO-07045`, which needed the same editors over an
     * importer's settings rather than over a component's properties. It is the same code rather
     * than a second copy on purpose: a property grid that edited a float one way for a component
     * and another way for an asset would drift, and the drift would be invisible until somebody
     * compared two panels side by side.
     *
     * It draws the *control* only. The label, the row and the command the edit goes through belong
     * to the caller, because those are what differ: a component's edit is a `SetPropertyCommand`
     * against the scene and an importer setting's is a `SetImporterSettingCommand` against the
     * asset database.
     *
     * @param frame The frame.
     * @param control The control's rectangle, to the right of the label.
     * @param value The value to edit; its type chooses the control.
     * @param enumOptions The closed set for an enumeration, or empty to fall back to typing.
     * @param editing What the reference pickers may look at.
     * @return The edit, if the user made one.
     */
    StudioPropertyEditResult studioPropertyEditor(StudioFrame& frame, UiRect control,
                                                  const PropertyValue& value,
                                                  const std::vector<std::string>& enumOptions,
                                                  const StudioPropertyEditContext& editing);

    StudioDetailsResult studioDetailsPanel(StudioFrame& frame, const UiRect& bounds,
                                           StudioContext& context,
                                           const StudioDetailsServices& services = {});
}
