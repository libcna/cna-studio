// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDetailsPanel.cpp
 * @brief The Details panel.
 */

#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/Assets/AssetCommands.hpp"

#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/MaterialDocument.hpp"
#include "CNA/Studio/Core/NumberText.hpp"
#include "CNA/Studio/PrefabWorkflow.hpp"
#include "CNA/Studio/Scene/PrefabCommands.hpp"
#include "CNA/Studio/Scene/PrefabDocument.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"
#include "CNA/Studio/ProjectCommands.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"
// The audio seam. A header with no CNA in it -- the implementation that links CNA lives in the one
// module that may, and this panel only ever sees the interface.
#include "CNA/Studio/Viewport/StudioAudio.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
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

        /**
         * @brief Formats a float the way a person would type it back.
         *
         * Not `%f`: a position of 3 should read "3", not "3.000000", and a scale of 0.5 should not
         * read "0.500000" in a field somebody is about to edit.
         *
         * And not `%.9g`, which is what this was and which is the same mistake one step further
         * along (STUDIO-35037). Nine significant digits round-trip every float, and nine
         * significant digits of a float are nine digits of its *binary representation*: a Volume
         * set to 0.6 read as `0.600000024`. `studioFormatFloat` finds the shortest text that reads
         * back as the same float, which is `0.6`.
         */
        std::string formatFloat(float value) { return studioFormatFloat(value); }

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
         * @param labelled False to leave the component letters off. For a colour, where the swatch
         *        beside the row already says which channel is which far better than a letter can,
         *        and where four letters cost exactly the width `255` needs.
         * @return True when any box committed a new value.
         */
        /**
         * @brief The axis colour for a component's letter.
         *
         * By the letter rather than by the index, because the same helper draws a Vector3's
         * X/Y/Z, a rectangle's X/Y/W/H and a colour's R/G/B/A, and only the first of those is an
         * axis. A rectangle's W is not the Z axis and colouring it blue would say it was.
         */
        StudioColorRole axisRoleFor(std::string_view name)
        {
            if (name == "x" || name == "pitch") { return StudioColorRole::AxisX; }
            if (name == "y" || name == "yaw")   { return StudioColorRole::AxisY; }
            if (name == "z" || name == "roll")  { return StudioColorRole::AxisZ; }
            if (name == "w") { return StudioColorRole::AxisW; }
            // r/g/b/a fall through deliberately. They are already beside a colour swatch that says
            // which is which far better than a letter can, and three coloured letters next to a
            // colour the user is choosing would be three more colours competing with it.
            return StudioColorRole::TextSecondary;
        }

        /**
         * @brief The component letter as a user should see it.
         *
         * Upper case, which is what every other tool shows and what reads at a glance in a field
         * three characters wide -- while the names themselves stay lower case, because they are
         * also the widget ids and the retained state behind every one of these fields is keyed on
         * them. Renaming an id to change a letter's case is how a field forgets what was typed
         * into it.
         */
        std::string_view axisLabelFor(std::string_view name)
        {
            // pitch/yaw/roll come out as X/Y/Z, which is the correct mapping -- the three are
            // built from euler.x, euler.y and euler.z in that order -- and it is what makes the
            // Transform block read as one grid. Position, Rotation and Scale with three different
            // vocabularies down the same three columns is three rows the eye has to align by hand.
            // The property's own label still says Rotation, so nothing is lost.
            static constexpr std::string_view kUpper[] = {"X", "Y", "Z", "W", "H",
                                                          "X", "Y", "Z"};
            static constexpr std::string_view kLower[] = {"x", "y", "z", "w", "h",
                                                          "pitch", "yaw", "roll"};
            for (std::size_t i = 0; i < std::size(kLower); ++i)
            {
                if (name == kLower[i]) { return kUpper[i]; }
            }
            return name;
        }

        /**
         * @brief A row of numeric fields, each typed into or dragged sideways to scrub.
         *
         * `STUDIO-07055`. Returns whether any value changed, and sets @p outDragging while a scrub
         * is in flight -- which is what tells the caller to push its change as
         * `MergePolicy::MergeWithPrevious` so the whole gesture is one undo entry rather than one
         * per pixel.
         */
        bool numericComponents(StudioFrame& frame, const UiRect& bounds, const char* const* names,
                               float* values, int count, bool integral = false,
                               bool labelled = true, bool* outDragging = nullptr,
                               float step = 0.0f)
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

                StudioNumericFieldOptions options;
                options.integral = integral;
                // An integer scrubs one per pixel and a float a hundredth, unless the caller knows
                // better. Not a fraction of a range, because a position has no range -- and a step
                // proportional to the current value would make a field at zero unmovable, which is
                // exactly where a user most often starts.
                options.step = step > 0.0f ? step : (integral ? 1.0f : 0.01f);
                options.text.font = StudioFontRole::Monospace;
                options.text.selectAllOnFocus = true;
                // The component's own letter, inside the field, always. It was on `placeholder`,
                // which shows only while a field is *empty* -- so every populated Position,
                // Rotation and Scale in Studio was three unlabelled boxes, which is precisely the
                // case the letters exist for.
                if (labelled)
                {
                    options.text.prefix = axisLabelFor(names[i]);
                    options.text.prefixRole = axisRoleFor(names[i]);
                }

                const StudioNumericFieldResult field =
                    studioNumericField(frame, frame.ids().make(names[i]), box, values[i], options);

                if (field.changed) { changed = true; }
                if (field.dragging && outDragging != nullptr) { *outDragging = true; }
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

        /**
         * @brief A value rendered as text, for a property that is shown rather than edited.
         *
         * Distinct from @ref summarise, which answers "this kind has no editor". A property the
         * *importer* declared read-only has a perfectly good value and showing "(not editable
         * yet)" in its place says the editor is missing when the value is simply not the user's to
         * change -- which is how a texture's pixel size came to read as an unimplemented feature.
         *
         * @param value The value.
         * @return Its content, or `summarise`'s answer for a kind with no text form.
         */
        std::string describeValue(const PropertyValue& value)
        {
            switch (value.getType())
            {
                case PropertyType::Boolean:
                    return value.get<bool>() ? "yes" : "no";
                case PropertyType::Integer:
                    return std::to_string(value.get<std::int64_t>());
                case PropertyType::Float:
                    return formatFloat(value.get<float>());
                case PropertyType::String:
                    return value.get<std::string>();
                case PropertyType::Enum:
                    return value.get<PropertyValue::EnumValue>().name;
                case PropertyType::Vector2:
                {
                    const StudioVector2 v = value.get<StudioVector2>();
                    return formatFloat(v.x) + ", " + formatFloat(v.y);
                }
                case PropertyType::Vector3:
                {
                    const StudioVector3 v = value.get<StudioVector3>();
                    return formatFloat(v.x) + ", " + formatFloat(v.y) + ", " + formatFloat(v.z);
                }
                case PropertyType::Vector4:
                {
                    const StudioVector4 v = value.get<StudioVector4>();
                    return formatFloat(v.x) + ", " + formatFloat(v.y) + ", " + formatFloat(v.z)
                         + ", " + formatFloat(v.w);
                }
                case PropertyType::Rectangle:
                {
                    const StudioRectangle r = value.get<StudioRectangle>();
                    return std::to_string(r.x) + ", " + std::to_string(r.y) + ", "
                         + std::to_string(r.width) + " x " + std::to_string(r.height);
                }
                case PropertyType::Color:
                {
                    const StudioColor c = value.get<StudioColor>();
                    return std::to_string(c.r) + ", " + std::to_string(c.g) + ", "
                         + std::to_string(c.b) + ", " + std::to_string(c.a);
                }
                default:
                    break;
            }
            return summarise(value);
        }

        /** @brief The row layout every property shares: a label on the left, a control on the right. */
        /**
         * @brief A swatch and four 0..255 channels, edited in place.
         *
         * Shared by the component editor and the scene settings rather than written twice: two
         * colour controls in one panel that disagreed about the range, or about whether the swatch
         * comes first, would be the sort of difference a user reads as a bug in one of them.
         *
         * Not a colour *picker* -- that is its own control and its own task -- but a swatch is
         * what makes a row of four numbers legible as a colour at all.
         *
         * @param frame The frame.
         * @param bounds Where the control goes.
         * @param key Identity of the row within the current id scope.
         * @param colour Read for the displayed value; written when a channel commits.
         * @return True when a channel committed a new value.
         */
        bool colourField(StudioFrame& frame, const UiRect& bounds, std::string_view key,
                         StudioColor& colour)
        {
            const StudioTheme& theme = frame.theme();
            const float spacing = metricOf(theme, StudioMetric::SpacingSmall);

            UiRect control = bounds;
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

            frame.ids().push(key);
            const bool changed =
                numericComponents(frame, control, kChannels, components, 4, /*integral=*/true,
                                  /*labelled=*/false);
            frame.ids().pop();

            if (!changed) { return false; }

            colour = StudioColor{toChannel(components[0]), toChannel(components[1]),
                                 toChannel(components[2]), toChannel(components[3])};
            return true;
        }

        struct PropertyRow
        {
            UiRect label;
            UiRect control;
        };

        /** @brief The last segment of a project-relative asset path. */
        std::string fileNameOf(const std::string& path)
        {
            const std::size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

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

        /**
         * @brief The sprite preview's box, in rows of the property grid.
         *
         * Expressed in rows rather than pixels so it scales with the theme like everything else,
         * and so the scroll view's content height -- which is counted in rows -- can include it
         * without a second unit.
         */
        constexpr std::size_t kAnimationPreviewRows = 4;

        /** @brief Whether a preview can be offered for an asset of this kind at all. */
        bool isAudibleAsset(AssetType type)
        {
            return type == AssetType::SoundEffect || type == AssetType::Song;
        }

        /** @brief How many rows the prefab section reserves at most. */
        constexpr std::size_t kPrefabSectionRows = 6;

        /** @brief What the prefab section knows, worked out once a frame and kept for both passes. */
        struct PrefabSummary
        {
            /** @brief -2 the asset is gone, -1 it will not load, otherwise the override count. */
            std::int64_t state = -2;
            std::string name;
            std::vector<std::string> lines;
        };

        /** @brief Packs a summary into the one string a `WidgetState` can hold. */
        std::string packPrefabSummary(const PrefabSummary& summary)
        {
            std::string packed = summary.name;
            for (const std::string& line : summary.lines) { packed += "\n" + line; }
            return packed;
        }

        /** @brief Unpacks what @ref packPrefabSummary wrote. */
        PrefabSummary unpackPrefabSummary(std::int64_t state, const std::string& packed)
        {
            PrefabSummary summary;
            summary.state = state;

            std::size_t start = 0;
            bool first = true;
            while (start <= packed.size())
            {
                const std::size_t end = packed.find('\n', start);
                const std::string part =
                    packed.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (first) { summary.name = part; first = false; }
                else { summary.lines.push_back(part); }
                if (end == std::string::npos) { break; }
                start = end + 1;
            }
            return summary;
        }

        /** @brief Describes one override the way the report reads it. */
        std::string describeOverride(const PrefabOverride& entry)
        {
            std::string line = std::string{toString(entry.kind)} + ": " + entry.entityName;
            if (!entry.propertyName.empty()) { line += "." + entry.propertyName; }
            return line;
        }

        /**
         * @brief A linear colour row: a swatch, then its channels as floats.
         *
         * `plan.md` STUDIO-07046. Not the property grid's `Color` editor, which edits a
         * `StudioColor` as four whole numbers from 0 to 255 -- a material's colours are linear
         * floats and an emissive one is routinely greater than one, so 0..255 would both quantise
         * what the user typed and refuse what a bright material needs.
         *
         * The swatch shows the 0..1 range as a colour, clamped. A value above one has no brighter
         * pixel to show, which is a property of a screen rather than of the material, so the
         * numbers beside it stay the truth.
         *
         * @param frame The frame.
         * @param bounds The control column.
         * @param key A stable id for the row.
         * @param colour In, and out when a channel committed.
         * @return True when a channel committed.
         */
        bool linearColorRow(StudioFrame& frame, const UiRect& bounds, const char* key,
                            StudioVector3& colour)
        {
            const StudioTheme& theme = frame.theme();
            const float spacing = metricOf(theme, StudioMetric::SpacingSmall);

            UiRect control = bounds;
            const UiRect swatch = control.splitLeft(
                std::min(metricOf(theme, StudioMetric::ControlHeight), control.width));
            control.splitLeft(std::min(spacing, control.width));

            if (frame.isDrawPass())
            {
                const auto channel = [](float value) {
                    return static_cast<std::uint8_t>(
                        std::clamp(std::lround(value * 255.0f), 0L, 255L));
                };
                frame.drawList().fillRect(
                    swatch.inset(UiEdges{0.0f, 2.0f}),
                    StudioColor{channel(colour.x), channel(colour.y), channel(colour.z), 255});
                frame.drawList().strokeRect(swatch.inset(UiEdges{0.0f, 2.0f}),
                                            theme.color(StudioColorRole::Border),
                                            metricOf(theme, StudioMetric::BorderWidth));
            }

            // Unlabelled, for the reason `axisRoleFor` records: the swatch beside them says which
            // channel is which far better than three letters can.
            static const char* const kChannels[] = {"r", "g", "b"};
            float components[3] = {colour.x, colour.y, colour.z};

            frame.ids().push(key);
            const bool changed = numericComponents(frame, control, kChannels, components, 3,
                                                   /*integral=*/false, /*labelled=*/false);
            frame.ids().pop();

            if (!changed) { return false; }
            colour = StudioVector3{components[0], components[1], components[2]};
            return true;
        }

        /**
         * @brief The prefab section: what this instance has changed, and what to do about it.
         *
         * `plan.md` STUDIO-07042. Answered for the **instance**, not for the entity: selecting a
         * child of an instance should still say what it is part of and let the user act on it, so
         * the section walks up to the instance root first.
         *
         * ### Worked out once a frame, not once a pass
         *
         * The comparison loads the prefab from disk and walks both subtrees. The panel is
         * described twice a frame, and doing that twice would double the cost of the one panel in
         * Studio that opens a file to draw itself. The input pass computes it and packs the
         * summary into the widget state; the draw pass reads what the input pass decided, which
         * also means the rows drawn are exactly the rows the buttons were hit-tested against.
         *
         * It is still a file read per frame while a prefab instance is selected, which is the
         * Content Browser's problem one size down (`STUDIO-30016`).
         *
         * ### Three at most, and a count
         *
         * The list exists to make the divergence *recognisable*, not to enumerate it — the same
         * rule the missing-reference report follows. A hundred overrides is a hundred rows nobody
         * reads and a panel that scrolls for a second.
         *
         * @param frame The frame.
         * @param area The rows the section may use; advanced by the ones it takes.
         * @param theme The theme.
         * @param context The editor. Its scene is compared and mutated; its history receives it.
         * @param entityId The selected entity, anywhere inside the instance.
         * @return What it reported and did.
         */
        StudioPrefabSectionResult studioPrefabSection(StudioFrame& frame, UiRect& area,
                                                      const StudioTheme& theme,
                                                      StudioContext& context, const Uuid& entityId)
        {
            StudioPrefabSectionResult result;

            const Uuid instanceRoot = findInstanceRoot(context.getScene(), entityId);
            if (!instanceRoot.isValid()) { return result; }
            result.present = true;

            const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                             metricOf(theme, StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

            const auto nextRow = [&]() {
                const UiRect row = area.splitTop(rowHeight);
                area.splitTop(spacing);
                return row;
            };

            const auto say = [&](const UiRect& box, const std::string& text, StudioColorRole role,
                                 StudioFontRole font = StudioFontRole::Body) {
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, box,
                                   studioTruncateText(frame, theme.font(font), text, box.width),
                                   font, theme.color(role));
                }
            };

            WidgetState& state = frame.state().get(frame.ids().make("prefabsummary"));

            const Uuid assetId = getPrefabAssetOf(context.getScene(), instanceRoot);
            const AssetRecord* record = context.getAssets().find(assetId);

            // Kept across the passes so the commands below have it without a second load.
            PrefabDocument prefab;
            bool prefabLoaded = false;

            if (frame.isInputPass())
            {
                PrefabSummary summary;
                if (record == nullptr)
                {
                    // The link survives the asset going away, so it is reported rather than
                    // vanishing: an instance whose prefab was deleted is exactly what a user needs
                    // told.
                    summary.state = -2;
                    summary.name = assetId.toString();
                }
                else if (!prefab.loadFromFile(context.getAssets().resolvePath(record->sourcePath),
                                              context.getComponentRegistry())
                              .succeeded)
                {
                    summary.state = -1;
                    summary.name = record->sourcePath;
                }
                else
                {
                    prefabLoaded = true;
                    const std::vector<PrefabOverride> overrides = findPrefabOverrides(
                        context.getScene(), instanceRoot, prefab, context.getComponentRegistry());

                    summary.state = static_cast<std::int64_t>(overrides.size());
                    summary.name = prefab.getName();
                    for (std::size_t index = 0; index < overrides.size() && index < 3; ++index)
                    {
                        summary.lines.push_back(describeOverride(overrides[index]));
                    }
                }

                state.integer = summary.state;
                state.text = packPrefabSummary(summary);
            }

            const PrefabSummary summary = unpackPrefabSummary(state.integer, state.text);
            result.overrides = summary.state > 0 ? static_cast<std::size_t>(summary.state) : 0;

            {
                const PropertyRow parts = splitRow(theme, nextRow());
                say(parts.label, "Prefab", StudioColorRole::TextSecondary);
                if (summary.state == -2)
                {
                    say(parts.control, "missing (" + summary.name + ")", StudioColorRole::Warning);
                }
                else if (summary.state == -1)
                {
                    say(parts.control, summary.name + " will not load", StudioColorRole::Warning);
                }
                else
                {
                    say(parts.control, summary.name, StudioColorRole::TextPrimary);
                }
            }

            if (summary.state < 0) { return result; }

            {
                const PropertyRow parts = splitRow(theme, nextRow());
                say(parts.label, "Changes", StudioColorRole::TextSecondary);
                say(parts.control,
                    result.overrides == 0
                        ? std::string{"None. This instance is the prefab."}
                        : std::to_string(result.overrides)
                              + (result.overrides == 1 ? " change" : " changes"),
                    result.overrides == 0 ? StudioColorRole::TextSecondary
                                          : StudioColorRole::TextPrimary);
            }

            for (const std::string& line : summary.lines)
            {
                UiRect row = nextRow();
                row.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingLarge), row.width));
                say(row, line, StudioColorRole::TextSecondary, StudioFontRole::BodySmall);
            }
            if (result.overrides > summary.lines.size())
            {
                UiRect row = nextRow();
                row.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingLarge), row.width));
                say(row, "and " + std::to_string(result.overrides - summary.lines.size()) + " more",
                    StudioColorRole::TextDisabled, StudioFontRole::BodySmall);
            }

            if (result.overrides == 0) { return result; }

            const PropertyRow parts = splitRow(theme, nextRow());
            UiRect controls = parts.control;
            const float buttonWidth =
                std::max(metricOf(theme, StudioMetric::ControlHeight) * 2.5f, 64.0f);
            const UiRect revertBox = controls.splitLeft(std::min(buttonWidth, controls.width));
            controls.splitLeft(std::min(spacing, controls.width));
            const UiRect applyBox = controls.splitLeft(std::min(buttonWidth, controls.width));

            frame.ids().push("prefab");

            StudioButtonOptions revert;
            revert.icon = StudioIcon::Undo;
            revert.tooltip = "Throw away this instance's changes and take the prefab as it is";
            const bool revertPressed =
                studioButton(frame, frame.ids().make("revert"), revertBox, "Revert", revert)
                    .activated;

            StudioButtonOptions apply;
            apply.icon = StudioIcon::Save;
            apply.tooltip = "Write this instance's changes back into the prefab file";
            const bool applyPressed =
                studioButton(frame, frame.ids().make("apply"), applyBox, "Apply", apply).activated;

            frame.ids().pop();

            // Only the input pass can have pressed anything, and only the input pass loaded the
            // prefab the revert needs.
            if (revertPressed && prefabLoaded)
            {
                auto command = std::make_unique<RevertPrefabInstanceCommand>(
                    context.getScene(), instanceRoot, prefab);
                if (command->isValid())
                {
                    result.message = command->getDescription();
                    context.execute(std::move(command));
                    context.pruneSelection();
                    result.reverted = true;
                }
                else
                {
                    result.message = "There is nothing to revert.";
                    result.failed = true;
                }
            }
            else if (applyPressed)
            {
                auto command = std::make_unique<ApplyPrefabInstanceCommand>(
                    context.getScene(), context.getAssets(), context.getComponentRegistry(),
                    instanceRoot);
                if (!command->isValid())
                {
                    result.message = "Cannot apply: " + command->getError();
                    result.failed = true;
                }
                else
                {
                    const std::string summaryText = command->getDescription();
                    const ApplyPrefabInstanceCommand* raw = command.get();
                    context.execute(std::move(command));

                    // Apply writes a file, and a write that failed has to be said: every other
                    // instance of this prefab is about to be compared against what is on disk.
                    if (raw->getError().empty())
                    {
                        result.message = summaryText;
                        result.applied = true;
                    }
                    else
                    {
                        result.message = "Cannot write the prefab: " + raw->getError();
                        result.failed = true;
                    }
                }
            }

            return result;
        }

        /** @brief What one sprite animation preview showed this frame. */
        struct StudioAnimationPreviewResult
        {
            AnimationPreview preview;
            std::size_t frames = 0;
        };

        /**
         * @brief The sprite animation preview: transport, frame readout and the frame itself.
         *
         * `plan.md` STUDIO-07043. The prototype's version, with three differences that are all the
         * same difference -- the native panel is a function called twice a frame rather than an
         * object that owns its panel.
         *
         * **The playback lives in the widget state store**, keyed by the component's identity, so
         * two animated entities each keep their own position and a panel that is not looked at for
         * ten minutes has its state reclaimed like any other widget's. It is *not* in the document:
         * a scene that recorded the frame an artist happened to be paused on would carry it into
         * every save and every diff (`ANALYSIS.md` decision D-07), and the whole point of this task
         * is a preview that puts nothing into the document.
         *
         * **Time advances on the input pass only.** The panel is described twice per frame and a
         * clip advanced on both would run at double speed -- and, worse, the draw pass would show
         * a different frame from the one the input pass decided, so a click on Next would step
         * from a frame nobody saw.
         *
         * @param frame The frame.
         * @param area The preview's whole block: one row of transport and @ref
         *        kAnimationPreviewRows of picture.
         * @param theme The theme the rows are measured from.
         * @param services The thumbnail seam, which may be absent.
         * @param assets Where the sheet's record and its recorded pixel size come from.
         * @param clip The clip, read from the component.
         * @param sheetId The sheet asset.
         * @param entityId Whose preview this is, for the snapshot the viewport reads.
         * @return The snapshot, and how many frames the clip has.
         */
        StudioAnimationPreviewResult studioAnimationPreview(
            StudioFrame& frame, const UiRect& area, const StudioTheme& theme,
            const StudioDetailsServices& services, const AssetDatabase& assets,
            const SpriteAnimationClip& clip, const Uuid& sheetId, const Uuid& entityId)
        {
            StudioAnimationPreviewResult result;
            result.frames = clip.getFrameCount();

            const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                             metricOf(theme, StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

            UiRect remaining = area;
            const PropertyRow parts = splitRow(theme, remaining.splitTop(rowHeight));
            remaining.splitTop(spacing);

            if (frame.isDrawPass())
            {
                studioDrawText(frame, parts.label, "Preview", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            frame.ids().push("animpreview");
            WidgetState& state = frame.state().get(frame.ids().make("playback"));

            // Round-tripped through the value the clip's own helpers take, so the clamping,
            // wrapping and loop rules are the prototype's tested ones rather than a second set
            // written here.
            AnimationPlayback playback;
            playback.position = state.integer < 0 ? 0u : static_cast<std::size_t>(state.integer);
            playback.elapsed = state.scalar;
            playback.playing = state.checked;
            playback.clampTo(clip);

            if (frame.isInputPass())
            {
                // The frame list is editable while the preview runs, and shortening it can leave
                // the position past the end -- which clampTo above has already dealt with.
                playback.advance(clip, frame.input().deltaSeconds);
            }

            const float buttonWidth = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                               metricOf(theme, StudioMetric::MinimumHitTarget));

            UiRect controls = parts.control;
            const UiRect playBox = controls.splitLeft(std::min(buttonWidth, controls.width));
            controls.splitLeft(std::min(spacing, controls.width));
            const UiRect backBox = controls.splitLeft(std::min(buttonWidth, controls.width));
            controls.splitLeft(std::min(spacing, controls.width));
            const UiRect forwardBox = controls.splitLeft(std::min(buttonWidth, controls.width));
            controls.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingSmall), controls.width));

            const bool playable = !clip.isEmpty();

            StudioButtonOptions play;
            play.icon = playback.playing ? StudioIcon::Pause : StudioIcon::Play;
            play.iconOnly = true;
            play.enabled = playable;
            play.tooltip = playable ? (playback.playing ? std::string_view{"Pause the preview"}
                                                        : std::string_view{"Play the clip"})
                                    : std::string_view{"This clip has no frames to play"};
            if (studioButton(frame, frame.ids().make("play"),
                             playBox, playback.playing ? "Pause" : "Play", play).activated)
            {
                playback.playing = !playback.playing;
            }

            StudioButtonOptions step;
            step.iconOnly = true;
            step.enabled = playable;

            step.icon = StudioIcon::ChevronLeft;
            step.tooltip = "Previous frame";
            if (studioButton(frame, frame.ids().make("previous"), backBox, "Previous frame", step)
                    .activated)
            {
                playback.step(clip, -1);
            }

            step.icon = StudioIcon::ChevronRight;
            step.tooltip = "Next frame";
            if (studioButton(frame, frame.ids().make("next"), forwardBox, "Next frame", step)
                    .activated)
            {
                playback.step(clip, 1);
            }

            state.integer = static_cast<std::int64_t>(playback.position);
            state.scalar = playback.elapsed;
            state.checked = playback.playing;

            if (frame.isDrawPass() && controls.width > 0.0f)
            {
                // The current frame and the clip's length. The frame's own hold is added only when
                // the frames differ: repeating one number for every frame of a uniform clip is
                // noise.
                std::string heading =
                    clip.frames.empty()
                        ? std::string{"No frames yet."}
                        : std::to_string(playback.position + 1) + " / "
                              + std::to_string(clip.frames.size()) + "  "
                              + std::to_string(static_cast<int>(clip.getDuration() * 1000.0f))
                              + " ms";
                if (clip.hasFrameDurations() && !clip.frames.empty())
                {
                    heading += "  (this "
                             + std::to_string(static_cast<int>(
                                   clip.getFrameDuration(playback.position) * 1000.0f))
                             + " ms)";
                }
                studioDrawText(frame, controls,
                               studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                  heading, controls.width),
                               StudioFontRole::BodySmall,
                               theme.color(clip.frames.empty() ? StudioColorRole::TextDisabled
                                                               : StudioColorRole::TextSecondary));
            }

            // --- The picture ------------------------------------------------------------------
            const PropertyRow pictureRow = splitRow(theme, remaining);
            const float side = std::min(pictureRow.control.width, pictureRow.control.height);
            UiRect box = pictureRow.control;
            box.width = side;
            box.height = side;

            const AssetRecord* sheet = sheetId.isValid() ? assets.find(sheetId) : nullptr;
            const StudioVector2 sheetSize =
                sheet != nullptr
                    ? PropertyValue::fromJson(sheet->importerSettings["pixelSize"],
                                              PropertyType::Vector2).get<StudioVector2>()
                    : StudioVector2{};
            const UiTextureId texture =
                (sheet != nullptr && services.thumbnail) ? services.thumbnail(sheetId)
                                                         : kUiTextureNone;
            const StudioRectangle source = clip.getFrameRectangle(playback.position);

            if (frame.isDrawPass() && side > 0.0f)
            {
                frame.drawList().fillRect(box, theme.color(StudioColorRole::ControlBackground));

                if (texture != kUiTextureNone && !source.isEmpty() && sheetSize.x > 0.0f
                    && sheetSize.y > 0.0f)
                {
                    frame.drawList().drawImageRegion(
                        box, texture,
                        UiRect{static_cast<float>(source.x), static_cast<float>(source.y),
                               static_cast<float>(source.width), static_cast<float>(source.height)},
                        sheetSize.x, sheetSize.y);
                }
                else
                {
                    // Everything the frame is except its pixels. A build with no device cannot
                    // show the picture and can still say exactly which texels it names, which is
                    // what makes a headless capture of this panel worth looking at -- and what
                    // tells a user with a device that the *sheet* is the problem rather than the
                    // clip.
                    const std::string said =
                        sheet == nullptr
                            ? std::string{"Assign a sheet."}
                            : (source.isEmpty()
                                   ? std::string{"No frame."}
                                   : (sheetSize.x <= 0.0f
                                          ? std::string{"The sheet's size is not recorded."}
                                          : std::to_string(source.width) + "x"
                                                + std::to_string(source.height) + " at "
                                                + std::to_string(source.x) + ","
                                                + std::to_string(source.y)));
                    studioDrawText(frame, box.inset(UiEdges{metricOf(theme, StudioMetric::SpacingXSmall)}),
                                   studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                      said, box.width),
                                   StudioFontRole::BodySmall,
                                   theme.color(StudioColorRole::TextDisabled));
                }
                frame.drawList().strokeRect(box, theme.color(StudioColorRole::Border));
            }

            frame.ids().pop();

            // Published so the viewport draws the frame this preview is showing. Only ever the
            // snapshot: the playback stays here.
            if (!clip.frames.empty())
            {
                result.preview = AnimationPreview{entityId, playback.position};
            }
            return result;
        }

        /**
         * @brief One audio preview control: Play, Stop, and what would be heard.
         *
         * `plan.md` STUDIO-07044. Drawn the same way wherever it appears -- under an audio source's
         * properties with that source's own volume, pan and pitch, and on a sound asset with
         * neutral ones -- because two previews with different controls on them is how the one that
         * is looked at less often quietly becomes the wrong one.
         *
         * Every refusal is *said*. A control that is missing, or present and inert, is one the user
         * cannot tell from a feature that was never written: no audio in this build, no clip
         * assigned and a clip whose file has gone are three different problems with three different
         * answers, and only the last of them is the device's fault.
         *
         * @param frame The frame.
         * @param row The whole row, label column included.
         * @param theme The theme the row is measured from.
         * @param services The audio seam, which may be absent.
         * @param assets Where the clip's record is resolved from.
         * @param clipId The clip to play, which may be unset.
         * @param volume 0..1.
         * @param pitch -1..1 in octaves.
         * @param pan -1..1, left to right.
         * @return What the control did.
         */
        StudioAudioPreviewResult studioAudioPreviewRow(StudioFrame& frame, const UiRect& row,
                                                       const StudioTheme& theme,
                                                       const StudioDetailsServices& services,
                                                       const AssetDatabase& assets,
                                                       const Uuid& clipId, float volume,
                                                       float pitch, float pan)
        {
            StudioAudioPreviewResult result;
            ++result.controls;

            const PropertyRow parts = splitRow(theme, row);
            if (frame.isDrawPass())
            {
                studioDrawText(frame, parts.label, "Preview", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            const AssetRecord* record = clipId.isValid() ? assets.find(clipId) : nullptr;

            // Why this control cannot do anything, when it cannot. Worked out once and used for
            // both the disabled state and the sentence beside it, so the two can never disagree --
            // a greyed button next to text saying it should work is the shape of a bug report.
            std::string refusal;
            std::string refusalDetail;
            if (services.audio == nullptr)
            {
                refusal = "No audio device.";
                refusalDetail = "This build has no audio device, so nothing can be previewed.";
            }
            else if (!clipId.isValid())
            {
                refusal = "No clip assigned.";
                refusalDetail = "Assign a clip to this audio source to hear it.";
            }
            else if (record == nullptr)
            {
                refusal = "Clip missing.";
                refusalDetail = "The clip this refers to is not in the project's assets.";
            }
            else if (!isAudibleAsset(record->type))
            {
                // A texture in a clip slot. The property editor's asset picker filters by the
                // declared type, but a scene authored elsewhere can hold anything.
                refusal = "Not a sound.";
                refusalDetail = fileNameOf(record->sourcePath) + " is not a sound.";
            }

            // Icon-only, and measured rather than assumed. Two buttons carrying the words "Play"
            // and "Stop" leave a property panel's control column about eighty pixels for the
            // sentence beside them, which is where "This build has no audio device." became
            // "This ..." -- an explanation nobody can read is an explanation nobody has. The
            // label still decides the identity, the tooltip and what a screen reader says.
            const float buttonWidth = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                               metricOf(theme, StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

            UiRect controls = parts.control;
            const UiRect playBox = controls.splitLeft(std::min(buttonWidth, controls.width));
            controls.splitLeft(std::min(spacing, controls.width));
            const UiRect stopBox = controls.splitLeft(std::min(buttonWidth, controls.width));
            controls.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingSmall), controls.width));

            frame.ids().push("audiopreview");

            StudioButtonOptions play;
            play.icon = StudioIcon::Play;
            play.iconOnly = true;
            play.enabled = refusal.empty();
            play.tooltip = refusal.empty()
                ? std::string_view{"Hear this clip as it is configured here"}
                : std::string_view{refusalDetail};
            if (studioButton(frame, frame.ids().make("play"), playBox, "Play", play).activated)
            {
                result.played = true;
                result.clip = record != nullptr ? record->sourcePath : std::string{};
                // Reported rather than swallowed: a clip that will not load and a clip of silence
                // sound identical, and only one of them is the user's problem to fix.
                result.started = services.audio->play(clipId, volume, pitch, pan);
            }

            StudioButtonOptions stop;
            stop.icon = StudioIcon::Stop;
            stop.iconOnly = true;
            // Asked of the device, not of a remembered flag, so a clip that simply reached its end
            // stops offering a Stop that could not do anything.
            stop.enabled = services.audio != nullptr && services.audio->isPlaying();
            stop.tooltip = "Stop the preview";
            if (studioButton(frame, frame.ids().make("stop"), stopBox, "Stop", stop).activated)
            {
                result.stopped = true;
                services.audio->stop();
            }

            frame.ids().pop();

            if (frame.isDrawPass() && controls.width > 0.0f)
            {
                // What will be heard, or why nothing will be. The clip's file name rather than its
                // path: a preview button beside a name answers "which sound is this" without the
                // user having to go back to the property above it.
                const std::string text =
                    refusal.empty() ? fileNameOf(record->sourcePath) : refusal;
                studioDrawText(frame, controls,
                               studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                  text, controls.width),
                               StudioFontRole::BodySmall,
                               theme.color(refusal.empty() ? StudioColorRole::TextSecondary
                                                           : StudioColorRole::TextDisabled));
            }

            return result;
        }
    }

