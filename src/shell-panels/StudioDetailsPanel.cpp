// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDetailsPanel.cpp
 * @brief The Details panel.
 */

#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/Assets/AssetCommands.hpp"

#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/NumberText.hpp"
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

        bool numericComponents(StudioFrame& frame, const UiRect& bounds, const char* const* names,
                               float* values, int count, bool integral = false,
                               bool labelled = true)
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
                // The component's own letter, inside the field, always. It was on `placeholder`,
                // which shows only while a field is *empty* -- so every populated Position,
                // Rotation and Scale in Studio was three unlabelled boxes, which is precisely the
                // case the letters exist for.
                if (labelled)
                {
                    options.prefix = axisLabelFor(names[i]);
                    options.prefixRole = axisRoleFor(names[i]);
                }

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

        /** @brief Whether a preview can be offered for an asset of this kind at all. */
        bool isAudibleAsset(AssetType type)
        {
            return type == AssetType::SoundEffect || type == AssetType::Song;
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

            if (numericComponents(frame, control, kAxes, components, count))
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
            const StudioVector3 euler = eulerDegreesOf(value.get<StudioQuaternion>());
            float components[3] = {euler.x, euler.y, euler.z};

            if (numericComponents(frame, control, kAngles, components, 3))
            {
                result.edited = PropertyValue{quaternionFromEulerDegrees(
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

            if (numericComponents(frame, control, kEdges, components, 4,
                                  /*integral=*/true))
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
                                  /*integral=*/true, /*labelled=*/false))
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

        // Name, path, kind, a gap, the importer's heading, and one row per setting -- plus the
        // preview row when this is something that can be heard.
        const std::size_t rows = 6 + (isAudibleAsset(record->type) ? 1u : 0u)
                                 + (properties != nullptr ? properties->size() : 0);

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

        if (properties == nullptr || properties->empty())
        {
            // An importer with no declared settings is not a fault -- most have nothing worth
            // choosing. Saying so beats an empty form the user waits for something to appear in.
            const UiRect row = nextRow();
            // A material is the editor's *own* document rather than something imported, so "no
            // importer" is true and useless: it reads as a fault when the honest answer is that
            // the editor for it has not been written. Saying which is the difference between a
            // gap somebody can look up and one they report as a bug.
            const std::string text =
                record->type == AssetType::Material
                    ? std::string{"A material is edited by the material editor, which the native "
                                  "shell does not have yet (STUDIO-07046)."}
                    : (record->importerId.empty()
                           ? std::string{"No importer handles this file type."}
                           : (descriptor == nullptr
                                  ? record->importerId + " is not registered in this build."
                                  : record->importerId + " has no settings."));
            label(row, text, StudioColorRole::TextSecondary);
            frame.ids().pop();
            studioEndScroll(frame);
            return result;
        }

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
        frame.ids().pop();
        studioEndScroll(frame);
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
            }
            return rows;
        }();

        // name, enabled, a gap, and the Add Component row at the bottom.
        const std::size_t totalRows = componentRows + 4;

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
                const StudioPropertyEditResult editResult = studioPropertyEditor(
                    frame, parts.control, value, property.enumOptions, editing);
                if (editResult.readOnlyKind) { ++result.readOnlyProperties; }
                const std::optional<PropertyValue>& edited = editResult.edited;
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
