// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDetailsPanel.cpp
 * @brief The Details panel.
 */

#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"

#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
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
                // No command yet: SceneCommands has no set-enabled, and inventing one here would
                // put an undoable operation somewhere nothing else can find it. Applied directly
                // and recorded as STUDIO-07019 rather than left looking undoable and not being.
                entity->setEnabled(enabled);
                result.edited = true;
                result.editedProperty = "enabled";
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
                else if (value.getType() == PropertyType::String
                         || value.getType() == PropertyType::Enum)
                {
                    const bool isEnum = value.getType() == PropertyType::Enum;
                    std::string text = isEnum ? value.get<PropertyValue::EnumValue>().name
                                              : value.get<std::string>();
                    if (studioTextField(frame, frame.ids().make("value"), parts.control, text)
                            .committed)
                    {
                        // An enumeration typed rather than chosen, until there is a dropdown
                        // (STUDIO-07018). Validated against the declared options, because an
                        // unknown name written into the document is a scene the loader will refuse.
                        if (!isEnum) { edited = PropertyValue{text}; }
                        else if (property.enumOptions.empty()
                                 || std::find(property.enumOptions.begin(),
                                              property.enumOptions.end(), text)
                                        != property.enumOptions.end())
                        {
                            edited = PropertyValue{PropertyValue::EnumValue{text}};
                        }
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
                         || value.getType() == PropertyType::Vector3)
                {
                    const bool three = value.getType() == PropertyType::Vector3;
                    float components[3] = {};
                    if (three)
                    {
                        const StudioVector3 vector = value.get<StudioVector3>();
                        components[0] = vector.x; components[1] = vector.y; components[2] = vector.z;
                    }
                    else
                    {
                        const StudioVector2 vector = value.get<StudioVector2>();
                        components[0] = vector.x; components[1] = vector.y;
                    }

                    const int count = three ? 3 : 2;
                    UiRect fields = parts.control;
                    const float fieldWidth =
                        (fields.width - spacing * static_cast<float>(count - 1))
                        / static_cast<float>(count);

                    bool changed = false;
                    for (int i = 0; i < count; ++i)
                    {
                        const UiRect box = fields.splitLeft(std::min(fieldWidth, fields.width));
                        if (i + 1 < count) { fields.splitLeft(std::min(spacing, fields.width)); }

                        std::string text = formatFloat(components[i]);
                        StudioTextFieldOptions options;
                        options.font = StudioFontRole::Monospace;
                        options.selectAllOnFocus = true;

                        const char* axis = i == 0 ? "x" : (i == 1 ? "y" : "z");
                        if (studioTextField(frame, frame.ids().make(axis), box, text, options)
                                .committed)
                        {
                            float parsed = 0.0f;
                            if (parseFloat(text, parsed)) { components[i] = parsed; changed = true; }
                        }
                    }

                    if (changed)
                    {
                        edited = three
                            ? PropertyValue{StudioVector3{components[0], components[1], components[2]}}
                            : PropertyValue{StudioVector2{components[0], components[1]}};
                    }
                }
                else if (frame.isDrawPass())
                {
                    studioDrawText(frame, parts.control,
                                   studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                      summarise(value), parts.control.width),
                                   StudioFontRole::BodySmall,
                                   theme.color(StudioColorRole::TextDisabled));
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