namespace
{
    /** @brief What an expanded compound row produced. */
    struct CompoundEditResult
    {
        std::optional<PropertyValue> edited;
        std::size_t rows = 0;
    };

    /**
     * @brief Draws a list or a structure as a summary row that expands into one row per element.
     *
     * `STUDIO-07054`. `studioPropertyEditor` draws *a control in a rect*, which is the right shape
     * for every scalar kind and the wrong shape for these two: a list of four frames needs four
     * rows, and a rect cannot grow. So the caller's row allocator is handed in, and this claims as
     * many rows as it needs.
     *
     * The expansion is retained state keyed on the row's id, so a list stays open across the frames
     * in which somebody edits it -- a section that collapsed after every keystroke would make a
     * four-element list four separate visits.
     *
     * @param frame The frame.
     * @param summary The summary row's control rect: the disclosure, the count and Add.
     * @param nextRow Allocates another full-width row, and is what the elements are drawn in.
     * @param value The list or structure.
     * @param editing The document, for the reference pickers inside the elements.
     * @return The whole new value when anything changed, and how many extra rows were claimed.
     */
    CompoundEditResult compoundPropertyEditor(StudioFrame& frame, const UiRect& summary,
                                              const std::function<UiRect()>& nextRow,
                                              const PropertyValue& value,
                                              const StudioPropertyEditContext& editing)
    {
        CompoundEditResult result;
        const StudioTheme& theme = frame.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        const float buttonWidth = metricOf(theme, StudioMetric::ControlHeight);

        const bool isList = value.getType() == PropertyType::List;
        const PropertyValue::ListValue list =
            isList ? value.get<PropertyValue::ListValue>() : PropertyValue::ListValue{};
        const PropertyValue::StructureValue structure =
            isList ? PropertyValue::StructureValue{} : value.get<PropertyValue::StructureValue>();

        const std::size_t count = isList ? list.items.size() : structure.fields.size();

        WidgetState& state = frame.state().get(frame.ids().make("compound"));

        UiRect head = summary;

        // The disclosure, then the count, then Add. Add is on the *summary* rather than under the
        // last element, because a list with nothing in it has no last element -- and an empty list
        // that cannot be added to is the state a user meets first.
        StudioButtonOptions disclosure;
        disclosure.icon = state.expanded ? StudioIcon::ChevronDown : StudioIcon::ChevronRight;
        disclosure.iconOnly = true;
        disclosure.kind = StudioButtonKind::Ghost;
        disclosure.focusable = false;
        disclosure.tooltip = state.expanded ? "Collapse" : "Expand";

        const UiRect toggle = head.splitLeft(std::min(buttonWidth, head.width));
        if (studioButton(frame, frame.ids().make("expand"), toggle, "", disclosure).activated)
        {
            state.expanded = !state.expanded;
        }
        head.splitLeft(std::min(spacing, head.width));

        StudioListEdit pending = StudioListEdit::None;
        std::size_t pendingIndex = 0;

        if (isList)
        {
            StudioButtonOptions add;
            add.icon = StudioIcon::Add;
            add.iconOnly = true;
            add.kind = StudioButtonKind::Ghost;
            add.tooltip = "Add an element";
            const UiRect addBox =
                head.splitRight(std::min(buttonWidth, head.width));
            if (studioButton(frame, frame.ids().make("add"), addBox, "", add).activated)
            {
                pending = StudioListEdit::Add;
            }
            head.splitRight(std::min(spacing, head.width));
        }

        if (frame.isDrawPass())
        {
            const std::string text = isList
                ? (count == 1 ? std::string{"1 item"} : std::to_string(count) + " items")
                : (count == 1 ? std::string{"1 field"} : std::to_string(count) + " fields");
            studioDrawText(frame, head,
                           studioTruncateText(frame, theme.font(StudioFontRole::BodySmall), text,
                                              head.width),
                           StudioFontRole::BodySmall,
                           theme.color(StudioColorRole::TextSecondary));
        }

        if (!state.expanded)
        {
            // Add still works while collapsed, because the button is on the summary. The list
            // opens itself so the new element is visible -- an Add whose result is hidden is one
            // the user presses twice.
            if (pending == StudioListEdit::Add)
            {
                state.expanded = true;
                PropertyValue::ListValue edited = list;
                const PropertyValue prototype =
                    edited.items.empty() ? PropertyValue{} : edited.items.back();
                if (studioApplyListEdit(edited, pending, 0, prototype))
                {
                    result.edited = PropertyValue{std::move(edited)};
                }
            }
            return result;
        }

        // One row per element, indented past the label column so the nesting reads.
        std::optional<PropertyValue> elementEdit;
        std::size_t elementIndex = 0;

        for (std::size_t i = 0; i < count; ++i)
        {
            UiRect row = nextRow();
            ++result.rows;

            row.splitLeft(std::min(buttonWidth, row.width));
            PropertyRow parts = splitRow(theme, row);

            if (frame.isDrawPass())
            {
                const std::string name = isList
                    ? "[" + std::to_string(i) + "]"
                    : structure.fields[i].first;
                studioDrawText(frame, parts.label,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body), name,
                                                  parts.label.width),
                               StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }

            if (isList)
            {
                // Up, down and remove, at the right of the row. Three buttons rather than a
                // context menu: a list is rearranged by eye and a menu per element would make
                // moving three elements nine interactions.
                const auto button = [&](const char* id, StudioIcon icon, const char* tip,
                                        bool enabled, StudioListEdit action) {
                    StudioButtonOptions options;
                    options.icon = icon;
                    options.iconOnly = true;
                    options.kind = StudioButtonKind::Ghost;
                    options.tooltip = tip;
                    options.enabled = enabled;
                    const UiRect box = parts.control.splitRight(
                        std::min(buttonWidth, parts.control.width));
                    if (studioButton(frame, frame.ids().make(id), box, "", options).activated)
                    {
                        pending = action;
                        pendingIndex = i;
                    }
                    parts.control.splitRight(std::min(spacing * 0.5f, parts.control.width));
                };

                frame.ids().push("element");
                frame.ids().push(std::to_string(i));
                button("remove", StudioIcon::Delete, "Remove this element", true,
                       StudioListEdit::Remove);
                button("down", StudioIcon::ChevronDown, "Move down", i + 1 < count,
                       StudioListEdit::MoveDown);
                button("up", StudioIcon::ChevronRight, "Move up", i > 0, StudioListEdit::MoveUp);
                frame.ids().pop();
                frame.ids().pop();
            }

            frame.ids().push(isList ? ("item" + std::to_string(i)) : structure.fields[i].first);
            const StudioPropertyEditResult edited = studioPropertyEditor(
                frame, parts.control,
                isList ? list.items[i] : structure.fields[i].second, {}, editing);
            frame.ids().pop();

            // Collected rather than applied here, because applying would rewrite the very vectors
            // this loop is reading -- the same reason the prototype's hierarchy defers a reparent
            // until after its tree is drawn.
            if (edited.edited.has_value() && !elementEdit.has_value())
            {
                elementEdit = edited.edited;
                elementIndex = i;
            }
        }

