// Settings: the options the VS host pushes (CodeTools.*), with typed reads and a change delegate. And
// CppEditor following them.

#include "../extension/NativeEditControls/CppEditor.h"
#include "../extension/NativeEditControls/SelectionOverlay.h"
#include "../extension/NativeEditControls/Settings.h"
#include "../extension/NativeEditControls/Workspace.h"

#include <newui/rootview.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using CodeToolsVsix::Settings;

namespace {

struct Heard
{
    std::vector<std::string> keys;
    newui::Connection connection;
};

}

TEST(Settings, ReadsTheDefaultUntilTheHostPushesAValue) {
    Settings settings;

    EXPECT_EQ(settings.getInt(Settings::kHighlightDelayMs, 0, 5000), 50);
    EXPECT_FALSE(settings.getBool(Settings::kWordWrap));
    EXPECT_EQ(settings.getString(Settings::kPeekTabWidth), L"4");

    settings.set(Settings::kHighlightDelayMs.key, L"120");
    settings.set(Settings::kWordWrap.key, L"true");
    EXPECT_EQ(settings.getInt(Settings::kHighlightDelayMs, 0, 5000), 120);
    EXPECT_TRUE(settings.getBool(Settings::kWordWrap));
}

TEST(Settings, AnIntIsClampedAndAnUnparsableOneReadsAsTheDefault) {
    Settings settings;

    settings.set(Settings::kMinimapWidth.key, L"5000");
    EXPECT_EQ(settings.getInt(Settings::kMinimapWidth, 12, 200), 200);
    settings.set(Settings::kMinimapWidth.key, L"-3");
    EXPECT_EQ(settings.getInt(Settings::kMinimapWidth, 12, 200), 12);
    settings.set(Settings::kMinimapWidth.key, L"wide");
    EXPECT_EQ(settings.getInt(Settings::kMinimapWidth, 12, 200), 28) << "the default, 28";
    settings.set(Settings::kMinimapWidth.key, L"");
    EXPECT_EQ(settings.getInt(Settings::kMinimapWidth, 12, 200), 28);
}

TEST(Settings, ABoolTakesTrueOneOrOnInAnyCase) {
    Settings settings;

    for (const wchar_t* yes : { L"true", L"TRUE", L"1", L"On" }) {
        settings.set(Settings::kWordWrap.key, yes);
        EXPECT_TRUE(settings.getBool(Settings::kWordWrap)) << std::wstring(yes).size();
    }
    for (const wchar_t* no : { L"false", L"0", L"off", L"" }) {
        settings.set(Settings::kWordWrap.key, no);
        EXPECT_FALSE(settings.getBool(Settings::kWordWrap));
    }
}

TEST(Settings, AChangeIsAnnouncedWithItsKeyButOnlyIfTheValueChanged) {
    Settings settings;
    Heard heard;
    heard.connection = settings.onChanged.add([&heard](Settings&, std::string key) {
        heard.keys.push_back(key);
        return newui::SyncReturn::Ignored;
    });

    settings.set("CodeTools.editor.wordWrap", L"true");
    settings.set("CodeTools.editor.wordWrap", L"true");   // same value
    settings.set("CodeTools.editor.wordWrap", L"false");

    ASSERT_EQ(heard.keys.size(), 2u);
    EXPECT_EQ(heard.keys[0], "CodeTools.editor.wordWrap");
}

TEST(Settings, AListenerThatDisconnectedIsNotCalled) {
    Settings settings;
    Heard heard;
    heard.connection = settings.onChanged.add([&heard](Settings&, std::string key) {
        heard.keys.push_back(key);
        return newui::SyncReturn::Ignored;
    });
    settings.onChanged.remove(heard.connection);

    settings.set("CodeTools.editor.wordWrap", L"true");

    EXPECT_TRUE(heard.keys.empty());
}

TEST(Settings, AKeyTheHostPushesThatNoCodeKnowsIsKeptAnyway) {
    Settings settings;
    settings.set("CodeTools.explorer.hideNative", L"true");

    EXPECT_EQ(settings.getString({ "CodeTools.explorer.hideNative", "false" }), L"true");
}

TEST(DesignerSettings, TheDefaultsAreTheSizesTheDesignerHadBefore) {
    EXPECT_FLOAT_EQ(CodeToolsVsix::Workspace::toolboxPaneWidth(), 220.0f);
    EXPECT_FLOAT_EQ(CodeToolsVsix::Workspace::propertiesPaneWidth(), 300.0f);
    EXPECT_FLOAT_EQ(CodeToolsVsix::Workspace::defaultCanvasWidth(), 640.0f);
    EXPECT_FLOAT_EQ(CodeToolsVsix::Workspace::newControlDefaultWidth(), 120.0f);
    EXPECT_FLOAT_EQ(CodeToolsVsix::SelectionOverlay::handleSize(), 7.0f);
}

TEST(DesignerSettings, ASizeFollowsItsSettingAndStaysInARange) {
    Settings& shared = Settings::instance();

    shared.set(Settings::kPropertiesWidth.key, L"410");
    EXPECT_FLOAT_EQ(CodeToolsVsix::Workspace::propertiesPaneWidth(), 410.0f);
    shared.set(Settings::kPropertiesWidth.key, L"5");
    EXPECT_FLOAT_EQ(CodeToolsVsix::Workspace::propertiesPaneWidth(), 160.0f) << "too narrow to use: raised to the minimum";
    shared.set(Settings::kPropertiesWidth.key, L"300");

    shared.set(Settings::kHandleSize.key, L"10");
    EXPECT_FLOAT_EQ(CodeToolsVsix::SelectionOverlay::handleSize(), 10.0f);
    shared.set(Settings::kHandleSize.key, L"7");
}

TEST(CppEditorSettings, WordWrapFollowsTheSettingWhileTheEditorIsOpen) {
    Settings& shared = Settings::instance();
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "settingsTestRoot");
    {
        CodeToolsVsix::CppEditor editor(root);
        ASSERT_NE(editor.textControl(), nullptr);
        EXPECT_FALSE(editor.textControl()->wordWrap()) << "off by default: code scrolls sideways";

        shared.set(Settings::kWordWrap.key, L"true");
        EXPECT_TRUE(editor.textControl()->wordWrap());

        shared.set(Settings::kWordWrap.key, L"false");
        EXPECT_FALSE(editor.textControl()->wordWrap());
    }
    // The editor is gone: a later change must not reach it.
    shared.set(Settings::kWordWrap.key, L"true");
    shared.set(Settings::kWordWrap.key, L"false");
}

TEST(CppEditorSettings, AnEditorOpenedAfterTheSettingIsPushedStartsWithIt) {
    Settings& shared = Settings::instance();
    shared.set(Settings::kWordWrap.key, L"true");
    {
        auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 900, 700), "settingsTestRoot2");
        CodeToolsVsix::CppEditor editor(root);
        ASSERT_NE(editor.textControl(), nullptr);
        EXPECT_TRUE(editor.textControl()->wordWrap());
    }
    shared.set(Settings::kWordWrap.key, L"false");
}
