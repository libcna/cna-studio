// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShortcutEditor.cpp
 * @brief Rebinding a command by pressing the key, with the conflict named.
 */

#include "CNA/Studio/ShellPanels/StudioShortcutEditor.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /** @brief Keys that are only ever part of a chord, never the whole of one. */
        bool isModifierOrReserved(UiKey key)
        {
            // Escape cancels the capture and Tab moves the focus; binding a command to either
            // would take away the two keys a user needs to get *out* of the row they are in.
            return key == UiKey::None || key == UiKey::Escape || key == UiKey::Tab;
        }
    }

    std::string studioShortcutConflictMessage(const StudioActionRegistry& registry,
                                              const StudioShortcut& refused,
                                              const std::string& refusedBy)
    {
        if (refusedBy.empty()) { return {}; }

        const StudioAction* holder = registry.find(refusedBy);
        // The id rather than nothing: a command that has gone between the refusal and the frame
        // that reports it is a Studio defect, and a blank line hides it.
        std::string name = holder != nullptr ? holder->label : refusedBy;

        if (holder != nullptr && !holder->description.empty())
        {
            std::size_t namesakes = 0;
            for (const StudioAction& command : registry.commands())
            {
                if (command.label == holder->label) { ++namesakes; }
            }
            if (namesakes > 1)
            {
                std::string sentence = holder->description;
                while (!sentence.empty() && sentence.back() == '.') { sentence.pop_back(); }
                name += " — " + sentence;
            }
        }

        return describeStudioShortcut(refused) + " is already " + name + ".";
    }

    bool studioCapturedShortcut(const StudioFrame& frame, StudioShortcut& out)
    {
        for (int value = 1; value < static_cast<int>(UiKey::Count); ++value)
        {
            const auto key = static_cast<UiKey>(value);
            if (isModifierOrReserved(key)) { continue; }
            if (!frame.router().keyPressed(key)) { continue; }

            out.key = key;
            out.modifiers = frame.input().modifiers;
            return true;
        }
        return false;
    }

    StudioShortcutEditorResult studioShortcutEditor(StudioFrame& frame, const UiRect& body,
                                                    StudioActionRegistry& registry,
                                                    StudioPreferences& preferences,
                                                    StudioShortcutEditorState& state)
    {
        StudioShortcutEditorResult result;

        const StudioTheme& theme = frame.theme();
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        const float pad = metricOf(theme, StudioMetric::SpacingMedium);

        if (body.width <= 0.0f) { return result; }

        // Every command with a label, in registration order: the categories already group them the
        // way the menus do, and re-sorting would put a command somewhere the user has not seen it.
        std::vector<const StudioAction*> rows;
        rows.reserve(registry.commands().size());
        for (const StudioAction& command : registry.commands())
        {
            if (command.label.empty()) { continue; }
            rows.push_back(&command);
        }

        frame.ids().push("shortcuts");

        UiRect cursor = body;
        for (const StudioAction* command : rows)
        {
            if (cursor.height < rowHeight) { break; }

            const UiRect row = cursor.splitTop(rowHeight);
            cursor.splitTop(spacing);
            ++result.rowsDrawn;

            const bool capturing = state.capturing == command->id;

            UiRect line = row;
            const UiRect button = line.splitRight(
                std::min(line.width, std::ceil(studioLabelWidth(frame, "Change")) + pad * 2.0f));
            line.splitRight(spacing);

            const UiRect chordBox = line.splitRight(
                std::min(line.width, metricOf(theme, StudioMetric::PanelHeaderHeight) * 5.0f));
            line.splitRight(spacing);

            // Two columns rather than one label and a wide empty gap. Labels are written for the
            // menu they sit in, where the surrounding menu says what they are about: six commands
            // are called "Close" and six are called "Float", and in a flat list of every command
            // in Studio those twelve rows are indistinguishable -- a user rebinding one of them
            // would be guessing which panel they had just changed. The description says which,
            // and it is the field's documented purpose.
            const UiRect labelBox = line.splitLeft(std::min(line.width, body.width * 0.34f));
            line.splitLeft(spacing);
            const UiRect descriptionBox = line;

            if (frame.isDrawPass())
            {
                studioDrawText(frame, labelBox,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body),
                                                  command->label, labelBox.width),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextPrimary));

                studioDrawText(frame, descriptionBox,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body),
                                                  command->description, descriptionBox.width),
                               StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));

                // "Press a chord" rather than the old one while capturing: showing what it *was*
                // beside a row that is listening reads as though nothing is happening.
                const std::string chord = capturing
                    ? std::string{"Press a chord…"}
                    : (command->shortcut.isBound() ? describeStudioShortcut(command->shortcut)
                                                   : std::string{"—"});
                studioDrawText(frame, chordBox, chord, StudioFontRole::Monospace,
                               theme.color(capturing ? StudioColorRole::Accent
                                                     : StudioColorRole::TextSecondary));
            }

            StudioButtonOptions options;
            options.selected = capturing;
            options.tooltip = capturing ? "Press the chord you want, or Escape to cancel."
                                        : "Press to rebind this command.";
            if (studioButton(frame, frame.ids().make(command->id), button,
                             capturing ? "Cancel" : "Change", options).activated)
            {
                state.capturing = capturing ? std::string{} : command->id;
                state.clearRefusal();
            }
        }

        frame.ids().pop();
        result.contentHeight = static_cast<float>(result.rowsDrawn) * (rowHeight + spacing);

        // The refusal, under the list, where a user who has just pressed something reads next.
        if (!state.refusedBy.empty() && frame.isDrawPass() && cursor.height >= rowHeight)
        {
            studioDrawText(frame, cursor.splitTop(rowHeight),
                           studioShortcutConflictMessage(registry, state.refused, state.refusedBy),
                           StudioFontRole::Body, theme.color(StudioColorRole::Warning));
            result.contentHeight += rowHeight + spacing;
        }

        if (!frame.isInputPass() || state.capturing.empty()) { return result; }

        // Escape first: a user who has armed a row and thought better of it presses it, and a
        // capture that also took Escape would bind the command to the key that cancels it.
        if (frame.router().keyPressed(UiKey::Escape))
        {
            state.capturing.clear();
            state.clearRefusal();
            return result;
        }

        StudioShortcut pressed;
        if (!studioCapturedShortcut(frame, pressed)) { return result; }

        std::string conflict;
        if (!registry.rebind(state.capturing, pressed, &conflict))
        {
            // Left armed: the user's next act is to try a different chord, not to find the button
            // again. The refused chord and its owner are kept so the message can name both.
            state.refused = pressed;
            state.refusedBy = conflict;
            return result;
        }

        // Recorded as an override so it survives a restart, replacing any earlier one for this
        // command: two overrides for one command is two answers to what it is bound to.
        const auto existing = std::find_if(preferences.shortcuts.begin(), preferences.shortcuts.end(),
            [&](const StudioShortcutOverride& override) {
                return override.actionId == state.capturing;
            });
        if (existing != preferences.shortcuts.end()) { existing->shortcut = pressed; }
        else { preferences.shortcuts.push_back(StudioShortcutOverride{state.capturing, pressed}); }

        state.capturing.clear();
        state.clearRefusal();
        result.changed = true;
        return result;
    }
}
