// SPDX-License-Identifier: MS-PL
/**
 * @file StudioBuildPanel.cpp
 * @brief The Build panel: the six-axis target profile, and the button that builds it.
 */

#include "CNA/Studio/ShellPanels/StudioBuildPanel.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief The height a scrolled cursor is given before anything is laid out.
         *
         * Large enough that `splitTop` never runs out, and finite so that a rectangle derived from
         * it is still a number. The real height is what the layout consumed, measured afterwards.
         */
        constexpr float kUnboundedContentHeight = 100000.0f;

        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /** @brief Every operating system the profile model knows, in display order. */
        const std::vector<StudioTargetOs>& knownOperatingSystems()
        {
            static const std::vector<StudioTargetOs> all{
                StudioTargetOs::Windows, StudioTargetOs::Linux, StudioTargetOs::MacOs,
                StudioTargetOs::Android, StudioTargetOs::IOs, StudioTargetOs::Web};
            return all;
        }

        /** @brief Every architecture the profile model knows, in display order. */
        const std::vector<StudioArchitecture>& knownArchitectures()
        {
            static const std::vector<StudioArchitecture> all{
                StudioArchitecture::X86_64, StudioArchitecture::X86, StudioArchitecture::Arm64,
                StudioArchitecture::Arm32, StudioArchitecture::Wasm32};
            return all;
        }

        /** @brief Every configuration, in the order a developer reaches for them. */
        const std::vector<StudioBuildConfiguration>& knownConfigurations()
        {
            static const std::vector<StudioBuildConfiguration> all{
                StudioBuildConfiguration::Debug, StudioBuildConfiguration::Release,
                StudioBuildConfiguration::RelWithDebInfo, StudioBuildConfiguration::MinSizeRel};
            return all;
        }

        /** @brief Index of @p value in @p all, or -1. */
        template <typename T>
        int indexOf(const std::vector<T>& all, T value)
        {
            const auto found = std::find(all.begin(), all.end(), value);
            return found == all.end() ? -1 : static_cast<int>(found - all.begin());
        }

        /** @brief Index of @p value in @p all, or -1. */
        int indexOfName(const std::vector<std::string>& all, std::string_view value)
        {
            for (std::size_t i = 0; i < all.size(); ++i)
            {
                if (all[i] == value) { return static_cast<int>(i); }
            }
            return -1;
        }

        /**
         * @brief The renderers CNA will configure for @p os, lower case.
         *
         * Filtered rather than listed in full. Offering Direct3D on a Linux target and then
         * failing validation would be the tool asking the user to discover a rule it already
         * knows -- and CNA makes that combination a hard configure error, so the discovery would
         * cost a full configure to make.
         */
        std::vector<std::string> renderersFor(StudioTargetOs os, std::string_view current)
        {
            std::vector<std::string> names;
            for (const RendererInfo& renderer : getKnownRenderers())
            {
                if (isRendererAvailableOn(renderer.commandLineName, os))
                {
                    names.emplace_back(renderer.commandLineName);
                }
            }

            // The profile's own renderer is always in the list, even when this system cannot build
            // it -- a project written for Windows and opened with the system switched to Linux
            // would otherwise show a blank control, which reads as "Studio lost your setting"
            // rather than as "this combination does not exist". Validation is what says the
            // second thing, and it can only say it about a value the user can still see.
            if (!current.empty()
                && std::find(names.begin(), names.end(), current) == names.end())
            {
                names.emplace_back(current);
            }
            return names;
        }

        /** @brief The platform implementations CNA actually has, lower case. */
        std::vector<std::string> implementedPlatforms(std::string_view current)
        {
            std::vector<std::string> names;
            for (const PlatformInfo& platform : getKnownPlatforms())
            {
                // A reserved name is one CNA's build recognises and refuses. Listing it would be
                // offering a choice whose only outcome is a configure error.
                if (platform.status == PlatformStatus::Implemented)
                {
                    names.emplace_back(platform.commandLineName);
                }
            }
            if (!current.empty()
                && std::find(names.begin(), names.end(), current) == names.end())
            {
                names.emplace_back(current);
            }
            return names;
        }

        /** @brief The colour a validation problem is said in. */
        StudioColorRole severityColor(StudioProfileSeverity severity)
        {
            switch (severity)
            {
                case StudioProfileSeverity::Error:     return StudioColorRole::Error;
                case StudioProfileSeverity::Warning:   return StudioColorRole::Warning;
                case StudioProfileSeverity::Migration: return StudioColorRole::TextSecondary;
            }
            return StudioColorRole::TextSecondary;
        }
    }

    StudioBuildPanel::StudioBuildPanel(StudioContext& context, BuildProcess& build)
        : context_(context), build_(build)
    {
    }

    BuildRequest StudioBuildPanel::makeRequest() const
    {
        BuildRequest request = makeBuildRequestFromActiveProfile(context_.getProject());
        request.cmakePath = cmakePath_;
        return request;
    }

    UiRect StudioBuildPanel::labelledRow(StudioFrame& frame, UiRect& cursor,
                                         std::string_view label)
    {
        const StudioTheme& theme = frame.theme();
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);

        UiRect row = cursor.splitTop(rowHeight);
        cursor.splitTop(spacing);

        // A fixed label column rather than one measured from the longest label: measuring would
        // make the controls jump sideways as the profile changes which words appear.
        const float labelWidth = std::min(row.width * 0.4f,
                                          metricOf(theme, StudioMetric::PanelHeaderHeight) * 5.0f);
        const UiRect labelBox = row.splitLeft(labelWidth);
        if (frame.isDrawPass())
        {
            studioDrawText(frame, labelBox, label, StudioFontRole::Body,
                           theme.color(StudioColorRole::TextSecondary));
        }
        row.splitLeft(spacing);

        // Capped rather than filling the row. A drop-down stretched across a wide panel puts its
        // arrow half a metre from its value, and the eye has to travel the whole way to read a
        // control holding one short word.
        return row.splitLeft(std::min(row.width, metricOf(theme, StudioMetric::PanelHeaderHeight)
                                                     * 9.0f));
    }

    StudioBuildPanelResult StudioBuildPanel::draw(StudioFrame& frame, const UiRect& body)
    {
        StudioBuildPanelResult result;

        const StudioTheme& theme = frame.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingMedium);
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float lineHeight = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                           + metricOf(theme, StudioMetric::SpacingXSmall));

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(body, theme.color(StudioColorRole::PanelBackground));
        }

        if (body.width <= 0.0f || body.height <= 0.0f) { return result; }

        if (!context_.hasProject())
        {
            if (frame.isDrawPass())
            {
                UiRect empty = body.inset(UiEdges{spacing, spacing});
                studioDrawText(frame, empty.splitTop(lineHeight),
                               "Open a project to build it.", StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
            return result;
        }

        // Scrolled, because the panel is taller than the strip at the bottom of a default layout
        // and the Build button is at the end of it. A panel whose primary action cannot be reached
        // is not a panel.
        //
        // The content height comes from the *previous* frame, held on the view's own state. The
        // rows are laid out as they are described, so the total is not known until the end -- and
        // a scroll view told the wrong height one frame late settles immediately and invisibly,
        // where measuring everything twice would cost a second layout of the whole panel.
        const WidgetId scrollId = frame.ids().make("build.scroll");
        StudioScrollOptions scrollOptions;
        scrollOptions.contentHeight = frame.state().get(scrollId).scalar;

        const StudioScrollResult scroll = studioBeginScroll(frame, scrollId, body, scrollOptions);

        UiRect content{scroll.viewport.left() + spacing,
                       scroll.viewport.top() - scroll.offsetY + spacing,
                       std::max(0.0f, scroll.viewport.width - spacing * 2.0f),
                       kUnboundedContentHeight};
        const float contentTop = content.top();

        if (!cmakeResolved_)
        {
            cmakePath_ = findCMake();
            cmakeResolved_ = true;
        }

        Project& project = context_.getProject();
        std::vector<StudioTargetProfile> profiles = project.getTargetProfiles();
        if (profiles.empty())
        {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, content.splitTop(lineHeight),
                               "This project declares no build target.", StudioFontRole::Body,
                               theme.color(StudioColorRole::Warning));
            }
            studioEndScroll(frame);
            return result;
        }

        frame.ids().push("build");

        // --- Which target ------------------------------------------------------------------------
        std::vector<std::string> profileNames;
        profileNames.reserve(profiles.size());
        for (const StudioTargetProfile& profile : profiles) { profileNames.push_back(profile.name); }

        int activeProfile = static_cast<int>(project.getActiveTargetProfileIndex());
        {
            const UiRect row = labelledRow(frame, content, "Target");
            const StudioDropdownResult chosen = studioDropdown(
                frame, frame.ids().make("profile"), row, profileNames, activeProfile);
            if (chosen.changed)
            {
                project.setActiveTargetProfileIndex(static_cast<std::size_t>(activeProfile));
                result.profileChanged = true;
            }
        }

        const auto activeIndex =
            std::min(static_cast<std::size_t>(std::max(activeProfile, 0)), profiles.size() - 1);
        StudioTargetProfile profile = profiles[activeIndex];
        bool edited = false;

        // --- The six axes ------------------------------------------------------------------------
        {
            std::vector<std::string> names;
            for (const StudioTargetOs os : knownOperatingSystems())
            {
                names.emplace_back(studioTargetOsDisplayName(os));
            }
            int index = indexOf(knownOperatingSystems(), profile.os);
            const UiRect row = labelledRow(frame, content, "Operating system");
            if (studioDropdown(frame, frame.ids().make("os"), row, names, index).changed
                && index >= 0)
            {
                profile.os = knownOperatingSystems()[static_cast<std::size_t>(index)];
                edited = true;
            }
        }
        {
            std::vector<std::string> names;
            for (const StudioArchitecture architecture : knownArchitectures())
            {
                names.emplace_back(studioArchitectureName(architecture));
            }
            int index = indexOf(knownArchitectures(), profile.architecture);
            const UiRect row = labelledRow(frame, content, "Architecture");
            if (studioDropdown(frame, frame.ids().make("arch"), row, names, index).changed
                && index >= 0)
            {
                profile.architecture = knownArchitectures()[static_cast<std::size_t>(index)];
                edited = true;
            }
        }
        {
            // Only the renderers that exist on the chosen system. Offering Direct3D on a Linux
            // target and then failing validation would be the tool asking the user to discover a
            // rule it already knows.
            const std::vector<std::string> renderers = renderersFor(profile.os, profile.renderer);
            int index = indexOfName(renderers, profile.renderer);
            const UiRect row = labelledRow(frame, content, "Renderer");
            if (studioDropdown(frame, frame.ids().make("renderer"), row, renderers, index).changed
                && index >= 0)
            {
                profile.renderer = renderers[static_cast<std::size_t>(index)];
                edited = true;
            }
        }
        {
            const std::vector<std::string> platforms = implementedPlatforms(profile.platform);
            int index = indexOfName(platforms, profile.platform);
            const UiRect row = labelledRow(frame, content, "Platform");
            if (studioDropdown(frame, frame.ids().make("platform"), row, platforms, index).changed
                && index >= 0)
            {
                profile.platform = platforms[static_cast<std::size_t>(index)];
                edited = true;
            }
        }
        {
            std::vector<std::string> names;
            for (const StudioBuildConfiguration configuration : knownConfigurations())
            {
                names.emplace_back(studioBuildConfigurationName(configuration));
            }
            int index = indexOf(knownConfigurations(), profile.configuration);
            const UiRect row = labelledRow(frame, content, "Configuration");
            if (studioDropdown(frame, frame.ids().make("configuration"), row, names, index).changed
                && index >= 0)
            {
                profile.configuration = knownConfigurations()[static_cast<std::size_t>(index)];
                edited = true;
            }
        }

        // --- Optional subsystems -----------------------------------------------------------------
        if (frame.isDrawPass())
        {
            studioDrawText(frame, content.splitTop(lineHeight), "Optional subsystems",
                           StudioFontRole::BodySmall,
                           theme.color(StudioColorRole::TextSecondary));
        }
        else
        {
            content.splitTop(lineHeight);
        }

        frame.ids().push("features");
        for (const StudioFeatureOption& feature : getKnownStudioFeatures())
        {
            const UiRect row = content.splitTop(rowHeight);
            content.splitTop(metricOf(theme, StudioMetric::SpacingXSmall));

            bool on = profile.hasFeature(feature.name);
            if (studioCheckbox(frame, frame.ids().make(feature.name), row, feature.displayName, on)
                    .activated)
            {
                profile.setFeature(feature.name, on);
                edited = true;
            }
        }
        frame.ids().pop();

        if (edited)
        {
            profiles[activeIndex] = profile;
            project.setTargetProfiles(std::move(profiles));
            result.profileChanged = true;
        }

        // --- What is wrong with it ---------------------------------------------------------------
        StudioTargetProfile checked = project.getActiveTargetProfile();
        const StudioProfileValidation validation = validateStudioTargetProfile(checked);
        for (const StudioProfileProblem& problem : validation.problems)
        {
            const UiRect row = content.splitTop(lineHeight);
            if (frame.isDrawPass())
            {
                studioDrawText(frame, row, problem.axis + ": " + problem.message,
                               StudioFontRole::BodySmall, theme.color(severityColor(problem.severity)));
            }
        }

        // --- What it would run -------------------------------------------------------------------
        BuildRequest request = makeRequest();
        const std::string problem = describeBuildProblem(request);

        if (!problem.empty())
        {
            // Said before the button rather than after a failure. A missing compiler otherwise
            // arrives as a wall of CMake output that says nothing a user can act on.
            if (frame.isDrawPass())
            {
                studioDrawText(frame, content.splitTop(lineHeight), "Cannot build: " + problem + ".",
                               StudioFontRole::Body, theme.color(StudioColorRole::Error));
            }
            else
            {
                content.splitTop(lineHeight);
            }
        }
        else
        {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, content.splitTop(lineHeight),
                               "Output: " + request.buildDirectory, StudioFontRole::BodySmall,
                               theme.color(StudioColorRole::TextSecondary));
            }
            else
            {
                content.splitTop(lineHeight);
            }

            for (const BuildStep& step : planBuild(request))
            {
                // The exact commands, because a real build has options the editor does not model
                // and somebody who needs one has to be able to take it away and run it by hand.
                const UiRect row = content.splitTop(lineHeight);
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, row,
                                   studioTruncateText(frame, theme.font(StudioFontRole::Monospace),
                                                      step.toCommandLine(), row.width),
                                   StudioFontRole::Monospace,
                                   theme.color(StudioColorRole::TextSecondary));
                }
            }
        }

        // --- The button --------------------------------------------------------------------------
        content.splitTop(metricOf(theme, StudioMetric::SpacingSmall));
        UiRect buttonRow = content.splitTop(rowHeight);
        content.splitTop(metricOf(theme, StudioMetric::SpacingSmall));

        const BuildState state = build_.getState();
        const float buttonWidth = std::max(
            studioLabelWidth(frame, "Cancel"), metricOf(theme, StudioMetric::PanelHeaderHeight) * 2.0f);
        const UiRect button = buttonRow.splitLeft(std::min(buttonWidth, buttonRow.width));
        buttonRow.splitLeft(metricOf(theme, StudioMetric::SpacingMedium));

        if (state == BuildState::Running)
        {
            StudioButtonOptions options;
            options.tooltip = "Stop the running build.";
            if (studioButton(frame, frame.ids().make("cancel"), button, "Cancel", options).activated)
            {
                result.cancelRequested = true;
            }
            if (frame.isDrawPass())
            {
                studioDrawText(frame, buttonRow,
                               "Step " + std::to_string(build_.getStepNumber()) + " of "
                                   + std::to_string(build_.getSteps().size()),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextPrimary));
            }
        }
        else
        {
            StudioButtonOptions options;
            options.kind = StudioButtonKind::Accent;
            options.enabled = problem.empty() && validation.isBuildable();
            options.tooltip = "Build the project with its own CMake.";
            if (studioButton(frame, frame.ids().make("build"), button, "Build", options).activated)
            {
                result.buildRequested = true;
            }
            if (state != BuildState::Idle && frame.isDrawPass())
            {
                studioDrawText(frame, buttonRow, std::string{"Last build "} + toString(state),
                               StudioFontRole::Body,
                               theme.color(state == BuildState::Failed ? StudioColorRole::Error
                                                                       : StudioColorRole::Success));
            }
        }

        // --- The log tail ------------------------------------------------------------------------
        if (!build_.getLogPath().empty())
        {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, content.splitTop(lineHeight), "Log: " + build_.getLogPath(),
                               StudioFontRole::BodySmall,
                               theme.color(StudioColorRole::TextSecondary));
            }
            else
            {
                content.splitTop(lineHeight);
            }

            // A tail, not the whole file: a failing build can produce megabytes, and the last
            // dozen lines are where the error is.
            for (const std::string& line : build_.readLogTail(kLogTailLines))
            {
                const UiRect row = content.splitTop(lineHeight);
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, row,
                                   studioTruncateText(frame, theme.font(StudioFontRole::Monospace), line,
                                                      row.width),
                                   StudioFontRole::Monospace,
                                   theme.color(StudioColorRole::TextSecondary));
                }
            }
        }

        frame.ids().pop();

        result.contentHeight = content.top() - contentTop + spacing;
        // Recorded in the input pass only, so the two passes of one frame lay out identically.
        // Writing it in the draw pass too would move every control between being hit-tested and
        // being drawn, on any frame where the content changed height.
        if (frame.isInputPass()) { frame.state().get(scrollId).scalar = result.contentHeight; }

        studioEndScroll(frame);
        return result;
    }
}
