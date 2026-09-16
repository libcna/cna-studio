// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/ShellPanels/StudioComparisonService.hpp
 * @brief Running one scene on every renderer this Studio can run, and saying where they disagree.
 *
 * `plan.md` STUDIO-02050, STUDIO-02056. The third service extracted from `StudioShellPanels`; the
 * rule that decides what earns its own type is on `StudioPlayService`.
 *
 * ### Why this is not part of the play service
 *
 * Both launch players, and that is the whole of the resemblance. Play starts one game because a
 * user wants to play it, and the user is looking at it. A comparison starts several, in sequence,
 * over something closer to half an hour, to answer a question about *renderers* — and it answers
 * it with images rather than with a window. It has its own state machine, its own report, and
 * failure modes play does not have: captures it cannot read back, an output directory it cannot
 * write, renderers that finished and drew different pictures. Folding the two together would make
 * one type "the things that start processes", which is a category rather than a responsibility.
 *
 * ### Why the builds arrive as a provider rather than as a `StudioPlayService&`
 *
 * What a comparison needs from play is one thing: the list of renderers this Studio has a player
 * for, which must be the same list Play chooses from or the panel would offer a renderer Play will
 * not use. Taking the play service itself would hand this type the player process, the session
 * override and the input bridge as well — none of which it has any business with, and all of which
 * would then be reachable from a comparison by anybody who noticed they were there. A provider
 * names the one dependency.
 *
 * `STUDIO-02059` would not catch that, because a constructor argument is a legal dependency
 * however large it is. The guard stops a service reaching one it was *not* given; keeping the
 * given ones narrow is the part that stays a judgement.
 */

#include "CNA/Studio/RuntimeBridge/BackendComparison.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioNotifications.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief Runs the open scene on every installed player and reports where they differ. */
    class StudioComparisonService
    {
    public:
        /** @brief Where the renderers to compare come from: the same list Play chooses from. */
        using BuildsProvider = std::function<const std::vector<PlayerBuild>&()>;

        /**
         * @brief Creates the service.
         *
         * @param context The open project and scene. A comparison saves through it before starting,
         *        because each player is a separate process and reads the scene from disk.
         * @param log Where every start, refusal and cancellation is reported.
         * @param notify Raised when a run finishes, fails, or finds renderers that disagree — the
         *        one outcome a user must see with the Backends panel docked away. A sink rather
         *        than a shell so that a test can read what was raised.
         * @param builds The installed players. A provider rather than a copy: the list changes
         *        when Studio rescans, and a snapshot taken at construction would go stale.
         * @param readImage Decodes a capture. Unset means this build cannot read one back, which
         *        the panel reports rather than running a comparison that compares nothing.
         * @param writeImage Writes the difference images. Unset writes none.
         */
        StudioComparisonService(StudioContext& context, StudioLog& log,
                                std::function<void(StudioNotification)> notify,
                                BuildsProvider builds,
                                ImageReader readImage = {}, ImageWriter writeImage = {})
            : context_(context), log_(log), notify_(std::move(notify)), builds_(std::move(builds)),
              readImage_(std::move(readImage)), writeImage_(std::move(writeImage))
        {
        }

        StudioComparisonService(const StudioComparisonService&) = delete;
        StudioComparisonService& operator=(const StudioComparisonService&) = delete;

        /** @brief The run itself, which the Backends panel reports on. */
        [[nodiscard]] BackendComparison& run() { return comparison_; }
        /** @brief The run itself. */
        [[nodiscard]] const BackendComparison& run() const { return comparison_; }

        /** @brief How different two pixels may be before the images are called different. */
        [[nodiscard]] int tolerance() const { return tolerance_; }
        /** @brief Sets the tolerance the next run uses. */
        void setTolerance(int tolerance) { tolerance_ = tolerance; }

        /** @brief Whether a run is launching players or collecting their captures. */
        [[nodiscard]] bool isRunning() const
        {
            const ComparisonState state = comparison_.getState();
            return state == ComparisonState::Launching || state == ComparisonState::Capturing;
        }

        /**
         * @brief What this Studio would compare, from the project and the discovered builds.
         *
         * Public because the panel needs it twice: to say where the captures will go, and to ask
         * `describeComparisonProblem` why the button is unavailable *before* it is pressed. A
         * refusal a user can only discover by pressing the button is a refusal they experience as
         * a bug.
         */
        [[nodiscard]] ComparisonRequest makeRequest() const;

        /**
         * @brief Saves the scene if it has to, then runs it on every installed player.
         *
         * @return Whether a run started. A refusal is logged with the reason.
         */
        bool start();

        /** @brief Abandons a run in progress. */
        void cancel();

        /**
         * @brief Pumps a run in progress and announces one that has just ended.
         *
         * On the transition rather than on the state, for the reason `StudioBuildService::poll`
         * gives: a finished run stays finished, so a notification raised from the state would be
         * raised again every frame for ever.
         *
         * @param nowSeconds The host clock.
         * @return Whether a notification was raised.
         */
        bool poll(double nowSeconds);

    private:
        StudioContext& context_;
        StudioLog& log_;
        std::function<void(StudioNotification)> notify_;
        BuildsProvider builds_;
        ImageReader readImage_;
        ImageWriter writeImage_;

        BackendComparison comparison_;
        int tolerance_ = kDefaultImageTolerance;
    };
} // namespace CNA::Studio
