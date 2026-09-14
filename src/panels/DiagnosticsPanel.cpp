// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/DiagnosticsPanel.hpp"

#include <string>
#include <vector>

#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    void DiagnosticsPanel::draw()
    {
        if (!ui_.beginPanel("Diagnostics", DockSide::Bottom)) { ui_.endPanel(); return; }

        ui_.text(std::string{"Editor UI: "} + ui_.getBackendName());
        ui_.text(std::string{"Viewport: "} + actions_.getViewport().getBackendName());

        // Which effect the 3D model pass got (ED-402). Reported rather than assumed: PbrEffect is
        // a CNA extension, this editor supports backends of three support tiers, and "why does a
        // model look different on that machine" deserves an answer that is not a screenshot
        // comparison. "none" is what a build with no CNA says, and is the honest word for it.
        ui_.text(std::string{"Model effect: "} + actions_.getViewport().getModelEffectName());
        ui_.separator();

        const std::vector<ViewportCapability> capabilities = actions_.getViewport().getBackendCapabilities();
        if (capabilities.empty())
        {
            // Said plainly. A blank section reads as "not implemented"; this is the honest state of
            // a headless run, where there is no device to ask.
            ui_.text("No graphics device, so no capabilities to report.");
        }
        else
        {
            ui_.text("Backend capabilities");
            for (const ViewportCapability& capability : capabilities)
            {
                ui_.text(std::string{"    "} + (capability.supported ? "yes  " : "no   ")
                         + capability.name);
            }
        }
        ui_.separator();

        // Because CNA fixes its backend at compile time, "run this on Vulkan" means "launch
        // cna-player-vulkan", and whether that binary exists is a question with a real answer. The
        // editor offers only what it found, and this is where a user sees why.
        const std::vector<PlayerBuild>& builds = actions_.getPlayerBuilds();
        ui_.text("Player builds found: " + std::to_string(builds.size()));
        for (const PlayerBuild& build : builds)
        {
            ui_.text("    " + build.backend + "  " + build.executablePath);
        }
        ui_.separator();

        // What the *player* makes of the input the editor forwards. Reported rather than assumed:
        // the editor's viewport panel and the game's window are different sizes, so the pointer
        // the game sees is not the pointer the user is looking at, and the difference between the
        // two is exactly what a user debugging "my game does not respond" needs to see.
        if (actions_.getPlayMode() == PlayMode::Stopped)
        {
            ui_.text("Player input: no player is running.");
        }
        else
        {
            const PlayerInputSnapshot& input = actions_.getPlayerInput();

            std::string keys;
            for (const std::string& key : input.keys)
            {
                if (!keys.empty()) { keys += ", "; }
                keys += key;
            }

            ui_.text("Player input -- keys: " + (keys.empty() ? std::string{"none"} : keys));

            if (!input.hasPointer())
            {
                ui_.text("    pointer: not over the viewport");
            }
            else
            {
                std::string buttons;
                if (input.leftButton) { buttons += " left"; }
                if (input.middleButton) { buttons += " middle"; }
                if (input.rightButton) { buttons += " right"; }

                ui_.text("    pointer: " + std::to_string(static_cast<int>(input.mouseX)) + ", "
                         + std::to_string(static_cast<int>(input.mouseY)) + " of "
                         + std::to_string(static_cast<int>(input.surfaceWidth)) + "x"
                         + std::to_string(static_cast<int>(input.surfaceHeight))
                         + (buttons.empty() ? std::string{"  (no buttons)"} : "  buttons:" + buttons));
            }
        }
        ui_.separator();

        ui_.text("Backends this editor knows about");
        for (const BackendInfo& backend : getKnownBackends())
        {
            const char* support = "runtime only";
            switch (backend.support)
            {
                case BackendEditorSupport::EditorSupported: support = "editor      "; break;
                case BackendEditorSupport::PreviewOnly: support = "preview only"; break;
                case BackendEditorSupport::RuntimeOnly: support = "runtime only"; break;
            }
            ui_.text(std::string{"    "} + support + "  " + backend.commandLineName + "  ("
                     + backend.displayName + ")");
        }

        ui_.endPanel();
    }
}
