// SPDX-License-Identifier: MS-PL
/**
 * @file StudioStartupDocument.cpp
 * @brief Opening the project and the scene a Studio was started with.
 */

#include "CNA/Studio/StudioStartupDocument.hpp"

#include "CNA/Studio/StudioContext.hpp"

namespace CNA::Studio
{
    StudioStartupDocument openStudioStartupDocument(StudioContext& context,
                                                    const std::string& projectPath,
                                                    const std::string& scenePath)
    {
        StudioStartupDocument result;

        if (projectPath.empty())
        {
            // A scene with a camera in it, not an empty document. A scene with no camera renders
            // nothing, and nothing is what a broken editor also renders.
            context.newScene("Untitled");
        }
        else if (context.openProject(projectPath))
        {
            result.projectOpened = true;
        }
        else
        {
            // The context has already logged why through its own log sink. This sentence is for
            // the places a log sink does not reach -- standard error, and the status bar.
            result.error = "Could not open '" + projectPath + "'.";
            return result;
        }

        if (scenePath.empty()) { return result; }

        if (context.openScene(scenePath)) { result.sceneOpened = true; }
        else { result.error = "Could not open scene '" + scenePath + "'."; }

        return result;
    }
}
