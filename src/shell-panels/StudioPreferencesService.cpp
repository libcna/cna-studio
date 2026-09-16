// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPreferencesService.cpp
 * @brief Applying the user's preferences, and writing them down in that order.
 */

#include "CNA/Studio/ShellPanels/StudioPreferencesService.hpp"

#include <string>
#include <utility>

namespace CNA::Studio
{
    StudioTheme StudioPreferencesService::theme() const
    {
        // A name rather than a theme on disk, so the file survives a retheme and an unknown name
        // reads as the default rather than as a corrupt file.
        StudioTheme theme = preferences_.theme == "light" ? StudioTheme::light()
                                                          : StudioTheme::dark();
        theme.setScale(preferences_.uiScale);
        return theme;
    }

    bool StudioPreferencesService::apply()
    {
        // Applied before it is persisted, so a write that fails still leaves the user looking at
        // what they chose: they can see it worked and decide what to do about the file.
        if (applyTheme_) { applyTheme_(theme()); }

        if (!save_) { return false; }

        std::string problem;
        if (save_(preferences_, &problem)) { return true; }

        log_.append(LogSeverity::Warning, "Could not save preferences: " + problem);
        return false;
    }

    void StudioPreferencesService::reset()
    {
        preferences_ = StudioPreferences{};
        (void)apply();
    }
} // namespace CNA::Studio
