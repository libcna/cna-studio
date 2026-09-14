// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/ComparisonPanel.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns @p value as a percentage with two decimals, e.g. "0.13%". */
        std::string toPercentage(double value)
        {
            std::string text = std::to_string(value * 100.0);
            const std::size_t point = text.find('.');
            if (point != std::string::npos && text.size() > point + 3) { text.resize(point + 3); }
            return text + "%";
        }
    }

    void ComparisonPanel::startComparison()
    {
        // The scene on screen, not the project's startup scene. A comparison of something the user
        // is not looking at answers a question nobody asked -- and, like play mode, the players
        // read from disk, so what is on screen has to be written there first.
        if (context_.getScenePath().empty())
        {
            context_.log(LogSeverity::Warning,
                         "Save the scene before comparing backends: each player is a separate "
                         "process and reads the scene from disk.");
            return;
        }

        if (context_.getHistory().isDirty())
        {
            if (!context_.saveScene())
            {
                context_.log(LogSeverity::Error, "Could not save the scene; not comparing backends.");
                return;
            }
            context_.log(LogSeverity::Info, "Saved the scene before comparing backends.");
        }

        ComparisonRequest request;
        request.projectPath = context_.getProject().getFilePath();
        request.outputDirectory = getDefaultComparisonDirectory(request.projectPath);
        request.builds = actions_.getPlayerBuilds();
        request.tolerance = tolerance_;

        // Relative to the project, like play mode's: two processes need not agree on a working
        // directory, and the project root is the one anchor both already have.
        std::error_code relativeError;
        const std::filesystem::path relativeScene = std::filesystem::relative(
            std::filesystem::path{context_.getScenePath()},
            std::filesystem::path{request.projectPath}.parent_path(), relativeError);
        if (!relativeError) { request.scenePath = relativeScene.generic_string(); }

        summary_.clear();

        // The reader and writer come from the viewport, because decoding a PNG needs a graphics API
        // and exactly one module may have one (D-03). A headless editor supplies a viewport that
        // reads nothing, and the run then reports that it could not read the captures back --
        // which is the honest answer rather than a crash.
        EditorViewport& viewport = actions_.getViewport();

        if (!comparison_.start(
                request, [&viewport](const std::string& path) { return viewport.readImageFile(path); },
                [&viewport](const std::string& path, const ImageBuffer& image) {
                    return viewport.writeImageFile(path, image);
                }))
        {
            context_.log(LogSeverity::Error, "Cannot compare backends: " + comparison_.getError());
            return;
        }

        context_.log(LogSeverity::Info,
                     "Comparing " + std::to_string(request.builds.size())
                         + " backends; captures go to " + request.outputDirectory);
    }

    void ComparisonPanel::drawEntry(const ComparisonEntry& entry)
    {
        std::string line = entry.backend;
        if (entry.isReference) { line += "  (reference)"; }

        if (!entry.errorMessage.empty())
        {
            ui_.text(line + " -- " + entry.errorMessage);
            return;
        }

        if (!entry.captured)
        {
            ui_.text(line + " -- waiting for a frame");
            return;
        }

        if (entry.isReference)
        {
            ui_.text(line + " -- " + entry.capturePath);
            return;
        }

        if (!entry.difference.comparable)
        {
            // A size mismatch is not a disagreement about pixels, it is a capture that went wrong,
            // and the two call for entirely different actions.
            ui_.text(line + " -- cannot compare: " + entry.difference.incomparableReason);
            return;
        }

        if (entry.difference.matches())
        {
            ui_.text(line + " -- identical (largest channel difference "
                     + std::to_string(entry.difference.maxChannelDelta) + ")");
            return;
        }

        ui_.text(line + " -- " + std::to_string(entry.difference.differingPixels) + " of "
                 + std::to_string(entry.difference.totalPixels) + " pixels differ ("
                 + toPercentage(entry.difference.getDifferingFraction()) + "), largest channel "
                 + "difference " + std::to_string(entry.difference.maxChannelDelta));

        // Where, not just how much. A band along one edge is a viewport or scissor problem; a
        // scattering over one sprite is a filtering one. The rectangle usually is the diagnosis.
        const EditorRectangle& box = entry.difference.boundingBox;
        ui_.text("        within " + std::to_string(box.width) + "x" + std::to_string(box.height)
                 + " at (" + std::to_string(box.x) + ", " + std::to_string(box.y) + ")");

        if (!entry.differencePath.empty()) { ui_.text("        " + entry.differencePath); }
    }

    void ComparisonPanel::draw()
    {
        if (!ui_.beginPanel("Backends", DockSide::Bottom)) { ui_.endPanel(); return; }

        if (!context_.hasProject())
        {
            ui_.text("Open a project to compare backends.");
            ui_.endPanel();
            return;
        }

        ComparisonRequest probe;
        probe.projectPath = context_.getProject().getFilePath();
        probe.outputDirectory = getDefaultComparisonDirectory(probe.projectPath);
        probe.builds = actions_.getPlayerBuilds();

        const std::string problem = describeComparisonProblem(probe);
        if (!problem.empty())
        {
            // Said before the button, for the same reason the build panel says its problem first:
            // the common case is a user who has one player build, and "nothing happened" would be
            // the worst possible answer to pressing Compare.
            ui_.text("Cannot compare: " + problem);
            ui_.endPanel();
            return;
        }

        const ComparisonState state = comparison_.getState();

        if (state == ComparisonState::Launching || state == ComparisonState::Capturing)
        {
            ui_.text(std::string{"Comparing: "} + toString(state));
            if (ui_.button("Cancel##comparison"))
            {
                comparison_.cancel();
                context_.log(LogSeverity::Warning, "Backend comparison cancelled.");
            }
        }
        else
        {
            if (ui_.button("Compare##comparison")) { startComparison(); }

            ui_.sameLine();
            ui_.setNextItemWidth(90.0f);
            PropertyValue tolerance{static_cast<std::int64_t>(tolerance_)};
            if (ui_.propertyField("Tolerance", tolerance))
            {
                // Clamped rather than trusted: a tolerance of 255 calls every pair of images
                // identical, which is a comparison that can never report anything.
                tolerance_ = static_cast<int>(std::clamp<std::int64_t>(tolerance.get<std::int64_t>(0), 0, 64));
            }
        }

        if (!comparison_.getError().empty()) { ui_.text("Problem: " + comparison_.getError()); }

        if (state == ComparisonState::Finished && summary_.empty())
        {
            summary_ = comparison_.allBackendsAgree()
                           ? "Every backend drew the same picture."
                           : "The backends do not agree; see the rows below.";
            context_.log(comparison_.allBackendsAgree() ? LogSeverity::Info : LogSeverity::Warning,
                         summary_);
        }

        if (!summary_.empty())
        {
            ui_.separator();
            ui_.text(summary_);
        }

        if (comparison_.getEntries().empty())
        {
            ui_.endPanel();
            return;
        }

        ui_.separator();
        for (const ComparisonEntry& entry : comparison_.getEntries()) { drawEntry(entry); }

        ui_.endPanel();
    }
}
