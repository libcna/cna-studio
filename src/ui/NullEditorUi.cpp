// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Ui/EditorUi.hpp"

namespace CNA::Editor
{
    const char* toString(LogSeverity severity)
    {
        switch (severity)
        {
            case LogSeverity::Trace: return "trace";
            case LogSeverity::Info: return "info";
            case LogSeverity::Warning: return "warn";
            case LogSeverity::Error: return "error";
        }
        return "info";
    }

    bool NullEditorUi::beginFrame()
    {
        if (!running_) { return false; }
        ++frameCount_;
        currentFramePanels_.clear();
        return true;
    }

    void NullEditorUi::endFrame()
    {
        lastFramePanels_ = currentFramePanels_;
    }

    bool NullEditorUi::beginPanel(const std::string& title, DockSide preferredSide)
    {
        (void)preferredSide;
        currentFramePanels_.push_back(title);
        // Reporting "visible" rather than "collapsed" is deliberate: it makes the headless run
        // exercise every panel's body, which is what turns `--headless` into a real smoke test
        // instead of a no-op.
        return true;
    }

    bool NullEditorUi::propertyField(const std::string& label,
                                     PropertyValue& value,
                                     const std::vector<std::string>& enumOptions,
                                     bool readOnly)
    {
        (void)label;
        (void)value;
        (void)enumOptions;
        (void)readOnly;
        return false;
    }

    UiTreeNodeResult NullEditorUi::treeNode(const Uuid& id,
                                            const std::string& label,
                                            bool selected,
                                            bool leaf)
    {
        (void)id;
        (void)label;
        (void)selected;

        // Expanding everything means a headless run walks the whole hierarchy, so a crash in a
        // deeply nested subtree is caught by CI rather than by the first user to expand it.
        UiTreeNodeResult result;
        result.expanded = !leaf;
        return result;
    }

    bool NullEditorUi::menuItem(const std::string& label, const std::string& shortcut, bool enabled)
    {
        (void)label;
        (void)shortcut;
        (void)enabled;
        return false;
    }

    void NullEditorUi::pressShortcut(UiKey key, UiKeyModifiers modifiers)
    {
        shortcuts_.push_back(PendingShortcut{key, modifiers});
    }

    bool NullEditorUi::isShortcutPressed(UiKey key, UiKeyModifiers modifiers)
    {
        for (auto entry = shortcuts_.begin(); entry != shortcuts_.end(); ++entry)
        {
            if (entry->key != key || !(entry->modifiers == modifiers)) { continue; }
            shortcuts_.erase(entry);
            return true;
        }
        return false;
    }

    void NullEditorUi::log(LogSeverity severity, const std::string& message)
    {
        log_.push_back(LogEntry{severity, message});
    }

    std::string NullEditorUi::getLogText(LogSeverity minimumSeverity) const
    {
        std::string text;
        for (const LogEntry& entry : log_)
        {
            if (entry.severity < minimumSeverity) { continue; }
            text += entry.message;
            text += '\n';
        }
        return text;
    }
}