        if (pending != StudioListEdit::None && isList)
        {
            PropertyValue::ListValue edited = list;
            const PropertyValue prototype =
                edited.items.empty() ? PropertyValue{} : edited.items.back();
            if (studioApplyListEdit(edited, pending, pendingIndex, prototype))
            {
                result.edited = PropertyValue{std::move(edited)};
            }
            return result;
        }

        if (elementEdit.has_value())
        {
            if (isList)
            {
                PropertyValue::ListValue edited = list;
                if (elementIndex < edited.items.size())
                {
                    edited.items[elementIndex] = *elementEdit;
                    result.edited = PropertyValue{std::move(edited)};
                }
            }
            else
            {
                PropertyValue::StructureValue edited = structure;
                if (elementIndex < edited.fields.size())
                {
                    edited.fields[elementIndex].second = *elementEdit;
                    result.edited = PropertyValue{std::move(edited)};
                }
            }
        }

        return result;
    }
}

    bool studioApplyListEdit(PropertyValue::ListValue& list, StudioListEdit edit,
                             std::size_t index, const PropertyValue& prototype)
    {
        switch (edit)
        {
            case StudioListEdit::None:
                return false;

            case StudioListEdit::Add:
                // A copy of the prototype rather than a default-constructed value. A list holds one
                // kind, and an element that arrived as `monostate` would be a row with no editor in
                // a list of rows that have one -- which reads as the list having been corrupted by
                // pressing Add.
                list.items.push_back(prototype);
                return true;

            case StudioListEdit::Remove:
                if (index >= list.items.size()) { return false; }
                list.items.erase(list.items.begin() + static_cast<std::ptrdiff_t>(index));
                return true;

            case StudioListEdit::MoveUp:
                // Refused at the top rather than wrapping to the bottom. Wrapping is never what
                // somebody pressing Up meant, and it is silent when it happens.
                if (index == 0 || index >= list.items.size()) { return false; }
                std::swap(list.items[index - 1], list.items[index]);
                return true;

            case StudioListEdit::MoveDown:
                if (index + 1 >= list.items.size()) { return false; }
                std::swap(list.items[index], list.items[index + 1]);
                return true;
        }
        return false;
    }

    StudioPropertyEditResult studioPropertyEditor(StudioFrame& frame, UiRect control,
                                                  const PropertyValue& value,
                                                  const std::vector<std::string>& enumOptions,
                                                  const StudioPropertyEditContext& editing)
    {
        StudioPropertyEditResult result;
        const StudioTheme& theme = frame.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);


        if (value.getType() == PropertyType::Boolean)
        {
            bool flag = value.get<bool>();
            if (studioCheckbox(frame, frame.ids().make("value"), control, {}, flag)
                    .changed)
            {
                result.edited = PropertyValue{flag};
            }
        }
        else if (value.getType() == PropertyType::Enum && !enumOptions.empty())
        {
            // Chosen, not typed. An enumeration is a closed set the descriptor already
            // names, and a text field over one is a field where every typo is a scene the
            // loader will refuse to open.
            const std::string current = value.get<PropertyValue::EnumValue>().name;
            int selected = -1;
            for (std::size_t i = 0; i < enumOptions.size(); ++i)
            {
                if (enumOptions[i] == current) { selected = static_cast<int>(i); }
            }

            StudioDropdownOptions options;
            // A value the descriptor does not declare is shown rather than blanked: it is
            // a scene written by an older plugin, and hiding it would make the field look
            // empty when it is merely unrecognised.
            options.placeholder = current.empty() ? "(none)" : current;
            if (studioDropdown(frame, frame.ids().make("value"), control,
                               enumOptions, selected, options)
                    .changed
                && selected >= 0)
            {
                result.edited = PropertyValue{PropertyValue::EnumValue{
                    enumOptions[static_cast<std::size_t>(selected)]}};
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
            if (studioTextField(frame, frame.ids().make("value"), control, text)
                    .committed)
            {
                result.edited = isEnum ? PropertyValue{PropertyValue::EnumValue{text}}
                                : PropertyValue{text};
            }
        }
        else if (value.getType() == PropertyType::Float)
        {
            std::string text = formatFloat(value.get<float>());
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("value"), control, text,
                                options)
                    .committed)
            {
                float parsed = 0.0f;
                if (parseFloat(text, parsed)) { result.edited = PropertyValue{parsed}; }
            }
        }
        else if (value.getType() == PropertyType::Integer)
        {
            std::string text = std::to_string(value.get<std::int64_t>());
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("value"), control, text,
                                options)
                    .committed)
            {
                std::int64_t parsed = 0;
                if (parseInteger(text, parsed)) { result.edited = PropertyValue{parsed}; }
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

            if (numericComponents(frame, control, kAxes, components, count, /*integral=*/false,
                                  /*labelled=*/true, &result.dragging))
            {
                if (count == 2)
                {
                    result.edited = PropertyValue{StudioVector2{components[0], components[1]}};
                }
                else if (count == 3)
                {
                    result.edited = PropertyValue{
                        StudioVector3{components[0], components[1], components[2]}};
                }
                else
                {
                    result.edited = PropertyValue{StudioVector4{components[0], components[1],
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
            const StudioQuaternion stored = value.get<StudioQuaternion>();

            // `STUDIO-07057`. The conversion is not injective: at gimbal lock, several triples
            // of angles produce the same rotation, so recomputing fresh from the quaternion every
            // frame can show a different triple from the one just typed -- the field the user is
            // looking at changes under them while they are still looking at it.
            //
            // Retained across frames by this property's own widget-id path, and valid for as long
            // as the stored quaternion is still exactly the one the cached degrees produce.
            // Comparing our own output rather than a dirty flag is what picks up an undo, a gizmo
            // drag, a reload or a selection change the instant any of them lands: none of those
            // happen to reproduce this editor's own rounding, so the comparison fails and the
            // cache is abandoned without having to be told to.
            frame.ids().push("eulerCache");
            WidgetState& cacheX = frame.state().get(frame.ids().make("x"));
            WidgetState& cacheY = frame.state().get(frame.ids().make("y"));
            WidgetState& cacheZ = frame.state().get(frame.ids().make("z"));
            frame.ids().pop();

            const StudioVector3 cachedDegrees{cacheX.scalar, cacheY.scalar, cacheZ.scalar};
            const bool cacheValid =
                cacheX.checked && quaternionFromEulerDegrees(cachedDegrees) == stored;

            const StudioVector3 euler = cacheValid ? cachedDegrees : eulerDegreesOf(stored);
            float components[3] = {euler.x, euler.y, euler.z};

            if (numericComponents(frame, control, kAngles, components, 3, /*integral=*/false,
                                  /*labelled=*/true, &result.dragging))
            {
                const StudioVector3 typed{components[0], components[1], components[2]};
                result.edited = PropertyValue{quaternionFromEulerDegrees(typed)};

                // What was typed, not what it round-trips to: the whole point is to show the
                // number the user entered rather than the equivalent one the extraction prefers.
                cacheX.scalar = typed.x;
                cacheY.scalar = typed.y;
                cacheZ.scalar = typed.z;
                cacheX.checked = true;
            }
        }
        else if (value.getType() == PropertyType::Rectangle)
        {
            static const char* const kEdges[] = {"x", "y", "w", "h"};
            const StudioRectangle rectangle = value.get<StudioRectangle>();
            float components[4] = {
                static_cast<float>(rectangle.x), static_cast<float>(rectangle.y),
                static_cast<float>(rectangle.width), static_cast<float>(rectangle.height)};

            if (numericComponents(frame, control, kEdges, components, 4,
                                  /*integral=*/true, /*labelled=*/true, &result.dragging))
            {
                result.edited = PropertyValue{StudioRectangle{
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
            // `control` is taken by value precisely so the swatch can be split off it here without
            // a copy: the caller's row is unaffected either way.
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
                                  /*integral=*/true, /*labelled=*/false, &result.dragging))
            {
                result.edited = PropertyValue{StudioColor{
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
                for (const AssetRecord* record : editing.context->getAssets().getAll())
                {
                    if (record == nullptr) { continue; }
                    labels.push_back(record->sourcePath);
                    ids.push_back(record->id);
                }
            }
            else
            {
                for (const StudioEntity& candidate : editing.context->getScene().getEntities())
                {
                    // An entity cannot refer to itself: the only thing that can come of
                    // offering it is a cycle nothing downstream expects.
                    if (candidate.getId() == editing.excludeEntity) { continue; }
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
            if (studioDropdown(frame, frame.ids().make("value"), control, labels,
                               selected, options)
                    .changed
                && selected >= 0)
            {
                const Uuid chosen = ids[static_cast<std::size_t>(selected)];
                result.edited = isAsset
                    ? PropertyValue{PropertyValue::AssetReference{chosen}}
                    : PropertyValue{PropertyValue::EntityReference{chosen}};
            }

            // And the slot takes a drop, which is how a user with the Content Browser open
            // expects to fill it -- picking from a list of every asset in the project is
            // the fallback, not the gesture.
            if (isAsset)
            {
                const StudioFrame::StudioDropResult drop = frame.acceptDrop(
                    frame.ids().make("drop"), control,
                    std::string{kStudioAssetDragType});
                if (drop.hovered && frame.isDrawPass())
                {
                    frame.drawList().strokeRect(
                        control, theme.color(StudioColorRole::Accent),
                        metricOf(theme, StudioMetric::FocusRingWidth));
                }
                if (drop.dropped)
                {
                    result.edited = PropertyValue{
                        PropertyValue::AssetReference{Uuid::parse(drop.value)}};
                }
            }
        }
        else
        {
            // Counted in both passes, because it is a property of the value rather than of
            // drawing -- and a caller reading the result from the input pass is exactly
            // who wants to know that a kind fell through.
            result.readOnlyKind = true;
            if (frame.isDrawPass())
            {
                studioDrawText(frame, control,
                               studioTruncateText(frame,
                                                  theme.font(StudioFontRole::BodySmall),
                                                  summarise(value), control.width),
                               StudioFontRole::BodySmall,
                               theme.color(StudioColorRole::TextDisabled));
            }
        }


        return result;
    }

    StudioDetailsResult studioSceneSettings(StudioFrame& frame, const UiRect& area,
                                            StudioContext& context)
    {
        StudioDetailsResult result;
        const StudioTheme& theme = frame.theme();

        const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

        Project& project = context.getProject();
        const SceneEnvironment environment = context.getScene().getEnvironment();
        const std::vector<std::string> layers = project.getLayers();

        // Project, Grid Snap, a gap, Scene Environment's heading plus ambient, fog, fog colour and
        // its two distances, a gap, the Layers heading, one row per layer, and Add.
        const std::size_t rows = 10 + layers.size();

        StudioScrollOptions scroll;
        scroll.contentHeight = static_cast<float>(rows) * (rowHeight + spacing);
        scroll.wheelStep = (rowHeight + spacing) * 3.0f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("scenesettings"), area, scroll);

        UiRect cursor = view.viewport;
        cursor.y -= view.offsetY;
        cursor.height += view.offsetY;

        const auto nextRow = [&]() {
            const UiRect row = cursor.splitTop(rowHeight);
            cursor.splitTop(spacing);
            ++result.rowsDrawn;
            return row;
        };

        const auto heading = [&](const std::string& text) {
            const UiRect row = nextRow();
            if (frame.isDrawPass())
            {
                studioDrawText(frame, row, text, StudioFontRole::Subheading,
                               theme.color(StudioColorRole::TextPrimary));
            }
        };

        const auto label = [&](const UiRect& box, const std::string& text) {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, box,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body), text,
                                                  box.width),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
        };

        frame.ids().push("scene");

        // --- The project ---------------------------------------------------------------------
        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Project");
            // Read-only: the name is the project file's, and renaming a project is renaming a file
            // on disk -- which is a command with a dialog, not a field somebody can edit by
            // accident while looking for the grid snap.
            label(parts.control, project.getName());
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Grid Snap");

            std::string text = formatFloat(project.getGridSnap());
            StudioTextFieldOptions options;
            options.font = StudioFontRole::Monospace;
            options.selectAllOnFocus = true;
            options.placeholder = "0 for none";
            if (studioTextField(frame, frame.ids().make("gridsnap"), parts.control, text, options)
                    .committed)
            {
                float step = 0.0f;
                if (parseFloat(text, step) && step >= 0.0f)
                {
                    context.execute(std::make_unique<SetProjectGridSnapCommand>(project, step));
                }
            }
        }

        nextRow();
        heading("Scene Environment");

        const auto applyEnvironment = [&](const SceneEnvironment& edited, const char* what) {
            context.execute(std::make_unique<SetSceneEnvironmentCommand>(context.getScene(), edited,
                                                                        what));
        };

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Ambient");

            StudioColor colour = environment.ambientColor;
            if (colourField(frame, parts.control, "ambient", colour))
            {
                SceneEnvironment edited = environment;
                edited.ambientColor = colour;
                applyEnvironment(edited, "ambient light");
            }
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Fog");

            bool enabled = environment.fogEnabled;
            if (studioCheckbox(frame, frame.ids().make("fog"), parts.control, {}, enabled).changed)
            {
                SceneEnvironment edited = environment;
                edited.fogEnabled = enabled;
                applyEnvironment(edited, "fog");
            }
        }

        // The fog's own settings only where they do something, on the same rule as the prototype's
        // grid-plane menu item: a control that changes nothing visible is a bug report waiting to
        // be filed.
        if (environment.fogEnabled)
        {
            {
                const PropertyRow parts = splitRow(theme, nextRow());
                label(parts.label, "Fog Colour");

                StudioColor colour = environment.fogColor;
                if (colourField(frame, parts.control, "fogcolour", colour))
                {
                    SceneEnvironment edited = environment;
                    edited.fogColor = colour;
                    applyEnvironment(edited, "fog colour");
                }
            }

            {
                const PropertyRow parts = splitRow(theme, nextRow());
                label(parts.label, "Fog Range");

                static const char* const kEnds[] = {"start", "end"};
                float ends[2] = {environment.fogStart, environment.fogEnd};
                if (numericComponents(frame, parts.control, kEnds, ends, 2))
                {
                    SceneEnvironment edited = environment;
                    edited.fogStart = ends[0];
                    edited.fogEnd = ends[1];
                    applyEnvironment(edited, "fog range");
                }
            }
        }

        nextRow();
        heading("Layers  (" + std::to_string(layers.size()) + ")");

        // --- The project's layers ------------------------------------------------------------
        //
        // The names, which is a different question from the Layers panel's "what is on each": one
        // is the list and the other is its contents, and a user who wants to add a layer has
        // nowhere else to go.
        std::vector<std::string> edited = layers;
        bool layersChanged = false;

        for (std::size_t i = 0; i < layers.size(); ++i)
        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Layer " + std::to_string(i));

            frame.ids().pushIndex(static_cast<std::int64_t>(i));

            UiRect control = parts.control;
            const UiRect remove = control.splitRight(
                std::min(control.width, metricOf(theme, StudioMetric::ControlHeight)));
            control.splitRight(std::min(spacing, control.width));

            std::string name = layers[i];
            if (studioTextField(frame, frame.ids().make("layer"), control, name).committed
                && !name.empty())
            {
                edited[i] = name;
                layersChanged = true;
            }

            StudioButtonOptions options;
            options.kind = StudioButtonKind::Toolbar;
            options.icon = StudioIcon::Delete;
            // The first layer is every entity's default, so removing it would leave the scene
            // pointing at a layer the project no longer declares.
            options.enabled = i > 0;
            options.tooltip = i > 0 ? "Remove this layer." : "The default layer cannot be removed.";
            if (studioButton(frame, frame.ids().make("removelayer"), remove, {}, options).activated)
            {
                edited.erase(edited.begin() + static_cast<std::ptrdiff_t>(i));
                layersChanged = true;
            }

            frame.ids().pop();
        }

        {
            UiRect row = nextRow();
            const UiRect button = row.splitLeft(
                std::min(row.width, std::ceil(studioLabelWidth(frame, "Add Layer")) + spacing * 4.0f));
            if (studioButton(frame, frame.ids().make("addlayer"), button, "Add Layer").activated)
            {
                edited.emplace_back("Layer " + std::to_string(layers.size()));
                layersChanged = true;
            }
        }

        if (layersChanged && frame.isInputPass())
        {
            context.execute(std::make_unique<SetProjectLayersCommand>(
                project, context.getComponentRegistry(), std::move(edited)));
        }

        frame.ids().pop();
        studioEndScroll(frame);
        return result;
    }

    /**
     * @brief The material asset editor: what a `.cnamaterial` holds, edited in place.
     *
     * `plan.md` STUDIO-07046, the last of the five Inspector sections `STUDIO-07041` found with no
     * native answer — and the one the migration inventory had recorded as not existing at all. It
     * said "there is no `.cnamaterial` editor to port; the `material` panel is registered and
     * empty". There is one: `InspectorPanel::drawMaterialAsset`, which is a *section* of the
     * Inspector rather than a panel, which is why an inventory of panels could not see it.
     *
     * ### The file is the document
     *
     * A material is not in the scene and not in the asset database: it is a file, and every edit
     * here rewrites it through `SetMaterialCommand`, which keeps the previous bytes so undo
     * replays them verbatim. There is nothing to invalidate afterwards — the material provider
     * reads the file on every ask rather than caching it, which is exactly what makes an edit
     * visible in the viewport on the next frame.
     *
     * ### A file that cannot be read is refused rather than defaulted
     *
     * Showing an editable form over a file this build could not parse is offering to overwrite it
     * with less than it holds. The three failures are told apart, because they are three different
     * problems: a material whose file has gone, one that is not valid JSON, and one written by a
     * newer Studio.
     *
     * @param frame The frame.
     * @param area The rows left below the asset's identity.
     * @param context The editor; its history receives the edits.
     * @param record The material asset.
     * @param services The effect-name seam.
     * @param rows Advanced by the rows this drew.
     * @return What happened.
     */
    StudioDetailsResult studioMaterialEditor(StudioFrame& frame, UiRect& area,
                                             StudioContext& context, const AssetRecord& record,
                                             const StudioDetailsServices& services)
    {
        StudioDetailsResult result;
        const StudioTheme& theme = frame.theme();

        const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

        const auto nextRow = [&]() {
            const UiRect row = area.splitTop(rowHeight);
            area.splitTop(spacing);
            ++result.rowsDrawn;
            return row;
        };

        const auto say = [&](const UiRect& box, const std::string& text, StudioColorRole role) {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, box,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body), text,
                                                  box.width),
                               StudioFontRole::Body, theme.color(role));
            }
        };

        MaterialDocument material;
        const MaterialLoadProblem problem =
            loadMaterialDocument(context.getAssets(), record.id, material);
        if (problem != MaterialLoadProblem::None)
        {
            say(nextRow(),
                problem == MaterialLoadProblem::Unreadable
                    ? "This material's file cannot be opened."
                    : "This material was written by a newer Studio, or is not valid JSON.",
                StudioColorRole::Warning);
            return result;
        }

        {
            const UiRect row = nextRow();
            if (frame.isDrawPass())
            {
                studioDrawText(frame, row, "Material", StudioFontRole::Subheading,
                               theme.color(StudioColorRole::TextPrimary));
            }
        }

        frame.ids().push("material");

        // Collected rather than applied as they are found, and applied once at the end: every one
        // of these rewrites the file, and two writes in one frame would put two entries in the
        // history for one keystroke.
        std::optional<MaterialDocument> edited;
        std::string editedField;

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            say(parts.label, "Name", StudioColorRole::TextSecondary);
            ++result.materialFields;

            std::string name = material.name;
            StudioTextFieldOptions options;
            options.selectAllOnFocus = true;
            if (studioTextField(frame, frame.ids().make("name"), parts.control, name, options)
                    .committed
                && name != material.name)
            {
                MaterialDocument next = material;
                next.name = name;
                edited = next;
                editedField = "name";
            }
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            say(parts.label, "Base Colour", StudioColorRole::TextSecondary);
            ++result.materialFields;

            StudioVector3 colour = material.diffuseColor;
            if (linearColorRow(frame, parts.control, "diffuse", colour) && !edited.has_value())
            {
                MaterialDocument next = material;
                next.diffuseColor = colour;
                edited = next;
                editedField = "base colour";
            }
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            say(parts.label, "Emissive", StudioColorRole::TextSecondary);
            ++result.materialFields;

            StudioVector3 colour = material.emissiveColor;
            if (linearColorRow(frame, parts.control, "emissive", colour) && !edited.has_value())
            {
                MaterialDocument next = material;
                next.emissiveColor = colour;
                edited = next;
                editedField = "emissive";
            }
        }

        struct ScalarField
        {
            const char* id;
            const char* label;
            float MaterialDocument::*member;
        };
        static const ScalarField kScalars[] = {
            {"metallic", "Metallic", &MaterialDocument::metallic},
            {"roughness", "Roughness", &MaterialDocument::roughness},
            {"alpha", "Alpha", &MaterialDocument::alpha},
        };

        for (const ScalarField& field : kScalars)
        {
            const PropertyRow parts = splitRow(theme, nextRow());
            say(parts.label, field.label, StudioColorRole::TextSecondary);
            ++result.materialFields;

            frame.ids().push(field.id);
            const StudioPropertyEditContext editing{&context, Uuid{}};
            const StudioPropertyEditResult change = studioPropertyEditor(
                frame, parts.control, PropertyValue{material.*field.member}, {}, editing);
            frame.ids().pop();

            if (change.edited.has_value() && !edited.has_value())
            {
                MaterialDocument next = material;
                next.*field.member = change.edited->get<float>(material.*field.member);
                edited = next;
                editedField = field.label;
            }
        }

        // Said plainly rather than left to be discovered: which effect a build got decides whether
        // metallic and roughness reach the screen at all (CNA gap G-05), and on a BasicEffect build
        // they are still not wasted -- the specular colour and power are derived from them.
        if (services.modelEffectName)
        {
            const std::string effect = services.modelEffectName();
            if (!effect.empty())
            {
                say(nextRow(), "Drawn through " + effect + ".", StudioColorRole::TextDisabled);
            }
        }

        frame.ids().pop();

        if (edited.has_value())
        {
            auto command = std::make_unique<SetMaterialCommand>(
                context.getAssets().resolvePath(record.sourcePath), *edited, editedField);
            context.execute(std::move(command), MergePolicy::MergeWithPrevious);
            result.edited = true;
            result.editedProperty = fileNameOf(record.sourcePath) + "." + editedField;
        }

        return result;
    }

    StudioDetailsResult studioAssetInspector(StudioFrame& frame, const UiRect& area,
                                             StudioContext& context, const Uuid& assetId,
                                             const StudioDetailsServices& services)
    {
        StudioDetailsResult result;
        const StudioTheme& theme = frame.theme();

        const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

        const AssetRecord* record = context.getAssets().find(assetId);
        if (record == nullptr)
        {
            // Selected and then deleted, or a project closed under it. Said rather than falling
            // back to the scene settings, which would look like the click had not registered.
            if (frame.isDrawPass())
            {
                studioDrawText(frame, area, "This asset is no longer in the database.",
                               StudioFontRole::Body, theme.color(StudioColorRole::Warning));
            }
            return result;
        }

        const ComponentDescriptor* descriptor =
            context.getImporterRegistry().find(record->importerId);
        const std::vector<PropertyDescriptor>* properties =
            descriptor != nullptr ? &descriptor->properties : nullptr;

        // What the dependency section will add: a heading, the two counted headings, and a row per
        // reference in each direction (STUDIO-09012). Counted here rather than guessed, because the
        // scroll extent is what decides whether the last row can be reached.
        const std::vector<AssetUsage> usedBy =
            services.dependencies != nullptr ? services.dependencies->referencedBy(assetId)
                                             : std::vector<AssetUsage>{};
        const std::vector<Uuid> uses =
            services.dependencies != nullptr ? services.dependencies->referencesTo(assetId)
                                             : std::vector<Uuid>{};
        const std::size_t dependencyRows =
            services.dependencies != nullptr ? 3 + usedBy.size() + uses.size() : 2;

        // Name, path, kind, a gap, the importer's heading, and one row per setting -- plus the
        // preview row when this is something that can be heard, and the material editor's own
        // rows when this is a material: a heading, six fields and the effect line.
        const std::size_t rows = 6 + (isAudibleAsset(record->type) ? 1u : 0u)
                                 + (record->type == AssetType::Material ? 8u : 0u)
                                 + (properties != nullptr ? properties->size() : 0)
                                 + dependencyRows;

        StudioScrollOptions scroll;
        scroll.contentHeight = static_cast<float>(rows) * (rowHeight + spacing);
        scroll.wheelStep = (rowHeight + spacing) * 3.0f;

        const StudioScrollResult view =
            studioBeginScroll(frame, frame.ids().make("assetinspector"), area, scroll);

        UiRect cursor = view.viewport;
        cursor.y -= view.offsetY;
        cursor.height += view.offsetY;

        const auto nextRow = [&]() {
            const UiRect row = cursor.splitTop(rowHeight);
            cursor.splitTop(spacing);
            ++result.rowsDrawn;
            return row;
        };

        const auto label = [&](const UiRect& box, const std::string& text, StudioColorRole role) {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, box,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body), text,
                                                  box.width),
                               StudioFontRole::Body, theme.color(role));
            }
        };

        frame.ids().push("asset");

        {
            // The file name rather than the whole path, in the heading face and beside the kind's
            // own icon -- the same vocabulary the Content Browser used to say what this is, so the
            // panel reads as being about the row that was clicked rather than about a path.
            const UiRect row = nextRow();
            UiRect line = row;
            const UiRect icon = line.splitLeft(std::min(rowHeight, line.width));
            line.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingXSmall), line.width));
            if (frame.isDrawPass())
            {
                studioDrawIcon(frame, icon.inset(UiEdges{4.0f}), studioAssetIcon(record->type),
                               theme.color(StudioColorRole::TextPrimary));
                studioDrawText(frame, line,
                               studioTruncateText(frame, theme.font(StudioFontRole::Subheading),
                                                  fileNameOf(record->sourcePath), line.width),
                               StudioFontRole::Subheading,
                               theme.color(StudioColorRole::TextPrimary));
            }
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Path", StudioColorRole::TextSecondary);
            // The whole path, truncated from the *left* when it does not fit: the end of a path is
            // what identifies a file and the start is what every asset in a project has in common.
            label(parts.control, record->sourcePath, StudioColorRole::TextPrimary);
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Type", StudioColorRole::TextSecondary);
            label(parts.control, std::string{toString(record->type)}, StudioColorRole::TextPrimary);
        }

        {
            const PropertyRow parts = splitRow(theme, nextRow());
            label(parts.label, "Id", StudioColorRole::TextSecondary);
            label(parts.control, record->id.toString(), StudioColorRole::TextDisabled);
        }

        // Offered on the asset itself as well as on a component that references it: hearing a clip
        // is most often wanted right after importing it, when no entity uses it yet.
        //
        // Neutral settings, unlike the component preview -- this is the file as imported, with
        // nothing an entity chose applied to it.
        if (isAudibleAsset(record->type))
        {
            result.audio = studioAudioPreviewRow(frame, nextRow(), theme, services,
                                                 context.getAssets(), assetId, 1.0f, 0.0f, 0.0f);
        }

        nextRow();

        // One exit from here on, rather than a `return` per case. The dependency section below
        // (STUDIO-09012) belongs to *every* asset, and it was skipped for two of the three when
        // each branch returned for itself -- which is most assets, because most have no importer
        // settings at all.
        if (record->type == AssetType::Material)
        {
            // A material is the editor's *own* document rather than something imported, so it gets
            // its fields here instead of an importer's settings form.
            const StudioDetailsResult material =
                studioMaterialEditor(frame, cursor, context, *record, services);
            result.rowsDrawn += material.rowsDrawn;
            result.materialFields = material.materialFields;
            result.edited = material.edited;
            result.editedProperty = material.editedProperty;
        }
        else if (properties == nullptr || properties->empty())
        {
            // An importer with no declared settings is not a fault -- most have nothing worth
            // choosing. Saying so beats an empty form the user waits for something to appear in.
            const UiRect row = nextRow();
            // A material is the editor's *own* document rather than something imported, so "no
            // importer" is true and useless: it reads as a fault when the honest answer is that
            // the editor for it has not been written. Saying which is the difference between a
            // gap somebody can look up and one they report as a bug.
            const std::string text =
                record->importerId.empty()
                    ? std::string{"No importer handles this file type."}
                    : (descriptor == nullptr
                           ? record->importerId + " is not registered in this build."
                           : record->importerId + " has no settings.");
            label(row, text, StudioColorRole::TextSecondary);
        }
        else
        {
            {
                const UiRect row = nextRow();
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, row,
                                   descriptor->displayName.empty() ? record->importerId
                                                                   : descriptor->displayName,
                                   StudioFontRole::Subheading,
                                   theme.color(StudioColorRole::TextPrimary));
                }
            }

            frame.ids().push(record->importerId);

            for (const PropertyDescriptor& property : *properties)
            {
                const PropertyRow parts = splitRow(theme, nextRow());
                label(parts.label,
                      property.displayName.empty() ? property.name : property.displayName,
                      StudioColorRole::TextSecondary);

                // The stored setting when the sidecar carries one, the declared default otherwise.
                // Writing every default into the sidecar on first sight would make each asset's diff
                // noise, so absent stays absent until the user actually chooses something.
                const JsonValue& stored = record->importerSettings[property.name];
                const PropertyValue value = stored.isNull()
                    ? property.defaultValue
                    : PropertyValue::fromJson(stored, property.type);

                frame.ids().push(property.name);

                StudioPropertyEditResult edit;
                if (property.readOnly)
                {
                    // Declared read-only by the importer. Shown as text rather than as a control the
                    // user can put a caret in and then find refuses them -- a disabled field that
                    // takes focus is one somebody reports as broken.
                    edit.readOnlyKind = true;
                    label(parts.control, describeValue(value), StudioColorRole::TextDisabled);
                }
                else if (value.getType() == PropertyType::List
                         || value.getType() == PropertyType::Structure)
                {
                    // The same expanding editor the component grid uses (STUDIO-07054), through the
                    // same row allocator. An importer setting that is a list is a list, and giving it
                    // a second editor here would be the drift `STUDIO-07045` extracted this code to
                    // avoid.
                    const StudioPropertyEditContext editing{&context, Uuid{}};
                    const CompoundEditResult compound =
                        compoundPropertyEditor(frame, parts.control, nextRow, value, editing);
                    edit.edited = compound.edited;
                }
                else
                {
                    const StudioPropertyEditContext editing{&context, Uuid{}};
                    edit = studioPropertyEditor(frame, parts.control, value, property.enumOptions,
                                                editing);
                }
                if (edit.readOnlyKind) { ++result.readOnlyProperties; }

                frame.ids().pop();

                if (!edit.edited.has_value()) { continue; }

                auto command = std::make_unique<SetImporterSettingCommand>(
                    context.getAssets(), assetId, property.name, *edit.edited);
                if (!command->isValid()) { continue; }

                // Merging, like every other property field: dragging a value is one undo entry that
                // returns to what the drag started from. And through the history at all, because an
                // importer setting is persisted to the sidecar -- an edit that could not be undone
                // would be the one edit in Studio that cannot.
                context.execute(std::move(command), MergePolicy::MergeWithPrevious);
                result.edited = true;
                result.editedProperty = record->sourcePath + "." + property.name;
                break;
            }

            frame.ids().pop();
        }

        // --- What uses this, and what this uses (STUDIO-09012) --------------------------------
        //
        // The question a user opens an asset to answer before they delete it, and the one nothing
        // on disk records: a scene holds a Uuid, so "what breaks if this goes" needs the reverse
        // map. Both directions, because they are different questions -- "is this safe to remove"
        // and "what did this model come with".
        {
            frame.ids().push("deps");
            label(nextRow(), "Dependencies", StudioColorRole::TextSecondary);

            if (services.dependencies == nullptr)
            {
                // Said rather than left out. A section that is simply absent is one a user cannot
                // tell from an asset nothing references, and those are opposite answers.
                label(nextRow(), "Not indexed in this build.", StudioColorRole::TextDisabled);
            }
            else
            {
                Uuid navigateTo;

                const auto section = [&](const char* heading, std::size_t count,
                                         const auto& drawRows) {
                    label(nextRow(),
                          std::string{heading} + " (" + std::to_string(count) + ")",
                          StudioColorRole::TextSecondary);

                    // A count of zero is an answer, and a better one than an empty gap: "nothing
                    // references this" is what makes a delete safe, and a section that showed
                    // nothing would read as a section that had not loaded.
                    if (count == 0)
                    {
                        label(nextRow(), "    Nothing.", StudioColorRole::TextDisabled);
                        return;
                    }
                    drawRows();
                };

                section("Used by", usedBy.size(), [&] {
                    frame.ids().push("usedby");
                    for (std::size_t i = 0; i < usedBy.size(); ++i)
                    {
                        const AssetUsage& usage = usedBy[i];
                        StudioButtonOptions options;
                        options.kind = StudioButtonKind::Ghost;
                        options.align = StudioTextAlign::Left;
                        options.icon = studioAssetIcon(usage.holderType);
                        options.tooltip = usage.holderPath;
                        if (studioButton(frame, frame.ids().makeIndex(static_cast<std::int64_t>(i)),
                                         nextRow(), usage.describe(), options).activated)
                        {
                            navigateTo = usage.holderId;
                        }
                    }
                    frame.ids().pop();
                });

                section("Uses", uses.size(), [&] {
                    frame.ids().push("uses");
                    for (std::size_t i = 0; i < uses.size(); ++i)
                    {
                        const AssetRecord* target = context.getAssets().find(uses[i]);

                        StudioButtonOptions options;
                        options.kind = StudioButtonKind::Ghost;
                        options.align = StudioTextAlign::Left;
                        options.icon = target != nullptr ? studioAssetIcon(target->type)
                                                         : StudioIcon::Warning;

                        // An id with no record is a reference this asset makes to something the
                        // database does not have -- which is the single most useful row in the
                        // section, so it is shown as what it is rather than skipped.
                        const std::string text = target != nullptr
                            ? target->sourcePath
                            : "Missing: " + uses[i].toString();
                        options.tooltip = text;

                        if (studioButton(frame, frame.ids().makeIndex(static_cast<std::int64_t>(i)),
                                         nextRow(), text, options).activated
                            && target != nullptr)
                        {
                            navigateTo = uses[i];
                        }
                    }
                    frame.ids().pop();
                });

                // Applied after both lists, because selecting from inside the loop would change
                // what the rest of this frame is describing half-way through drawing it.
                if (navigateTo.isValid() && frame.isInputPass())
                {
                    context.selectAsset(navigateTo);
                    result.navigatedToAsset = navigateTo;
                }
            }

            result.dependencyRows = usedBy.size() + uses.size();
            frame.ids().pop();
        }

        frame.ids().pop();
        studioEndScroll(frame);
        return result;
    }

    StudioDetailsResult studioMaterialPanel(StudioFrame& frame, const UiRect& bounds,
                                            StudioContext& context,
                                            const StudioDetailsServices& services)
    {
        StudioDetailsResult result;
        const StudioTheme& theme = frame.theme();

        UiRect area = bounds.inset(UiEdges{metricOf(theme, StudioMetric::SpacingSmall)});
        if (area.width <= 0.0f || area.height <= 0.0f) { return result; }

        const AssetRecord* record = context.getSelectedAsset().isValid()
            ? context.getAssets().find(context.getSelectedAsset())
            : nullptr;

        if (record == nullptr || record->type != AssetType::Material)
        {
            // A next action rather than a dead end, which is the rule every other empty state in
            // Studio follows: "nothing to show" leaves a user looking for the thing that is
            // broken.
            if (frame.isDrawPass())
            {
                // Short enough to fit a narrow dock. The longer form -- "Select a material in the
                // Content Browser to edit it" -- ran off the edge of this panel at the width the
                // default layout gives it, which is the one width it is guaranteed to be seen at.
                studioDrawText(frame, area,
                               studioTruncateText(frame, theme.font(StudioFontRole::Body),
                                                  context.hasProject()
                                                      ? "Select a material to edit it."
                                                      : "No project is open.",
                                                  area.width),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        // The file name first, because this panel is not beside the asset inspector's identity
        // rows and would otherwise be a form with no subject.
        const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

        frame.ids().push("materialpanel");
        {
            UiRect line = area.splitTop(rowHeight);
            area.splitTop(spacing);
            ++result.rowsDrawn;

            const UiRect icon = line.splitLeft(std::min(rowHeight, line.width));
            line.splitLeft(std::min(metricOf(theme, StudioMetric::SpacingXSmall), line.width));
            if (frame.isDrawPass())
            {
                studioDrawIcon(frame, icon.inset(UiEdges{4.0f}), StudioIcon::Material,
                               theme.color(StudioColorRole::TextPrimary));
                studioDrawText(frame, line,
                               studioTruncateText(frame, theme.font(StudioFontRole::Subheading),
                                                  fileNameOf(record->sourcePath), line.width),
                               StudioFontRole::Subheading,
                               theme.color(StudioColorRole::TextPrimary));
            }
        }

        const StudioDetailsResult editor =
            studioMaterialEditor(frame, area, context, *record, services);
        frame.ids().pop();

        result.rowsDrawn += editor.rowsDrawn;
        result.materialFields = editor.materialFields;
        result.edited = editor.edited;
        result.editedProperty = editor.editedProperty;
        return result;
    }

    StudioDetailsResult studioDetailsPanel(StudioFrame& frame, const UiRect& bounds,
                                           StudioContext& context,
                                           const StudioDetailsServices& services)
    {
        StudioDetailsResult result;
        const StudioTheme& theme = frame.theme();

        const float padding = metricOf(theme, StudioMetric::SpacingSmall);
        const float rowHeight = std::max(metricOf(theme, StudioMetric::ControlHeight),
                                         metricOf(theme, StudioMetric::MinimumHitTarget));
        const float spacing = metricOf(theme, StudioMetric::SpacingXSmall);

        UiRect area = bounds.inset(UiEdges{padding});
        if (area.width <= 0.0f || area.height <= 0.0f) { return result; }

        // An asset first, because `StudioContext::selectAsset` clears the entity selection: the
        // panel shows one thing at a time, and which one is decided by what was clicked last.
        if (context.getSelectedAsset().isValid())
        {
            return studioAssetInspector(frame, area, context, context.getSelectedAsset(), services);
        }

        const std::vector<Uuid>& selection = context.getSelection();
        if (selection.empty())
        {
            if (!context.hasProject())
            {
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, area, "No project is open.", StudioFontRole::Body,
                                   theme.color(StudioColorRole::TextSecondary));
                }
                return result;
            }

            // Not a dead end. A setting that belongs to no entity has to live somewhere, and the
            // inspector standing idle is where the prototype put it -- which the panel inventory
            // could not see, because the inventory accounts for panels and this one is ported.
            // `docs/VISUAL-ACCEPTANCE.md` found it by looking at the two editors side by side.
            return studioSceneSettings(frame, area, context);
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
                // And its preview, which is a row of the grid like any other.
                if (component.getTypeId() == BuiltinComponentIds::kAudioSource) { ++rows; }
                // The sprite preview is a transport row and a picture.
                if (component.getTypeId() == BuiltinComponentIds::kSpriteAnimation)
                {
                    rows += 1 + kAnimationPreviewRows;
                }
            }
            return rows;
        }();

        // name, enabled, a gap, and the Add Component row at the bottom -- plus the prefab
        // section when there is one. Reserved at its maximum rather than at what it will draw:
        // the count comes from a comparison the section has not run yet at this point, and a
        // scroll view that is a row too tall is invisible where one a row too short clips the
        // last control.
        const std::size_t totalRows =
            componentRows + 4
            + (findInstanceRoot(context.getScene(), entityId).isValid() ? kPrefabSectionRows + 1
                                                                        : 0u);

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

        // --- The prefab this entity came from, if any ----------------------------------------
        //
        // `STUDIO-07042`. Above the components, where the prototype draws it: what an entity *is*
        // comes before what it holds, and an instance whose prefab is missing is something a user
        // needs told before they start editing components that are about to be reverted.
        {
            const std::size_t before = result.rowsDrawn;
            UiRect section = cursor;
            result.prefab = studioPrefabSection(frame, section, theme, context, entityId);
            if (result.prefab.present)
            {
                const float consumed = section.y - cursor.y;
                cursor.splitTop(consumed);
                result.rowsDrawn =
                    before + static_cast<std::size_t>(std::lround(consumed / (rowHeight + spacing)));
            }
        }

        nextRow();

        // --- Components ---------------------------------------------------------------------
        //
        // Collected rather than applied in the loop, because removing a component rebuilds the
        // vector this loop is walking. The same reason the property edit below breaks out of its
        // own loop, and the same failure if it did not.
        std::optional<std::size_t> removing;
        std::size_t nextComponentIndex = 0;

        for (const StudioComponent& component : entity->getComponents())
        {
            const std::size_t componentIndex = nextComponentIndex++;
            const ComponentDescriptor* descriptor =
                context.getComponentRegistry().find(component.getTypeId());

            {
                UiRect header = nextRow();
                if (frame.isDrawPass())
                {
                    frame.drawList().fillRect(header, theme.color(StudioColorRole::PanelHeader));
                }

                // `STUDIO-07040`. On the header rather than in a context menu, because a component
                // that can be added and not removed is a mistake a user cannot undo except through
                // the history -- and reaching for Undo to correct a click is not the same thing as
                // a Remove.
                frame.ids().push(component.getTypeId());
                frame.ids().pushIndex(static_cast<std::int64_t>(componentIndex));
                {
                    const UiRect box = header.splitRight(std::min(header.width, rowHeight));

                    // Asked of the command rather than decided here. A descriptor may mark a
                    // component required -- a transform is -- and two places deciding that is one
                    // place that will eventually say a thing the other refuses.
                    RemoveComponentCommand probe{context.getScene(),
                                                 context.getComponentRegistry(), entityId,
                                                 componentIndex};

                    StudioButtonOptions options;
                    options.icon = StudioIcon::Delete;
                    options.iconOnly = true;
                    options.enabled = probe.isValid();
                    options.tooltip = probe.isValid()
                        ? "Remove this component"
                        : "This component cannot be removed from this entity";

                    if (studioButton(frame, frame.ids().make("remove"), box, "Remove", options)
                            .activated)
                    {
                        removing = componentIndex;
                    }
                }
                frame.ids().pop();
                frame.ids().pop();

                if (frame.isDrawPass())
                {
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

            // Read before the property loop, which may break out of itself the moment an edit
            // lands. Values rather than a reference into the component, so the preview below is
            // drawn from something that cannot have been invalidated by the edit that ended it.
            const bool isAudioSource = component.getTypeId() == BuiltinComponentIds::kAudioSource;
            const Uuid clipId =
                isAudioSource ? component.getPropertyOrDefault("clip", descriptor)
                                    .get<PropertyValue::AssetReference>()
                                    .id
                              : Uuid{};
            const float clipVolume =
                isAudioSource ? component.getPropertyOrDefault("volume", descriptor).get<float>(1.0f)
                              : 1.0f;
            const float clipPitch =
                isAudioSource ? component.getPropertyOrDefault("pitch", descriptor).get<float>(0.0f)
                              : 0.0f;
            const float clipPan =
                isAudioSource ? component.getPropertyOrDefault("pan", descriptor).get<float>(0.0f)
                              : 0.0f;

            // The same rule for the sprite clip, and for the same reason: an edit to any property
            // of this component breaks out of the loop below, and the preview under it is drawn
            // from values rather than from a reference that edit may have invalidated.
            const bool isSpriteAnimation =
                component.getTypeId() == BuiltinComponentIds::kSpriteAnimation;
            const SpriteAnimationClip spriteClip =
                isSpriteAnimation ? readSpriteAnimationClip(component, descriptor)
                                  : SpriteAnimationClip{};
            const Uuid sheetId =
                isSpriteAnimation ? component.getPropertyOrDefault(SpriteAnimationKeys::kSheet,
                                                                   descriptor)
                                        .get<PropertyValue::AssetReference>()
                                        .id
                                  : Uuid{};

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

                const StudioPropertyEditContext editing{&context, entityId};

                // Lists and structures claim rows of their own (STUDIO-07054). Everything else is
                // a control in the one rect the row already gave it.
                StudioPropertyEditResult editResult;
                if (value.getType() == PropertyType::List
                    || value.getType() == PropertyType::Structure)
                {
                    const CompoundEditResult compound =
                        compoundPropertyEditor(frame, parts.control, nextRow, value, editing);
                    editResult.edited = compound.edited;
                }
                else
                {
                    editResult = studioPropertyEditor(frame, parts.control, value,
                                                      property.enumOptions, editing);
                }
                if (editResult.readOnlyKind) { ++result.readOnlyProperties; }
                const std::optional<PropertyValue>& edited = editResult.edited;
                frame.ids().pop();
                frame.ids().pop();

                if (edited.has_value())
                {
                    // Through the history, always. Showing a scene wrong is a bad afternoon and
                    // editing one wrong is a lost afternoon's work, so nothing here touches an
                    // entity directly.
                    //
                    // Merged while a scrub is in flight (STUDIO-07055), so a drag across forty
                    // pixels is one undo entry rather than forty. The chain is closed by
                    // `endInteraction` on the first frame nothing is being dragged, which is what
                    // stops two separate drags of the same field from folding into each other.
                    context.execute(std::make_unique<SetPropertyCommand>(
                        context.getScene(), entityId, component.getTypeId(), property.name,
                        *edited),
                        editResult.dragging ? MergePolicy::MergeWithPrevious
                                            : MergePolicy::NewEntry);
                    result.edited = true;
                    result.editedProperty = component.getTypeId() + "." + property.name;

                    // The component list may have been rebuilt underneath this loop.
                    break;
                }
            }

            // The preview, under the properties it plays with. Per *source* rather than per
            // entity: `CNA.AudioSource` is declared non-unique, so an entity may carry several,
            // and the prototype's `findComponent` preview can only ever hear the first of them.
            //
            // Scoped by type and index like the header's Remove button, because two sources on one
            // entity would otherwise share one widget identity -- and two buttons with one id is a
            // press that lands on whichever of them the state store saw last.
            if (isAudioSource)
            {
                frame.ids().push(component.getTypeId());
                frame.ids().pushIndex(static_cast<std::int64_t>(componentIndex));
                const StudioAudioPreviewResult preview =
                    studioAudioPreviewRow(frame, nextRow(), theme, services, context.getAssets(),
                                          clipId, clipVolume, clipPitch, clipPan);
                frame.ids().pop();
                frame.ids().pop();

                result.audio.controls += preview.controls;
                if (preview.played || preview.stopped)
                {
                    result.audio.played = preview.played;
                    result.audio.stopped = preview.stopped;
                    result.audio.started = preview.started;
                    result.audio.clip = preview.clip;
                }
            }

            // The sprite animation preview (STUDIO-07043), under the properties it plays. Scoped
            // the same way, because `CNA.SpriteAnimation` is non-unique too.
            if (isSpriteAnimation)
            {
                UiRect block = cursor.splitTop((rowHeight + spacing)
                                               * static_cast<float>(1 + kAnimationPreviewRows));
                cursor.splitTop(spacing);
                result.rowsDrawn += 1 + kAnimationPreviewRows;

                frame.ids().push(component.getTypeId());
                frame.ids().pushIndex(static_cast<std::int64_t>(componentIndex));
                const StudioAnimationPreviewResult preview =
                    studioAnimationPreview(frame, block, theme, services, context.getAssets(),
                                           spriteClip, sheetId, entityId);
                frame.ids().pop();
                frame.ids().pop();

                // The last one described wins, which is the same rule the panel already uses for
                // which entity it is about: one preview travels to the viewport, because the
                // viewport draws one scene.
                if (preview.preview.isActive())
                {
                    result.animation = preview.preview;
                    result.animationFrames = preview.frames;
                }
            }
        }

        // --- Add Component ---------------------------------------------------------------------
        //
        // `STUDIO-07040`, and the gap that stopped Dear ImGui being deleted. The prototype's
        // Inspector has had this since it existed; the native Details panel had no way to add a
        // component *at all*, so an entity created in the native shell could never be given
        // anything to do. The migration inventory did not catch it because it accounts for panels,
        // menus, toolbars and shortcuts -- and this is a button inside a panel.
        nextRow();
        {
            const PropertyRow parts = splitRow(theme, nextRow());
            frame.ids().push("addcomponent");

            std::vector<std::string> labels;
            std::vector<std::string> typeIds;
            for (const std::string& typeId : context.getComponentRegistry().getTypeIds())
            {
                const ComponentDescriptor* candidate = context.getComponentRegistry().find(typeId);
                if (candidate == nullptr) { continue; }

                // A unique component the entity already has cannot be added again, so listing it
                // would be listing an entry that does nothing -- AddComponentCommand refuses it
                // anyway, and a control that refuses is indistinguishable from one that is broken.
                if (candidate->unique && entity->findComponent(typeId) != nullptr) { continue; }

                labels.push_back(candidate->category.empty()
                                     ? candidate->displayName
                                     : candidate->category + " / " + candidate->displayName);
                typeIds.push_back(typeId);
            }

            if (labels.empty())
            {
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, parts.label, "Add Component", StudioFontRole::Body,
                                   theme.color(StudioColorRole::TextDisabled));
                    studioDrawText(frame, parts.control, "Every type is already on this entity.",
                                   StudioFontRole::BodySmall,
                                   theme.color(StudioColorRole::TextSecondary));
                }
            }
            else
            {
                // The choice is remembered as a *type id* and resolved to an index every frame,
                // not kept as an index: the list shortens the moment a unique component is added,
                // and a remembered index would then silently point at a different type.
                WidgetState& state = frame.state().get(frame.ids().make("choice"));
                int chosen = 0;
                for (std::size_t index = 0; index < typeIds.size(); ++index)
                {
                    if (typeIds[index] == state.text) { chosen = static_cast<int>(index); break; }
                }
                state.text = typeIds[static_cast<std::size_t>(chosen)];

                UiRect control = parts.control;
                const UiRect addButton =
                    control.splitRight(std::min(control.width, rowHeight * 3.0f));
                control.splitRight(std::min(spacing, control.width));

                if (frame.isDrawPass())
                {
                    studioDrawText(frame, parts.label, "Add Component", StudioFontRole::Body,
                                   theme.color(StudioColorRole::TextSecondary));
                }

                if (studioDropdown(frame, frame.ids().make("type"), control, labels, chosen).changed
                    && chosen >= 0 && static_cast<std::size_t>(chosen) < typeIds.size())
                {
                    state.text = typeIds[static_cast<std::size_t>(chosen)];
                }

                StudioButtonOptions options;
                options.icon = StudioIcon::Add;
                options.tooltip = "Add this component to the selected entity";
                if (studioButton(frame, frame.ids().make("add"), addButton, "Add", options)
                        .activated)
                {
                    auto command = std::make_unique<AddComponentCommand>(
                        context.getScene(), context.getComponentRegistry(), entityId, state.text);
                    // Asked before it is pushed, so the undo stack never gains an entry that does
                    // nothing. The list above already excludes the refusals this can name, which
                    // makes this the belt to that braces rather than the only check.
                    if (command->isValid())
                    {
                        context.execute(std::move(command));
                        result.edited = true;
                        result.editedProperty = state.text;
                    }
                }
            }

            frame.ids().pop();
        }

        // After the loop and after the Add row, for the reason it was collected: removing a
        // component rebuilds the vector both were walking.
        if (removing.has_value())
        {
            auto command = std::make_unique<RemoveComponentCommand>(
                context.getScene(), context.getComponentRegistry(), entityId, *removing);
            if (command->isValid())
            {
                result.edited = true;
                result.editedProperty = command->getDescription();
                context.execute(std::move(command));
            }
        }

        studioEndScroll(frame);
        return result;
    }
}
