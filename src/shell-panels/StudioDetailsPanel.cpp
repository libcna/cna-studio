// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDetailsPanel.cpp
 * @brief The Details panel.
 */

#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /**
         * @brief Formats a float the way a person would type it back.
         *
         * Not `%f`: a position of 3 should read "3", not "3.000000", and a scale of 0.5 should not
         * read "0.500000" in a field somebody is about to edit. `%g` with enough significant
         * figures round-trips a float without printing the noise of its binary representation.
         */
        std::string formatFloat(float value)
        {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
            return buffer;
        }

        /**
         * @brief Parses a float, refusing anything with characters left over.
         *
         * "3abc" is not three. Accepting a prefix is how a typo silently becomes a value the user
         * did not enter and cannot see is wrong.
         */
        bool parseFloat(const std::string& text, float& out)
        {
            try
            {
                std::size_t consumed = 0;
                const float parsed = std::stof(text, &consumed);
                while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed])))
                {
                    ++consumed;
                }
                if (consumed != text.size()) { return false; }
                out = parsed;
                return true;
            }
            catch (const std::exception&) { return false; }
        }

        /** @brief Parses an integer, refusing anything with characters left over. */
        bool parseInteger(const std::string& text, std::int64_t& out)
        {
            try
            {
                std::size_t consumed = 0;
                const long long parsed = std::stoll(text, &consumed);
                while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed])))
                {
                    ++consumed;
                }
                if (consumed != text.size()) { return false; }
                out = parsed;
                return true;
            }
            catch (const std::exception&) { return false; }
        }

        /**
         * @brief Draws @p count numeric boxes across @p bounds, one per named component.
         *
         * Every composite value the inspector edits — a vector, a quaternion, a rectangle, a
         * colour — is a row of numbers with different labels on them, and writing that loop once
         * is what keeps the column widths, the font, the select-all behaviour and the parsing the
         * same across all of them. Six copies is how a property grid ends up with one field that
         * commits per keystroke and five that do not.
         *
         * @param frame The frame.
         * @param bounds Where the row of boxes goes.
         * @param names One id per component, also used as the placeholder.
         * @param values In, and out for whichever boxes committed.
         * @param count How many components.
         * @param integral True to parse as whole numbers, which is what a rectangle holds.
         * @return True when any box committed a new value.
         */
        bool numericComponents(StudioFrame& frame, const UiRect& bounds, const char* const* names,
                               float* values, int count, bool integral = false)
        {
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingSmall);
            UiRect fields = bounds;
            const float fieldWidth =
                (fields.width - spacing * static_cast<float>(count - 1)) / static_cast<float>(count);

            bool changed = false;
            for (int i = 0; i < count; ++i)
            {
                const UiRect box = fields.splitLeft(std::min(fieldWidth, fields.width));
                if (i + 1 < count) { fields.splitLeft(std::min(spacing, fields.width)); }

                std::string text = integral
                    ? std::to_string(static_cast<std::int64_t>(values[i]))
                    : formatFloat(values[i]);

                StudioTextFieldOptions options;
                options.font = StudioFontRole::Monospace;
                options.selectAllOnFocus = true;
                // The component's own letter, so a row of four boxes says which is which without
                // a second row of labels above it.
                options.placeholder = names[i];

                if (!studioTextField(frame, frame.ids().make(names[i]), box, text, options)
                         .committed)
                {
                    continue;
                }

                if (integral)
                {
                    std::int64_t parsed = 0;
                    if (parseInteger(text, parsed))
                    {
                        values[i] = static_cast<float>(parsed);
                        changed = true;
                    }
                }
                else
                {
                    float parsed = 0.0f;
                    if (parseFloat(text, parsed)) { values[i] = parsed; changed = true; }
                }
            }
            return changed;
        }

        /** @brief Clamps a float to the 0..255 a colour channel holds. */
        std::uint8_t toChannel(float value)
        {
            const float clamped = std::min(255.0f, std::max(0.0f, value));
            return static_cast<std::uint8_t>(clamped + 0.5f);
        }

        /** @brief A one-line summary of a value the panel cannot yet edit. */
        std::string summarise(const PropertyValue& value)
        {
            if (value.getType() == PropertyType::AssetReference)
            {
                const Uuid id = value.get<PropertyValue::AssetReference>().id;
                return id.isValid() ? "asset " + id.toString() : "(no asset)";
            }
            if (value.getType() == PropertyType::EntityReference)
            {
                const Uuid id = value.get<PropertyValue::EntityReference>().id;
                return id.isValid() ? "entity " + id.toString() : "(no entity)";
            }
            if (value.getType() == PropertyType::List)
            {
                const std::size_t count = value.get<PropertyValue::ListValue>().items.size();
                return std::to_string(count) + (count == 1 ? " item" : " items");
            }
            if (value.getType() == PropertyType::Structure)
            {
                const std::size_t count = value.get<PropertyValue::StructureValue>().fields.size();
                return std::to_string(count) + (count == 1 ? " field" : " fields");
            }
            return "(not editable yet)";
        }

        /** @brief The row layout every property shares: a label on the left, a control on the right. */
        struct PropertyRow
        {
            UiRect label;
            UiRect control;
        };

        PropertyRow splitRow(const StudioTheme& theme, UiRect row)
        {
            PropertyRow parts;
            // A fixed fraction rather than the widest label. Labels change as the selection does,
            // and a column that resized with them would make every control on screen jump each
            // time somebody clicked a different entity.
            const float labelWidth = std::round(row.width * 0.38f);
            parts.label = row.splitLeft(std::min(labelWidth, row.width));
            row.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingSmall), row.width));
            parts.control = row;
            return parts;
        }
    }

    StudioDetailsResult studioDetailsPanel(StudioFrame& frame, const UiRect& bounds,
                                           StudioContext& context)
    {
        StudioDetailsResult result;
        const StudioTheme& theme = frame.theme();

        const float padding = metricOf(theme, StudioMetric::SpacingSmall);
        const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

        UiRect area = bounds.inset(UiEdges{padding});
        if (area.width <= 0.0f || area.height <= 0.0f) { return result; }

        const std::vector<Uuid>& selection = context.getSelection();
        if (selection.empty())
        {
            if (frame.isDrawPass())
            {
                // Which nothing it is. "Select something to see its details" is a next action;
                // "nothing to show" is a dead end.
                studioDrawText(frame, area,
                               context.hasProject() ? "Select an entity to see its details."
                                                    : "No project is open.",
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        StudioEntity* entity = context.getScene().findEntity(selection.back());
        if (entity == nullptr)
        {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, area, "The selected entity is no longer in the scene.",
                               StudioFontRole::Body, theme.color(StudioColorRole::Warning));
            }
            return result;
        }

        const Uuid entityId = entity->getId();
        result.componentCount = entity->getComponents().size();

        // Every row measured before any is drawn, because the scroll view has to know how tall the
        // content is before it can decide whether it needs a bar.
        const std::size_t componentRows = [&] {
            std::size_t rows = 0;
            for (const StudioComponent& component : entity->getComponents())
            {
                ++rows;  // the component's own header
                const ComponentDescriptor* descriptor =
                    context.getComponentRegistry().find(component.getTypeId());
                rows += descriptor != nullptr ? descriptor->properties.size()
                                              : component.getProperties().size();
            }
            return rows;
        }();

        const std::size_t totalRows = componentRows + 3;  // name, enabled, a gap

        StudioScrollOptions scroll;
        scroll.contentHeight = static_cast<float>(totalRows) * (rowHeight + spacing);
        scroll.wheelStep = (rowHeight + spacing) * 3.0f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("detailsscroll"), area, scroll);

        // The cursor starts above the viewport by the scroll offset, so rows land where the
        // scroll position says rather than where the panel does. Rows that fall outside are
        // clipped by the scroll view; a tall inspector is a few dozen rows, not a few thousand,
        // so laying them all out costs nothing worth culling for.
        UiRect cursor = view.viewport;
        cursor.y -= view.offsetY;
        cursor.height += view.offsetY;

        const auto nextRow = [&]() {
            const UiRect row = cursor.splitTop(rowHeight);
            cursor.splitTop(spacing);
            ++result.rowsDrawn;
            return row;
        };

        // --- The entity itself -------------------------------------------------------------
        {
            const PropertyRow parts = splitRow(theme, nextRow());
            if (frame.isDrawPass())
            {
                studioDrawText(frame, parts.label, "Name", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            std::string name = entity->getName();
            StudioTextFieldOptions options;
            options.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("name"), parts.control, name, options)
                    .committed
                && name != entity->getName())
            {
                context.execute(std::make_unique<RenameEntityCommand>(context.getScene(), entityId,
                                                                      name));
                result.edited = true;
                result.editedProperty = "name";
            }
        }
        {
            const PropertyRow parts = splitRow(theme, nextRow());
            if (frame.isDrawPass())
            {
                studioDrawText(frame, parts.label, "Enabled", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            bool enabled = entity->isEnabled();
            if (studioCheckbox(frame, frame.ids().make("enabled"), parts.control, {}, enabled)
                    .changed)
            {
                // Through the history like every other edit. This was the one change in the panel
                // that Ctrl+Z could not reach, and it is the one somebody does by accident.
                auto command = std::make_unique<SetEntityEnabledCommand>(context.getScene(),
                                                                         entityId, enabled);
                if (command->isValid())
                {
                    context.execute(std::move(command));
                    result.edited = true;
                    result.editedProperty = "enabled";
                }
            }
        }

        nextRow();

        // --- Components ---------------------------------------------------------------------
        for (const StudioComponent& component : entity->getComponents())
        {
            const ComponentDescriptor* descriptor =
                context.getComponentRegistry().find(component.getTypeId());

            {
                const UiRect header = nextRow();
                if (frame.isDrawPass())
                {
                    frame.drawList().fillRect(header, theme.color(StudioColorRole::PanelHeader));
                    studioDrawText(frame,
                                   header.inset(UiEdges{metricOf(theme, StudioMetric::SpacingSmall),
                                                        0.0f,
                                                        metricOf(theme, StudioMetric::SpacingSmall),
                                                        0.0f}),
                                   descriptor != nullptr && !descriptor->displayName.empty()
                                       ? descriptor->displayName
                                       : component.getTypeId(),
                                   StudioFontRole::Subheading,
                                   theme.color(StudioColorRole::TextPrimary));
                }
            }

            const std::vector<PropertyDescriptor>* properties =
                descriptor != nullptr ? &descriptor->properties : nullptr;

            // A component the registry does not know still shows what it holds. A scene authored
            // by a plugin that is not loaded must be readable, or opening it looks like data loss.
            std::vector<PropertyDescriptor> improvised;
            if (properties == nullptr)
            {
                for (const auto& [name, value] : component.getProperties())
                {
                    PropertyDescriptor field;
                    field.name = name;
                    field.displayName = name;
                    improvised.push_back(std::move(field));
                }
                properties = &improvised;
            }

            for (const PropertyDescriptor& property : *properties)
            {
                const PropertyRow parts = splitRow(theme, nextRow());
                const std::string& label =
                    property.displayName.empty() ? property.name : property.displayName;

                if (frame.isDrawPass())
                {
                    studioDrawText(frame, parts.label,
                                   studioTruncateText(frame, theme.font(StudioFontRole::Body),
                                                      label, parts.label.width),
                                   StudioFontRole::Body,
                                   theme.color(StudioColorRole::TextSecondary));
                }

                const PropertyValue value = component.getPropertyOrDefault(property.name, descriptor);

                frame.ids().push(component.getTypeId());
                frame.ids().push(property.name);

                std::optional<PropertyValue> edited;

                if (value.getType() == PropertyType::Boolean)
                {
                    bool flag = value.get<bool>();
                    if (studioCheckbox(frame, frame.ids().make("value"), parts.control, {}, flag)
                            .changed)
                    {
                        edited = PropertyValue{flag};
                    }
                }
                else if (value.getType() == PropertyType::Enum && !property.enumOptions.empty())
                {
                    // Chosen, not typed. An enumeration is a closed set the descriptor already
                    // names, and a text field over one is a field where every typo is a scene the
                    // loader will refuse to open.
                    const std::string current = value.get<PropertyValue::EnumValue>().name;
                    int selected = -1;
                    for (std::size_t i = 0; i < property.enumOptions.size(); ++i)
                    {
                        if (property.enumOptions[i] == current) { selected = static_cast<int>(i); }
                    }

                    StudioDropdownOptions options;
                    // A value the descriptor does not declare is shown rather than blanked: it is
                    // a scene written by an older plugin, and hiding it would make the field look
                    // empty when it is merely unrecognised.
                    options.placeholder = current.empty() ? "(none)" : current;
                    if (studioDropdown(frame, frame.ids().make("value"), parts.control,
                                       property.enumOptions, selected, options)
                            .changed
                        && selected >= 0)
                    {
                        edited = PropertyValue{PropertyValue::EnumValue{
                            property.enumOptions[static_cast<std::size_t>(selected)]}};
                    }
                }
                else if (value.getType() == PropertyType::String
                         || value.getType() == PropertyType::Enum)
                {
                    // An enumeration whose descriptor declares no options falls back to typing:
                    // a drop-down over nothing is a control that cannot be used at all.
                    const bool isEnum = value.getType() == PropertyType::Enum;
                    std::string text = isEnum ? value.get<PropertyValue::EnumValue>().name
                                              : value.get<std::string>();
                    if (studioTextField(frame, frame.ids().make("value"), parts.control, text)
                            .committed)
                    {
                        edited = isEnum ? PropertyValue{PropertyValue::EnumValue{text}}
                                        : PropertyValue{text};
                    }
                }
                else if (value.getType() == PropertyType::Float)
                {
                    std::string text = formatFloat(value.get<float>());
                    StudioTextFieldOptions options;
                    options.font = StudioFontRole::Monospace;
                    options.selectAllOnFocus = true;
                    if (studioTextField(frame, frame.ids().make("value"), parts.control, text,
                                        options)
                            .committed)
                    {
                        float parsed = 0.0f;
                        if (parseFloat(text, parsed)) { edited = PropertyValue{parsed}; }
                    }
                }
                else if (value.getType() == PropertyType::Integer)
                {
                    std::string text = std::to_string(value.get<std::int64_t>());
                    StudioTextFieldOptions options;
                    options.font = StudioFontRole::Monospace;
                    options.selectAllOnFocus = true;
                    if (studioTextField(frame, frame.ids().make("value"), parts.control, text,
                                        options)
                            .committed)
                    {
                        std::int64_t parsed = 0;
                        if (parseInteger(text, parsed)) { edited = PropertyValue{parsed}; }
                    }
                }
                else if (value.getType() == PropertyType::Vector2
                         || value.getType() == PropertyType::Vector3
                         || value.getType() == PropertyType::Vector4)
                {
                    static const char* const kAxes[] = {"x", "y", "z", "w"};
                    float components[4] = {};
                    int count = 2;

                    if (value.getType() == PropertyType::Vector2)
                    {
                        const StudioVector2 vector = value.get<StudioVector2>();
                        components[0] = vector.x; components[1] = vector.y;
                    }
                    else if (value.getType() == PropertyType::Vector3)
                    {
                        const StudioVector3 vector = value.get<StudioVector3>();
                        components[0] = vector.x; components[1] = vector.y; components[2] = vector.z;
                        count = 3;
                    }
                    else
                    {
                        const StudioVector4 vector = value.get<StudioVector4>();
                        components[0] = vector.x; components[1] = vector.y;
                        components[2] = vector.z; components[3] = vector.w;
                        count = 4;
                    }

                    if (numericComponents(frame, parts.control, kAxes, components, count))
                    {
                        if (count == 2)
                        {
                            edited = PropertyValue{StudioVector2{components[0], components[1]}};
                        }
                        else if (count == 3)
                        {
                            edited = PropertyValue{
                                StudioVector3{components[0], components[1], components[2]}};
                        }
                        else
                        {
                            edited = PropertyValue{StudioVector4{components[0], components[1],
                                                                 components[2], components[3]}};
                        }
                    }
                }
                else if (value.getType() == PropertyType::Quaternion)
                {
                    // Edited as Euler angles in degrees, not as x/y/z/w. A quaternion's components
                    // are not numbers a person can reason about: nobody knows what to type into w
                    // to turn something thirty degrees, and typing four independent numbers is how
                    // you produce a rotation that is not a rotation at all.
                    static const char* const kAngles[] = {"pitch", "yaw", "roll"};
                    const StudioVector3 euler = eulerDegreesOf(value.get<StudioQuaternion>());
                    float components[3] = {euler.x, euler.y, euler.z};

                    if (numericComponents(frame, parts.control, kAngles, components, 3))
                    {
                        edited = PropertyValue{quaternionFromEulerDegrees(
                            StudioVector3{components[0], components[1], components[2]})};
                    }
                }
                else if (value.getType() == PropertyType::Rectangle)
                {
                    static const char* const kEdges[] = {"x", "y", "w", "h"};
                    const StudioRectangle rectangle = value.get<StudioRectangle>();
                    float components[4] = {
                        static_cast<float>(rectangle.x), static_cast<float>(rectangle.y),
                        static_cast<float>(rectangle.width), static_cast<float>(rectangle.height)};

                    if (numericComponents(frame, parts.control, kEdges, components, 4,
                                          /*integral=*/true))
                    {
                        edited = PropertyValue{StudioRectangle{
                            static_cast<int>(components[0]), static_cast<int>(components[1]),
                            static_cast<int>(components[2]), static_cast<int>(components[3])}};
                    }
                }
                else if (value.getType() == PropertyType::Color)
                {
                    // A swatch and four channels. Not a colour *picker* — that is its own control
                    // and its own task — but a swatch is what makes a row of four numbers legible
                    // as a colour at all, and 0..255 is the range the value is stored in rather
                    // than a normalised one the user would have to convert to.
                    const StudioColor colour = value.get<StudioColor>();
                    UiRect control = parts.control;
                    const UiRect swatch = control.splitLeft(
                        std::min(metricOf(theme, StudioMetric::ControlHeight), control.width));
                    control.splitLeft(std::min(spacing, control.width));

                    if (frame.isDrawPass())
                    {
                        frame.drawList().fillRect(swatch.inset(UiEdges{0.0f, 2.0f}), colour);
                        frame.drawList().strokeRect(swatch.inset(UiEdges{0.0f, 2.0f}),
                                                    theme.color(StudioColorRole::Border),
                                                    metricOf(theme, StudioMetric::BorderWidth));
                    }

                    static const char* const kChannels[] = {"r", "g", "b", "a"};
                    float components[4] = {
                        static_cast<float>(colour.r), static_cast<float>(colour.g),
                        static_cast<float>(colour.b), static_cast<float>(colour.a)};

                    if (numericComponents(frame, control, kChannels, components, 4,
                                          /*integral=*/true))
                    {
                        edited = PropertyValue{StudioColor{
                            toChannel(components[0]), toChannel(components[1]),
                            toChannel(components[2]), toChannel(components[3])}};
                    }
                }
                else if (value.getType() == PropertyType::AssetReference
                         || value.getType() == PropertyType::EntityReference)
                {
                    // A picker over what exists, not a field for typing a UUID. Nobody types a
                    // UUID, and a reference to something that is not there is exactly the state
                    // the Problems panel exists to report.
                    const bool isAsset = value.getType() == PropertyType::AssetReference;
                    const Uuid current = isAsset
                        ? value.get<PropertyValue::AssetReference>().id
                        : value.get<PropertyValue::EntityReference>().id;

                    std::vector<std::string> labels;
                    std::vector<Uuid> ids;
                    // "(none)" first, because clearing a reference is an ordinary thing to want
                    // and a picker with no way to do it forces a user to edit the file by hand.
                    labels.emplace_back("(none)");
                    ids.emplace_back();

                    if (isAsset)
                    {
                        for (const AssetRecord* record : context.getAssets().getAll())
                        {
                            if (record == nullptr) { continue; }
                            labels.push_back(record->sourcePath);
                            ids.push_back(record->id);
                        }
                    }
                    else
                    {
                        for (const StudioEntity& candidate : context.getScene().getEntities())
                        {
                            // An entity cannot refer to itself: the only thing that can come of
                            // offering it is a cycle nothing downstream expects.
                            if (candidate.getId() == entityId) { continue; }
                            labels.push_back(candidate.getName().empty()
                                                 ? std::string{"(unnamed)"}
                                                 : candidate.getName());
                            ids.push_back(candidate.getId());
                        }
                    }

                    int selected = 0;
                    for (std::size_t i = 0; i < ids.size(); ++i)
                    {
                        if (ids[i] == current) { selected = static_cast<int>(i); }
                    }

                    StudioDropdownOptions options;
                    // A reference to something that has gone still shows its id rather than
                    // silently reading as "(none)", which would look like the value was cleared.
                    options.placeholder = current.isValid() ? summarise(value) : "(none)";
                    if (studioDropdown(frame, frame.ids().make("value"), parts.control, labels,
                                       selected, options)
                            .changed
                        && selected >= 0)
                    {
                        const Uuid chosen = ids[static_cast<std::size_t>(selected)];
                        edited = isAsset
                            ? PropertyValue{PropertyValue::AssetReference{chosen}}
                            : PropertyValue{PropertyValue::EntityReference{chosen}};
                    }
                }
                else
                {
                    // Counted in both passes, because it is a property of the value rather than of
                    // drawing -- and a caller reading the result from the input pass is exactly
                    // who wants to know that a kind fell through.
                    ++result.readOnlyProperties;
                    if (frame.isDrawPass())
                    {
                        studioDrawText(frame, parts.control,
                                       studioTruncateText(frame,
                                                          theme.font(StudioFontRole::BodySmall),
                                                          summarise(value), parts.control.width),
                                       StudioFontRole::BodySmall,
                                       theme.color(StudioColorRole::TextDisabled));
                    }
                }

                frame.ids().pop();
                frame.ids().pop();

                if (edited.has_value())
                {
                    // Through the history, always. Showing a scene wrong is a bad afternoon and
                    // editing one wrong is a lost afternoon's work, so nothing here touches an
                    // entity directly.
                    context.execute(std::make_unique<SetPropertyCommand>(
                        context.getScene(), entityId, component.getTypeId(), property.name,
                        *edited));
                    result.edited = true;
                    result.editedProperty = component.getTypeId() + "." + property.name;

                    // The component list may have been rebuilt underneath this loop.
                    break;
                }
            }
        }

        studioEndScroll(frame);
        return result;
    }
}
