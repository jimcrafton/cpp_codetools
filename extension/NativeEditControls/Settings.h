#pragma once

#include <newui/delegate.h>

#include <map>
#include <mutex>
#include <string>

namespace newui { class RunLoop; }

namespace CodeToolsVsix
{
    // The user's codetools++ options, as the host (VS Tools > Options) last pushed them. Keys are the
    // CodeTools.* names in CodeToolsSettings.registration.json; a key never pushed reads as its default.
    // set() may be called from any thread (the managed host's). onChanged fires on the run loop given to
    // setRunLoop() - the edit thread in the DLL - so UI code can apply a change directly.
    class Settings
    {
    public:
        struct Def
        {
            const char* key;
            const char* defaultValue;
        };

        // C++ editor
        static constexpr Def kHighlightDelayMs{ "CodeTools.editor.highlightDelayMs", "50" };
        static constexpr Def kDiagnosticsDelayMs{ "CodeTools.editor.diagnosticsDelayMs", "600" };
        static constexpr Def kWordWrap{ "CodeTools.editor.wordWrap", "false" };
        static constexpr Def kInactiveNotes{ "CodeTools.editor.inactiveNotes", "true" };   // "inactive: depends on X" after skipped code
        static constexpr Def kFadeInactive{ "CodeTools.editor.fadeInactive", "true" };   // skipped code keeps its colors, faded
        static constexpr Def kFadeStrength{ "CodeTools.editor.fadeStrength", "70" };   // percent, 0..100
        static constexpr Def kPeekTabWidth{ "CodeTools.editor.peekTabWidth", "4" };
        static constexpr Def kMinimapWidth{ "CodeTools.editor.minimapWidth", "28" };   // DIP

        // Designer. Pane widths and the blank canvas size take effect for designers opened afterward.
        static constexpr Def kToolboxWidth{ "CodeTools.designer.toolboxWidth", "220" };
        static constexpr Def kPropertiesWidth{ "CodeTools.designer.propertiesWidth", "300" };
        static constexpr Def kCanvasWidth{ "CodeTools.designer.canvasWidth", "640" };
        static constexpr Def kNewControlWidth{ "CodeTools.designer.newControlWidth", "120" };
        static constexpr Def kHandleSize{ "CodeTools.designer.handleSize", "7" };

        // Project explorer
        static constexpr Def kExplorerDefaultView{ "CodeTools.explorer.defaultView", "Symbols" };

        static Settings& instance();

        // Fires with the key that changed, only when its value did.
        newui::Delegate<Settings, std::string> onChanged;

        // Where onChanged runs. Unset: on the thread that called set(). Must outlive this object.
        void setRunLoop(newui::RunLoop* loop);

        // Any thread.
        void set(const std::string& key, const std::wstring& value);

        std::wstring getString(const Def& def) const;
        // Clamped to [minValue, maxValue]; an unparsable value reads as the default.
        int getInt(const Def& def, int minValue, int maxValue) const;
        bool getBool(const Def& def) const;

    private:
        std::wstring valueOrDefault(const Def& def) const;

        mutable std::mutex mutex_;
        std::map<std::string, std::wstring> values_;
        newui::RunLoop* loop_ = nullptr;
    };
}
