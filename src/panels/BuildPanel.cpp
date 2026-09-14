// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/BuildPanel.hpp"

#include <string>
#include <vector>

#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    BuildRequest BuildPanel::makeRequest() const
    {
        BuildRequest request = makeBuildRequest(context_.getProject(), platform_, backend_);
        request.cmakePath = cmakePath_;
        return request;
    }

    void BuildPanel::draw()
    {
        if (!ui_.beginPanel("Build", DockSide::Bottom)) { ui_.endPanel(); return; }

        if (!context_.hasProject())
        {
            ui_.text("Open a project to build it.");
            ui_.endPanel();
            return;
        }

        if (!cmakeResolved_)
        {
            cmakePath_ = findCMake();
            cmakeResolved_ = true;
        }

        const Project& project = context_.getProject();

        // Defaults taken from the project the first time round, not hard-coded: the project already
        // declares which platforms it targets and which backend it prefers, and asking the user to
        // say it again would be asking them to repeat themselves.
        if (platform_.empty() && !project.getTargetPlatforms().empty())
        {
            platform_ = project.getTargetPlatforms().front();
        }
        if (backend_.empty())
        {
            const BackendInfo* backend = findBackend(project.getDefaultGraphicsBackend());
            backend_ = backend != nullptr ? backend->cmakeName : std::string{};
        }

        PropertyValue platform{PropertyValue::EnumValue{platform_}};
        ui_.setNextItemWidth(160.0f);
        if (ui_.propertyField("Platform", platform, project.getTargetPlatforms()))
        {
            platform_ = platform.get<PropertyValue::EnumValue>().name;
        }

        std::vector<std::string> backends;
        for (const BackendInfo& entry : getKnownBackends()) { backends.push_back(entry.cmakeName); }

        PropertyValue backend{PropertyValue::EnumValue{backend_}};
        ui_.setNextItemWidth(160.0f);
        if (ui_.propertyField("Backend", backend, backends))
        {
            backend_ = backend.get<PropertyValue::EnumValue>().name;
        }

        static const std::vector<std::string> kConfigurations{"Debug", "Release", "RelWithDebInfo",
                                                              "MinSizeRel"};
        PropertyValue configuration{PropertyValue::EnumValue{configuration_}};
        ui_.setNextItemWidth(160.0f);
        if (ui_.propertyField("Configuration", configuration, kConfigurations))
        {
            configuration_ = configuration.get<PropertyValue::EnumValue>().name;
        }

        ui_.separator();

        BuildRequest request = makeRequest();
        request.configuration = configuration_;

        const std::string problem = describeBuildProblem(request);
        if (!problem.empty())
        {
            // Said before the button rather than after a failure. A missing compiler otherwise
            // arrives as a wall of CMake output that says nothing a user can act on.
            ui_.text("Cannot build: " + problem + ".");
            ui_.endPanel();
            return;
        }

        ui_.text("Output: " + request.buildDirectory);
        for (const BuildStep& step : planBuild(request))
        {
            // The exact commands, because a real build has options the editor does not model and
            // someone who needs one has to be able to take the command away and run it by hand.
            ui_.text("    " + step.toCommandLine());
        }

        ui_.separator();

        const BuildState state = build_.getState();
        if (state == BuildState::Running)
        {
            ui_.text("Building: step " + std::to_string(build_.getStepNumber()) + " of "
                     + std::to_string(build_.getSteps().size()));
            if (ui_.button("Cancel##build"))
            {
                build_.cancel();
                context_.log(LogSeverity::Warning, "Build cancelled.");
            }
        }
        else
        {
            if (ui_.button("Build##build"))
            {
                std::string errorMessage;
                if (build_.start(request, &errorMessage))
                {
                    context_.log(LogSeverity::Info, "Build started; log at " + build_.getLogPath());
                }
                else
                {
                    context_.log(LogSeverity::Error, "Cannot start the build: " + errorMessage);
                }
            }

            if (state != BuildState::Idle)
            {
                ui_.sameLine();
                ui_.text(std::string{"Last build "} + toString(state));
            }
        }

        if (build_.getLogPath().empty())
        {
            ui_.endPanel();
            return;
        }

        ui_.separator();
        ui_.text("Log: " + build_.getLogPath());

        // A tail, not the whole file: a failing build can produce megabytes, and the last dozen
        // lines are where the error is.
        for (const std::string& line : build_.readLogTail(12)) { ui_.text("    " + line); }

        ui_.endPanel();
    }
}
