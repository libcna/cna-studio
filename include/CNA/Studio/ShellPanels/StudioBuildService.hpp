// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/ShellPanels/StudioBuildService.hpp
 * @brief Building the user's game, and packaging it as a project that does not know Studio exists.
 *
 * `plan.md` STUDIO-02050, STUDIO-02055. The second service extracted from `StudioShellPanels`; the
 * reasoning that decides what earns its own type is on `StudioPlayService`.
 *
 * ### Why building and packaging are one service and not two
 *
 * They are the same question asked twice — *turn this project into something that runs elsewhere* —
 * and they share the invariant this whole product is held to: **CNA Studio produces CNA games, not
 * CNA Studio games.** A build runs the project's own CMake; a package writes a directory that
 * builds with CMake and a CNA checkout and nothing else. Splitting them would put that one rule in
 * two places, and a rule in two places is a rule that gets weakened in one of them.
 *
 * What they do *not* share with play is a process the user is looking at. A build is minutes long
 * and finishes while the user is reading something else, which is why both outcomes arrive as
 * notifications rather than as a panel the user has to have been watching.
 */

#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioNotifications.hpp"

#include <functional>
#include <utility>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief Drives the project's own build, and writes its standalone package. */
    class StudioBuildService
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param context The open project.
         * @param log Where packaging warnings are recorded.
         * @param notify Raised when a build finishes and when a package is written or refused.
         */
        StudioBuildService(StudioContext& context, StudioLog& log,
                           std::function<void(StudioNotification)> notify)
            : context_(context), log_(log), notify_(std::move(notify))
        {
        }

        StudioBuildService(const StudioBuildService&) = delete;
        StudioBuildService& operator=(const StudioBuildService&) = delete;

        /** @brief The build process itself, which the Build panel drives and reports on. */
        [[nodiscard]] BuildProcess& process() { return build_; }
        /** @brief The build process itself. */
        [[nodiscard]] const BuildProcess& process() const { return build_; }

        /**
         * @brief Notices a build finishing, exactly once.
         *
         * On the transition rather than on the state: a build that has failed stays failed until
         * the next one starts, so a notification raised from the state would be raised again every
         * frame for ever.
         *
         * @return Whether a notification was raised.
         */
        bool poll();

        /**
         * @brief Writes the open project as a standalone CNA game and reports where.
         *
         * @return Whether the package was written.
         */
        bool packageProject();

    private:
        StudioContext& context_;
        StudioLog& log_;
        std::function<void(StudioNotification)> notify_;

        BuildProcess build_;

        /** @brief The build's state last poll, so a finish is noticed as a transition. */
        BuildState was_ = BuildState::Idle;
    };
} // namespace CNA::Studio
