// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/ConsolePanel.hpp"

#include <string>
#include <vector>

#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief Returns the filter's display name for @p severity.
         *
         * Distinct from toString(), which produces the short prefix a log line carries ("warn").
         * Reusing that here would leave the combo's current value absent from its own option list,
         * and a combo whose selection matches nothing renders empty.
         */
        const char* severityDisplayName(LogSeverity severity)
        {
            switch (severity)
            {
                case LogSeverity::Trace: return "Trace";
                case LogSeverity::Info: return "Info";
                case LogSeverity::Warning: return "Warning";
                case LogSeverity::Error: return "Error";
            }
            return "Trace";
        }

        /** @brief Maps a display name from the filter back onto the enumeration. */
        LogSeverity parseLogSeverityName(std::string_view name)
        {
            if (name == "Error") { return LogSeverity::Error; }
            if (name == "Warning") { return LogSeverity::Warning; }
            if (name == "Info") { return LogSeverity::Info; }
            return LogSeverity::Trace;
        }
    }

    void ConsolePanel::draw()
    {
        if (!ui_.beginPanel("Console", DockSide::Bottom)) { ui_.endPanel(); return; }

        if (ui_.button("Copy")) { ui_.setClipboardText(ui_.getLogText(minimumSeverity_)); }
        ui_.sameLine();
        if (ui_.button("Clear")) { ui_.clearLog(); }
        ui_.sameLine();
        ui_.checkbox("Auto-scroll", autoScroll_);
        ui_.sameLine();

        // The filter is the console's own state, not a document property, so it is not a command
        // and does not belong in the undo stack -- what a user chooses to look at is not an edit.
        static const std::vector<std::string> kSeverities{"Trace", "Info", "Warning", "Error"};
        ui_.setNextItemWidth(110.0f);

        PropertyValue severity{PropertyValue::EnumValue{severityDisplayName(minimumSeverity_)}};
        if (ui_.propertyField("##consoleSeverity", severity, kSeverities))
        {
            minimumSeverity_ = parseLogSeverityName(severity.get<PropertyValue::EnumValue>().name);
        }

        ui_.separator();

        UiLogViewOptions options;
        options.minimumSeverity = minimumSeverity_;
        options.autoScroll = autoScroll_;
        ui_.drawLogView(options);

        ui_.endPanel();
    }
}
