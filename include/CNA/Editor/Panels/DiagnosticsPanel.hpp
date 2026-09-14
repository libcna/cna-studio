// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/DiagnosticsPanel.hpp
 * @brief What this build of the editor actually is, and what it can actually do.
 *
 * plan.md ED-309. Because CNA fixes its graphics backend at *compile* time (ANALYSIS.md finding
 * F-01), "which backend am I on" is a property of the binary rather than a setting, and several
 * questions that would be obvious elsewhere are not: which backend the viewport is using, which
 * player builds exist to preview on, and which features the driver under this backend actually
 * supports.
 *
 * All of it is a view over data that already exists -- the capability set is queried from the
 * device, the backend table is the one `.cnaproject` validates against, and the player builds are
 * the ones discovery found. Nothing here is computed for the panel's benefit.
 */

#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Reports the editor's backend, its capabilities, and the player builds it can launch.
     *
     * Capabilities are asked of the *device* rather than derived from the backend's name. Several
     * of them -- anisotropic filtering and MSAA especially -- vary by driver within one backend, so
     * a table keyed on the backend would confidently report what this machine cannot do.
     */
    class DiagnosticsPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;
    };
}
