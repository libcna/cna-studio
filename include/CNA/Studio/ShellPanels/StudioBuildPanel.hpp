// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioBuildPanel.hpp
 * @brief The Build panel on the Studio UI.
 *
 * `plan.md` STUDIO-07010.
 *
 * ### It is not a straight port
 *
 * The ImGui Build panel offers two axes — a platform triple and a graphics backend — because that
 * is all the prototype's model had. Studio's model is the six-axis `StudioTargetProfile`
 * (`STUDIO-02040`): operating system, architecture, CNA platform, CNA renderer, configuration and
 * the optional subsystems, with validation that knows which combinations exist. That model has had
 * no user interface at all since it landed, and a project could only change its targets by editing
 * the `.cnaproject` by hand. This is that interface.
 *
 * ### A profile is a project's, not a panel's
 *
 * The legacy panel kept the chosen platform and backend in its own members, so they were forgotten
 * when the panel was closed and were never saved. Here the profile list *is* the project's, edited
 * in place, so what the Build button does and what the project ships are one thing rather than two
 * that agree until they do not.
 */

#pragma once

#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/Project/TargetProfile.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <string>
#include <vector>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What the Build panel did this frame. */
    struct StudioBuildPanelResult
    {
        /** @brief The user changed something about the target profile. Input pass only. */
        bool profileChanged = false;

        /** @brief The user asked for a build. Input pass only. */
        bool buildRequested = false;

        /** @brief The user asked to stop the running build. Input pass only. */
        bool cancelRequested = false;

        /** @brief How tall the content is, so the panel can be scrolled. */
        float contentHeight = 0.0f;
    };

    /**
     * @brief The Build panel: what this project ships, and the button that builds it.
     *
     * Holds no copy of the profile. Everything it shows is read from the project when it is drawn,
     * and everything it changes is written back to the project, so a profile edited here is the
     * profile the Play button uses and the profile the next save writes.
     */
    class StudioBuildPanel
    {
    public:
        /**
         * @brief Constructs the panel.
         * @param context The editor context, for the project and the log.
         * @param build The build process this panel starts and reports on.
         */
        StudioBuildPanel(StudioContext& context, BuildProcess& build);

        /**
         * @brief Describes the panel into a frame.
         * @param frame The frame.
         * @param body Where the panel's content goes.
         * @return What the user asked for.
         */
        StudioBuildPanelResult draw(StudioFrame& frame, const UiRect& body);

        /**
         * @brief The build this panel would start, from the project's active profile.
         *
         * Planned by the project's language adapter, not here. The panel shows the commands and
         * presses the button; which commands those are is the language's answer, and a panel that
         * knew it would be a panel that had to be edited for every language Studio grows.
         *
         * @return The planned job, or one with no steps when there is no adapter or nothing to run.
         */
        [[nodiscard]] StudioBuildJob planBuild() const;

        /** @brief How many lines of the build log the panel shows. */
        static constexpr std::size_t kLogTailLines = 12;

    private:
        /**
         * @brief Draws one labelled row and returns its content rectangle.
         *
         * Every row in the panel is "a label on the left, a control on the right", and having one
         * place decide the split is what keeps the controls in a column rather than stepping in
         * and out as the labels change length.
         */
        [[nodiscard]] static UiRect labelledRow(StudioFrame& frame, UiRect& cursor,
                                                std::string_view label);

        StudioContext& context_;
        BuildProcess& build_;

        /**
         * @brief Resolved once: probing a toolchain walks the PATH, which is not a per-frame cost.
         *
         * Held rather than asked for each frame, and the field is the adapter's *report* rather
         * than a path, so the reason a toolchain is unusable survives to the row that shows it.
         */
        StudioToolchainReport toolchain_;
        bool toolchainProbed_ = false;
    };
}
