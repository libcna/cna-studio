// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/BuildPanel.hpp
 * @brief Building the open project, and seeing why when it does not work.
 *
 * plan.md ED-308. The panel is deliberately unglamorous: a platform, a backend, a configuration,
 * the exact command lines that will run, a button, and the tail of the log. Showing the commands
 * matters -- a real build has options the editor does not model, and a user who needs one of them
 * has to be able to take the command away and run it themselves.
 */

#include <string>

#include "CNA/Editor/Panels/EditorPanel.hpp"
#include "CNA/Editor/Project/BuildRunner.hpp"

namespace CNA::Editor
{
    /**
     * @brief Drives a `BuildProcess` over the open project and reports what it is doing.
     *
     * Problems are reported *before* the build is offered rather than after it fails. A missing
     * compiler arrives as a wall of CMake output that says nothing a user can act on, and anyone
     * who installed an editor and not a toolchain is the common case rather than the odd one.
     */
    class BuildPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

        /** @brief Advances the running build. Called once per frame, never blocks. */
        void poll() { build_.poll(); }

    private:
        /** @brief Returns the request the current choices describe. */
        [[nodiscard]] BuildRequest makeRequest() const;

        BuildProcess build_;

        /**
         * @brief Where cmake was found, resolved once.
         *
         * Cached because the panel draws every frame and the lookup walks every directory on the
         * PATH. A toolchain that appears mid-session is rare enough to be worth a restart; sixty
         * PATH scans a second is not worth catching it.
         */
        std::string cmakePath_;
        bool cmakeResolved_ = false;

        std::string platform_;
        std::string backend_;
        std::string configuration_ = "Release";
    };
}
