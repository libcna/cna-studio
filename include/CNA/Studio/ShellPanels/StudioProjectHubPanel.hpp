// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/ShellPanels/StudioProjectHubPanel.hpp
 * @brief The Project Hub: recent projects, the templates on offer, and the New Project form.
 *
 * `plan.md` STUDIO-08001, STUDIO-08002, STUDIO-08003, STUDIO-08004.
 *
 * ### It knows nothing about how a project is built
 *
 * The Hub asks for a name, a place, a template and a language, and hands all four to
 * `createStudioProject`. Which files that writes is the template's answer and the language
 * adapter's, and `STUDIO-02085`'s guard refuses this file the vocabulary to have an opinion — it
 * cannot name a compiler, a build system or a build file, and the language chooser it draws is
 * filled from the registry rather than from a list.
 *
 * That is the whole point of doing the language seam before this tranche. A Hub written first
 * would have had `CMakeLists.txt` in it by the third row.
 *
 * ### The state is the user's half-finished form, and it is the panel's
 *
 * Which template is selected and what has been typed so far belong to a panel rather than to a
 * frame, and they have to outlive both description passes. `StudioProjectHubState` is what the
 * shell holds, so a test can drive the Hub the way the editor does and read out what it would
 * create without creating it.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "CNA/Studio/Project/ProjectCreation.hpp"
#include "CNA/Studio/Project/ProjectTemplate.hpp"
#include "CNA/Studio/Project/RecentProjects.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

namespace CNA::Studio
{
    class StudioLanguageRegistry;

    /** @brief Which half of the Hub is showing. */
    enum class StudioProjectHubPage
    {
        /** @brief Recent projects, and the way in to the other two. */
        Recent,

        /** @brief The New Project form. */
        New
    };

    /** @brief The Hub's retained state: the page, the selection and the half-typed form. */
    struct StudioProjectHubState
    {
        StudioProjectHubPage page = StudioProjectHubPage::Recent;

        /** @brief What the user has typed into the name field. */
        std::string name;

        /** @brief The directory the project goes *in*, which the name is appended to. */
        std::string location;

        /** @brief Id of the selected template, or empty. */
        std::string templateId;

        /** @brief Id of the selected language, or empty for this build's default. */
        std::string languageId;

        /** @brief Index of the selected recent project, or -1. */
        int selectedRecent = -1;

        /**
         * @brief A `.cnaproject` path typed on the Recent page, for one that is not on the list.
         *
         * There is no modal file dialog yet (`STUDIO-03022` covers the layering, not the window),
         * and Open Project cannot wait for one: a Hub that can only reopen what it has already
         * opened cannot open anything the first time. A path field is the plain answer, it is what
         * a terminal user would type anyway, and it does not become dead weight when a file dialog
         * arrives — a dialog fills this field in.
         */
        std::string openPath;

        /** @brief Why the last open attempt was refused, so it survives to the next frame. */
        std::string openProblem;

        /** @brief What the last attempt to create refused, so it survives to the next frame. */
        std::vector<StudioNewProjectProblem> problems;
    };

    /** @brief What the Hub drew, and what the user asked for. */
    struct StudioProjectHubResult
    {
        /** @brief How many recent rows were drawn. */
        std::size_t recentRowsDrawn = 0;

        /** @brief How many template rows were drawn. */
        std::size_t templateRowsDrawn = 0;

        /** @brief A project the user asked to open, as an absolute `.cnaproject` path. */
        std::string openProjectPath;

        /** @brief True when the user asked to create the project the form describes. */
        bool createRequested = false;

        /** @brief A recent row the user asked to take off the list. */
        std::string forgetProjectPath;
    };

    /** @brief Everything the Hub reads, gathered by whoever draws it. */
    struct StudioProjectHubModel
    {
        /** @brief The recent list, newest first, each row's availability already answered. */
        std::vector<StudioRecentProject> recent;

        /** @brief The templates on offer. */
        const StudioTemplateCatalogue* templates = nullptr;

        /** @brief The languages this build implements. */
        const StudioLanguageRegistry* languages = nullptr;
    };

    /**
     * @brief Returns the directory a project with @p state's name and location would be created in.
     *
     * Shown as the user types, so that where it goes is visible before they commit to it rather
     * than discovered afterwards. Empty when either half is missing.
     */
    [[nodiscard]] std::string studioProjectHubTargetDirectory(const StudioProjectHubState& state);

    /**
     * @brief Turns @p state into the request `createStudioProject` takes.
     *
     * Separate from drawing so a test can ask "what would this form create" without a frame, and
     * so the panel and the command line compose the same request from the same rule.
     */
    [[nodiscard]] StudioNewProjectRequest studioProjectHubRequest(
        const StudioProjectHubState& state, const StudioLanguageRegistry& languages);

    /**
     * @brief Describes the Project Hub into @p frame.
     *
     * @param frame The frame.
     * @param body Where the panel's content goes.
     * @param model What to show.
     * @param state The Hub's retained state; the form is written back into it.
     * @return What the user asked for.
     */
    StudioProjectHubResult studioProjectHubPanel(StudioFrame& frame, const UiRect& body,
                                                 const StudioProjectHubModel& model,
                                                 StudioProjectHubState& state);
}
