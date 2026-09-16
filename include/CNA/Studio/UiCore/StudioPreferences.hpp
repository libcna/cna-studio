// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioPreferences.hpp
 * @brief What a *user* has decided, as opposed to what a project is.
 *
 * `plan.md` STUDIO-06009, STUDIO-06010.
 *
 * ### The separation is the whole point
 *
 * A project's settings travel with the project: which renderers it ships on, where its scenes live,
 * what it is called. A user's preferences travel with the *person*: which theme they can read,
 * how fast the camera moves under their hand, where their compiler is. Putting either in the other's
 * file is a bug with a long tail — a theme committed to version control makes every teammate's
 * Studio dark, and a build directory kept per-user makes a project build differently for each of
 * them.
 *
 * So this file lives beside the workspace layout, in the user's configuration directory, and the
 * project file knows nothing about it.
 *
 * ### Losing preferences is never losing work
 *
 * Every value here has a default that is usable, so the worst outcome of an unreadable file is a
 * Studio that looks like a fresh install — not one that will not start. Nothing throws and nothing
 * is silent: a user whose theme reverted deserves to know why.
 */

#pragma once

#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/UiCore/StudioActionRegistry.hpp"

#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief How the viewport's middle-drag and wheel behave. */
    enum class StudioNavigationStyle : std::uint8_t
    {
        /** @brief Studio's own: middle-drag pans, wheel zooms at the pointer. */
        Studio,
        /** @brief Maya-style: Alt with left, middle and right. */
        Maya,
        /** @brief Blender-style: middle orbits, Shift-middle pans. */
        Blender
    };

    /** @brief Returns a stable name for @p style, for the file and for tests. */
    [[nodiscard]] std::string_view studioNavigationStyleName(StudioNavigationStyle style);

    /** @brief Parses a name from @ref studioNavigationStyleName; false for an unknown one. */
    [[nodiscard]] bool parseStudioNavigationStyle(std::string_view name,
                                                  StudioNavigationStyle& out);

    /** @brief One command the user has rebound. */
    struct StudioShortcutOverride
    {
        /** @brief The command's id. */
        std::string actionId;

        /** @brief What it is bound to now. A cleared chord means "unbound deliberately". */
        StudioShortcut shortcut;
    };

    /**
     * @brief Everything a user has decided about Studio itself.
     *
     * A plain struct with usable defaults, so a build that cannot read the file still runs and a
     * test can construct exactly the preferences it is about.
     */
    struct StudioPreferences
    {
        // --- Appearance ------------------------------------------------------------------------

        /** @brief `"dark"` or `"light"`. A name rather than a theme, so the file survives a retheme. */
        std::string theme = "dark";

        /**
         * @brief UI scale, as a multiplier.
         *
         * Separate from the display's own DPI scale, which the platform reports: this is the user
         * saying "everything is too small on this monitor", which no amount of correct DPI handling
         * can answer for them.
         */
        float uiScale = 1.0f;

        /** @brief Body text size in points, before @ref uiScale. */
        float fontSizePoints = 13.0f;

        // --- Viewport --------------------------------------------------------------------------

        /** @brief Which navigation scheme the viewport follows. */
        StudioNavigationStyle navigation = StudioNavigationStyle::Studio;

        /** @brief Multiplier on camera pan and orbit speed. */
        float cameraSpeed = 1.0f;

        /** @brief True to invert the wheel's zoom direction. */
        bool invertZoom = false;

        /**
         * @brief True to draw the 3D grid on the ground plane rather than the scene's own.
         *
         * `STUDIO-07056`. A boolean rather than the `GridPlane` enum the wireframe takes, because
         * `cna-studio-ui-core` does not link `cna-studio-scene` and giving preferences a reason to
         * would invert the layering for one field. The mapping happens where the wireframe is
         * built, which is where both halves are already in scope.
         *
         * The default is the scene's own plane, because everything this editor can place today
         * lives in XY — a floor is what a user needs the moment a model stands on one, and not
         * before.
         */
        bool gridOnGroundPlane = false;

        // --- Documents -------------------------------------------------------------------------

        /** @brief Seconds between autosaves, or zero for none. */
        int autosaveSeconds = 300;

        /** @brief True to restore the last project on start-up. */
        bool reopenLastProject = true;

        // --- Tools -----------------------------------------------------------------------------

        /** @brief The editor to open a source file in. Empty means the system default. */
        std::string externalEditor;

        /** @brief An explicit CMake to build with. Empty means whatever is on the PATH. */
        std::string cmakePath;

        /** @brief Parallel build jobs, or zero for one per core. */
        int buildJobs = 0;

        // --- Workspace -------------------------------------------------------------------------

        /** @brief The saved layout to open with, or empty for whatever was last arranged. */
        std::string defaultLayout;

        /** @brief Commands the user has rebound, in the order they were read. */
        std::vector<StudioShortcutOverride> shortcuts;

        /** @brief Applies @ref shortcuts to @p registry, reporting how many took effect. */
        std::size_t applyShortcuts(StudioActionRegistry& registry) const;
    };

    /** @brief Equality, for a test asserting a round trip rather than field by field. */
    [[nodiscard]] bool operator==(const StudioPreferences& lhs, const StudioPreferences& rhs);

    /**
     * @brief Returns @p preferences with every value forced into the range it is usable in.
     *
     * Applied on the way in as well as on the way out: this is a plain JSON file in the user's
     * configuration directory, and a UI scale of zero read from a hand-edited one is a Studio with
     * no pixels rather than a Studio that says no.
     *
     * @param preferences What was read or set.
     * @return The usable version.
     */
    [[nodiscard]] StudioPreferences studioClampPreferences(StudioPreferences preferences);

    /** @brief What reading the preferences produced. */
    struct StudioPreferencesDocument
    {
        /** @brief The preferences, already clamped. Defaults when nothing usable was found. */
        StudioPreferences preferences;

        /** @brief Whether a usable document was found and read. */
        bool found = false;

        /**
         * @brief Why the stored preferences were not usable, when they were not.
         *
         * Empty on a clean read *and* on a first run: not finding a file the user has never saved
         * is not a problem to report, and reporting it would train them to ignore the channel that
         * reports the real ones.
         */
        std::string problem;
    };

    /**
     * @brief Reads and writes one preferences file.
     *
     * Constructed with a path rather than finding its own, so a test can point it at a temporary
     * directory and the application at the user's. @ref defaultPath is the convention.
     */
    class StudioPreferencesStore
    {
    public:
        /** @brief The file name, under the user's Studio configuration directory. */
        static constexpr const char* kFileName = "preferences.json";

        /**
         * @brief The version of the enclosing document.
         *
         * A *missing* field is the migration mechanism: every value has a usable default, so a file
         * written by an older Studio reads correctly by leaving the new fields alone. The version
         * exists to refuse a file from a *newer* one, where a missing field may mean something.
         */
        static constexpr int kFileVersion = 1;

        /** @brief Where preferences live for this user, or empty with nowhere to write. */
        [[nodiscard]] static std::string defaultPath();

        /** @brief Constructs a store over @p path. */
        explicit StudioPreferencesStore(std::string path) : path_(std::move(path)) {}

        /** @brief The file this store reads and writes. */
        [[nodiscard]] const std::string& getPath() const { return path_; }

        /**
         * @brief Writes @p preferences, replacing whatever was there.
         * @param preferences What to store.
         * @param outProblem Receives the reason on failure.
         * @return Whether they were stored.
         */
        [[nodiscard]] bool save(const StudioPreferences& preferences,
                                std::string* outProblem = nullptr) const;

        /** @brief Reads the stored preferences. */
        [[nodiscard]] StudioPreferencesDocument load() const;

        /** @brief Removes the file, so the next start uses the defaults. */
        bool forget() const;

    private:
        std::string path_;
    };

    /** @brief Serializes @p preferences. */
    [[nodiscard]] JsonValue studioPreferencesToJson(const StudioPreferences& preferences);

    /**
     * @brief Reads preferences from @p value, leaving anything absent at its default.
     * @param value The document.
     * @return The preferences, already clamped.
     */
    [[nodiscard]] StudioPreferences studioPreferencesFromJson(const JsonValue& value);
}
