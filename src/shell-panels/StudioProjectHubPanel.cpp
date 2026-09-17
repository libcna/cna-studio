// SPDX-License-Identifier: MS-PL
/**
 * @file StudioProjectHubPanel.cpp
 * @brief The Project Hub, drawn.
 *
 * `plan.md` STUDIO-08001 … STUDIO-08004.
 */

#include "CNA/Studio/ShellPanels/StudioProjectHubPanel.hpp"

#include <algorithm>
#include <filesystem>

#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

namespace CNA::Studio
{
    namespace
    {
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }
    }

    std::string studioProjectHubTargetDirectory(const StudioProjectHubState& state)
    {
        const std::string directoryName = studioProjectDirectoryName(state.name);
        if (state.location.empty() || directoryName.empty()) { return {}; }

        return (std::filesystem::path{state.location} / directoryName).generic_string();
    }

    StudioNewProjectRequest studioProjectHubRequest(const StudioProjectHubState& state,
                                                    const StudioLanguageRegistry& languages)
    {
        StudioNewProjectRequest request;
        request.name = state.name;
        request.directory = studioProjectHubTargetDirectory(state);
        request.templateId = state.templateId;
        request.languageId =
            state.languageId.empty() ? languages.defaultLanguageId() : state.languageId;
        return request;
    }

    StudioProjectHubResult studioProjectHubPanel(StudioFrame& frame, const UiRect& body,
                                                 const StudioProjectHubModel& model,
                                                 StudioProjectHubState& state)
    {
        StudioProjectHubResult result;
        if (body.isEmpty()) { return result; }

        const StudioTheme& theme = frame.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingMedium);
        const float small = metricOf(theme, StudioMetric::SpacingSmall);
        const float rowHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float lineHeight = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                           + metricOf(theme, StudioMetric::SpacingXSmall));

        if (frame.isDrawPass())
        {
            frame.drawList().fillRect(body, theme.color(StudioColorRole::PanelBackground));
        }

        UiRect content = body.inset(UiEdges{spacing, spacing});

        // --- The two pages, as a strip of tabs --------------------------------------------------
        UiRect tabs = content.splitTop(rowHeight);
        content.splitTop(spacing);
        {
            const float tabWidth = std::min(tabs.width * 0.5f, rowHeight * 6.0f);

            UiRect recentTab = tabs.splitLeft(tabWidth);
            tabs.splitLeft(small);
            UiRect newTab = tabs.splitLeft(tabWidth);

            StudioTabOptions options;
            options.active = state.page == StudioProjectHubPage::Recent;
            if (studioTab(frame, frame.ids().make("hub.tab.recent"), recentTab, "Recent",
                          options).activated)
            {
                state.page = StudioProjectHubPage::Recent;
            }

            options.active = state.page == StudioProjectHubPage::New;
            if (studioTab(frame, frame.ids().make("hub.tab.new"), newTab, "New Project",
                          options).activated)
            {
                state.page = StudioProjectHubPage::New;
            }
        }

        if (state.page == StudioProjectHubPage::Recent)
        {
            // --- Open a project that is not on the list -----------------------------------------
            {
                UiRect row = content.splitTop(rowHeight);
                content.splitTop(small);

                UiRect openButton = row.splitRight(std::min(row.width * 0.25f, rowHeight * 5.0f));
                row.splitRight(small);

                StudioTextFieldOptions options;
                options.placeholder = "Path to a .cnaproject";
                const StudioTextFieldResult typed =
                    studioTextField(frame, frame.ids().make("hub.open.path"), row, state.openPath,
                                    options);

                StudioButtonOptions openOptions;
                openOptions.enabled = !state.openPath.empty();

                // Enter in the field opens it too. A path field whose only way forward is the
                // mouse is one that interrupts somebody who has just finished typing.
                if (studioButton(frame, frame.ids().make("hub.open.go"), openButton, "Open",
                                 openOptions).activated
                    || (typed.committed && !state.openPath.empty()))
                {
                    state.openProblem = describeStudioProjectAvailability(state.openPath);
                    if (state.openProblem.empty()) { result.openProjectPath = state.openPath; }
                }

                if (frame.isDrawPass() && !state.openProblem.empty())
                {
                    studioDrawText(frame, content.splitTop(lineHeight), state.openProblem,
                                   StudioFontRole::BodySmall, theme.color(StudioColorRole::Error));
                }
                else if (!frame.isDrawPass() && !state.openProblem.empty())
                {
                    content.splitTop(lineHeight);
                }
            }

            // --- Recent projects ----------------------------------------------------------------
            if (model.recent.empty())
            {
                if (frame.isDrawPass())
                {
                    studioDrawText(frame, content.splitTop(lineHeight),
                                   "No projects yet. New Project makes one.", StudioFontRole::Body,
                                   theme.color(StudioColorRole::TextSecondary));
                }
                return result;
            }

            const float entryHeight = rowHeight * 2.0f + small;
            const WidgetId scrollId = frame.ids().make("hub.recent.scroll");

            StudioScrollOptions scrollOptions;
            scrollOptions.contentHeight =
                static_cast<float>(model.recent.size()) * (entryHeight + small);

            const StudioScrollResult scroll = studioBeginScroll(frame, scrollId, content,
                                                               scrollOptions);

            std::size_t first = 0;
            std::size_t last = 0;
            scroll.visibleRows(entryHeight + small, model.recent.size(), first, last);

            for (std::size_t index = first; index < last; ++index)
            {
                const StudioRecentProject& recent = model.recent[index];

                const UiRect entry{scroll.viewport.x,
                                   scroll.viewport.top() - scroll.offsetY
                                       + static_cast<float>(index) * (entryHeight + small),
                                   scroll.viewport.width, entryHeight};

                ++result.recentRowsDrawn;

                frame.ids().push("recent");
                frame.ids().push(std::to_string(index));

                UiRect row = entry;
                UiRect actions = row.splitRight(rowHeight * 5.0f);

                // An unavailable row is drawn and is *not* clickable. Dropping it would make a
                // project on an unmounted drive disappear, which loses the one place there is to
                // say why it is not there (STUDIO-08002).
                StudioButtonOptions open;
                open.enabled = recent.available;
                open.tooltip = recent.available ? std::string{} : recent.problem;

                if (studioButton(frame, frame.ids().make("open"), row,
                                 recent.name.empty()
                                     ? std::filesystem::path{recent.path}.stem().generic_string()
                                     : recent.name,
                                 open).activated)
                {
                    result.openProjectPath = recent.path;
                }

                if (frame.isDrawPass())
                {
                    // The directory under the name, muted, and the reason in its place when there
                    // is one: a row that says only "unavailable" sends somebody looking.
                    const UiRect detail{row.x + small, row.top() + rowHeight, row.width - small * 2.0f,
                                        rowHeight};
                    studioDrawText(frame, detail,
                                   studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                      recent.available ? recent.directory()
                                                                       : recent.problem,
                                                      detail.width),
                                   StudioFontRole::BodySmall,
                                   theme.color(recent.available ? StudioColorRole::TextSecondary
                                                                : StudioColorRole::Warning));
                }

                UiRect forget = actions.splitRight(rowHeight * 4.0f);
                if (studioButton(frame, frame.ids().make("forget"), forget.splitTop(rowHeight),
                                 "Remove").activated)
                {
                    // Offered because the one thing to do about a row pointing at a project that is
                    // gone for good is to take it off the list.
                    result.forgetProjectPath = recent.path;
                }

                frame.ids().pop();
                frame.ids().pop();
            }

            studioEndScroll(frame);
            return result;
        }

        // --- New Project ----------------------------------------------------------------------
        const auto labelled = [&](std::string_view label, UiRect& cursor) {
            UiRect row = cursor.splitTop(rowHeight);
            cursor.splitTop(small);

            const UiRect labelBox = row.splitLeft(std::min(row.width * 0.3f, rowHeight * 5.0f));
            if (frame.isDrawPass())
            {
                studioDrawText(frame, labelBox, label, StudioFontRole::Body,
                               theme.color(StudioColorRole::TextSecondary));
            }
            row.splitLeft(small);
            return row;
        };

        {
            const UiRect nameRow = labelled("Name", content);
            StudioTextFieldOptions options;
            options.placeholder = "My Game";
            (void)studioTextField(frame, frame.ids().make("hub.new.name"), nameRow, state.name,
                                  options);
        }
        {
            const UiRect locationRow = labelled("Location", content);
            StudioTextFieldOptions options;
            options.placeholder = "A directory to create the project in";
            (void)studioTextField(frame, frame.ids().make("hub.new.location"), locationRow,
                                  state.location, options);
        }

        // The language chooser, filled from the registry. It holds one entry today, and the point
        // of drawing it from the registry rather than from a list is that it will hold two without
        // this file changing (STUDIO-02080).
        if (model.languages != nullptr && !model.languages->empty())
        {
            std::vector<std::string> names;
            std::vector<std::string> ids;
            for (const std::shared_ptr<const StudioLanguageAdapter>& adapter :
                 model.languages->all())
            {
                names.push_back(adapter->descriptor().displayName);
                ids.push_back(adapter->descriptor().id);
            }

            const std::string current =
                state.languageId.empty() ? model.languages->defaultLanguageId() : state.languageId;
            int selected = 0;
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                if (ids[index] == current) { selected = static_cast<int>(index); }
            }

            const UiRect languageRow = labelled("Language", content);
            if (studioDropdown(frame, frame.ids().make("hub.new.language"), languageRow, names,
                               selected).changed
                && selected >= 0 && static_cast<std::size_t>(selected) < ids.size())
            {
                state.languageId = ids[static_cast<std::size_t>(selected)];

                // A template the new language cannot host stops being the selection rather than
                // becoming a refusal the user has to read to understand.
                const StudioProjectTemplate* chosen =
                    model.templates != nullptr ? model.templates->find(state.templateId) : nullptr;
                if (chosen != nullptr && !chosen->supportsLanguage(state.languageId))
                {
                    state.templateId.clear();
                }
            }
        }

        // --- Where it goes, shown before it is committed to ---------------------------------------
        if (frame.isDrawPass())
        {
            const std::string target = studioProjectHubTargetDirectory(state);
            studioDrawText(frame, content.splitTop(lineHeight),
                           target.empty() ? std::string{"Fill in a name and a location."}
                                          : "Creates " + target,
                           StudioFontRole::BodySmall,
                           theme.color(StudioColorRole::TextSecondary));
        }
        else
        {
            content.splitTop(lineHeight);
        }
        content.splitTop(small);

        // --- The templates -----------------------------------------------------------------------
        const std::string languageId =
            !state.languageId.empty() ? state.languageId
                                      : (model.languages != nullptr
                                             ? model.languages->defaultLanguageId()
                                             : std::string{});

        std::vector<const StudioProjectTemplate*> offered;
        if (model.templates != nullptr) { offered = model.templates->forLanguage(languageId); }

        // The button first, measured off the bottom, so that the list above it takes what is left.
        // A Hub whose Create button is below the fold is a Hub whose primary action cannot be
        // reached, which is the failure the Build panel's scroll view was written against.
        UiRect footer = content.splitBottom(rowHeight);
        content.splitBottom(small);

        if (!state.problems.empty())
        {
            UiRect problems = content.splitBottom(lineHeight
                                                  * static_cast<float>(state.problems.size()));
            content.splitBottom(small);

            if (frame.isDrawPass())
            {
                for (const StudioNewProjectProblem& problem : state.problems)
                {
                    studioDrawText(frame, problems.splitTop(lineHeight), problem.message,
                                   StudioFontRole::BodySmall,
                                   theme.color(StudioColorRole::Error));
                }
            }
        }

        if (offered.empty())
        {
            if (frame.isDrawPass())
            {
                studioDrawText(frame, content.splitTop(lineHeight),
                               "No templates were found. Studio looks beside its executable and in "
                               "its own source tree.",
                               StudioFontRole::Body, theme.color(StudioColorRole::Warning));
            }
        }
        else
        {
            const float entryHeight = rowHeight * 2.0f;
            const WidgetId scrollId = frame.ids().make("hub.templates.scroll");

            StudioScrollOptions scrollOptions;
            scrollOptions.contentHeight =
                static_cast<float>(offered.size()) * (entryHeight + small);

            const StudioScrollResult scroll = studioBeginScroll(frame, scrollId, content,
                                                               scrollOptions);

            std::size_t first = 0;
            std::size_t last = 0;
            scroll.visibleRows(entryHeight + small, offered.size(), first, last);

            for (std::size_t index = first; index < last; ++index)
            {
                const StudioProjectTemplate& value = *offered[index];

                const UiRect entry{scroll.viewport.x,
                                   scroll.viewport.top() - scroll.offsetY
                                       + static_cast<float>(index) * (entryHeight + small),
                                   scroll.viewport.width, entryHeight};

                ++result.templateRowsDrawn;

                frame.ids().push("template");
                frame.ids().push(value.id);

                StudioButtonOptions options;
                options.selected = state.templateId == value.id;

                UiRect row = entry;
                if (studioButton(frame, frame.ids().make("choose"), row.splitTop(rowHeight),
                                 value.name, options).activated)
                {
                    state.templateId = value.id;

                    // Filling in a name nobody typed is worse than an empty field; suggesting one
                    // that is already correct is not. The template's own name is a bad project
                    // name, so this only moves the *selection* on.
                    state.problems.clear();
                }

                if (frame.isDrawPass())
                {
                    studioDrawText(frame, row,
                                   studioTruncateText(frame, theme.font(StudioFontRole::BodySmall),
                                                      value.description, row.width),
                                   StudioFontRole::BodySmall,
                                   theme.color(StudioColorRole::TextSecondary));
                }

                frame.ids().pop();
                frame.ids().pop();
            }

            studioEndScroll(frame);
        }

        UiRect createButton = footer.splitRight(std::min(footer.width, rowHeight * 6.0f));
        if (studioButton(frame, frame.ids().make("hub.new.create"), createButton, "Create Project")
                .activated)
        {
            result.createRequested = true;
        }
        return result;
    }
}
