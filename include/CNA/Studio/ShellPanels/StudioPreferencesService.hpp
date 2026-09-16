// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/ShellPanels/StudioPreferencesService.hpp
 * @brief What the user decided Studio should be like, and getting it there and onto disk.
 *
 * `plan.md` STUDIO-02050, STUDIO-02057. The fourth service extracted from `StudioShellPanels`; the
 * rule that decides what earns its own type is on `StudioPlayService`.
 *
 * ### Why a handful of settings is a service
 *
 * Because of the third part of the rule — a failure mode of its own — and because that failure is
 * one Studio gets wrong by default. A preference is applied and *then* persisted, so that a write
 * which fails still leaves the user looking at what they chose: they can see that it worked, and
 * decide what to do about the file. The opposite order makes a full disk look like a control that
 * does nothing. That ordering is a rule, it has a failure with a message, and it is the kind of
 * rule that gets quietly reversed by somebody tidying up a function that does two things.
 *
 * ### The theme goes out through a sink, like every other service's outcome
 *
 * Applying preferences means giving the shell a theme, and taking a `StudioShell&` would put this
 * back where it started: untestable without one, and holding the whole shell in order to call one
 * method on it. It takes a sink instead, so a test reads the theme that was applied and the
 * headless preview passes nothing at all.
 *
 * ### What is deliberately *not* here
 *
 * Reading and writing the file. That is `StudioPreferencesStore`'s, it is CNA-free and already
 * tested on its own, and a service that also knew the file format would own two things that change
 * for different reasons. This one owns the *decision*; the store owns the bytes.
 */

#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioPreferences.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <functional>
#include <string>
#include <utility>

namespace CNA::Studio
{
    /** @brief Holds the user's preferences, applies them, and writes them down. */
    class StudioPreferencesService
    {
    public:
        /** @brief Where an applied theme goes. Unset means nothing is showing these preferences. */
        using ThemeSink = std::function<void(StudioTheme)>;

        /** @brief Writes the preferences down, reporting the reason when it cannot. */
        using SaveSink = std::function<bool(const StudioPreferences&, std::string*)>;

        /**
         * @brief Creates the service.
         *
         * @param log Where a failed write is reported. Silence would be the wrong answer: a user
         *        whose theme reverts on the next launch deserves to know why.
         * @param applyTheme Receives the theme these preferences describe.
         */
        StudioPreferencesService(StudioLog& log, ThemeSink applyTheme)
            : log_(log), applyTheme_(std::move(applyTheme))
        {
        }

        StudioPreferencesService(const StudioPreferencesService&) = delete;
        StudioPreferencesService& operator=(const StudioPreferencesService&) = delete;

        /** @brief The preferences themselves, which the Preferences panel edits in place. */
        [[nodiscard]] StudioPreferences& model() { return preferences_; }
        /** @brief The preferences themselves. */
        [[nodiscard]] const StudioPreferences& model() const { return preferences_; }

        /**
         * @brief Sets the seam through which changed preferences reach disk.
         *
         * A seam because writing them needs a home directory, which `cna-studio-shell-panels` has
         * no business resolving. Unset means they are not persisted, which is what a preview and a
         * test both want.
         */
        void setSaveSink(SaveSink save) { save_ = std::move(save); }

        /** @brief The theme these preferences describe. */
        [[nodiscard]] StudioTheme theme() const;

        /**
         * @brief Applies whatever @ref model now says, then persists it.
         *
         * @return Whether the preferences reached disk. False also means false when there is
         *         nowhere to write them, which is not a failure and is not logged.
         */
        bool apply();

        /**
         * @brief Puts every preference back the way Studio ships, and applies that.
         *
         * Separate from assigning a default-constructed model through @ref model so that the reset
         * cannot be done without the apply — a reset that changed the record and not the screen is
         * the one the user reports as "Reset did nothing".
         */
        void reset();

    private:
        StudioLog& log_;
        ThemeSink applyTheme_;
        SaveSink save_;

        StudioPreferences preferences_;
    };
} // namespace CNA::Studio
