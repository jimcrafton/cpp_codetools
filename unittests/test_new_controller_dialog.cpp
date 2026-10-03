#include "../extension/NativeEditControls/ControllerWiringAction.h"
#include "../extension/NativeEditControls/NewControllerDialog.h"
#include "../extension/NativeEditControls/TextEncoding.h"

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>

using namespace CodeToolsVsix;

namespace {

class Dir : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> counter{0};
        dir = std::filesystem::temp_directory_path() /
              ("newctl_" + std::to_string(::GetCurrentProcessId()) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(dir);
        document = dir / "dialog.newui";
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
    void write(const std::filesystem::path& relative, const std::string& text) {
        std::filesystem::create_directories((dir / relative).parent_path());
        std::ofstream(dir / relative, std::ios::binary) << text;
    }
    std::filesystem::path dir, document;
};

}  // namespace

// ---- checkNewController ----

using CheckNewController = Dir;

TEST_F(CheckNewController, AFreshNameIsUsable) {
    const NewControllerCheck check = checkNewController(document, "SaveController", "SaveController.h");
    EXPECT_TRUE(check.ok);
    EXPECT_EQ(check.message, "Will create SaveController.h next to the document");
}

TEST_F(CheckNewController, TheClassNameMustBeAnIdentifier) {
    for (const char* bad : {"", "9Lives", "Has Space", "a-b", "a::b"}) {
        EXPECT_FALSE(checkNewController(document, bad, "X.h").ok) << bad;
    }
    EXPECT_TRUE(checkNewController(document, "_ok_9", "X.h").ok);
}

TEST_F(CheckNewController, TheHeaderMustBeARelativePathInsideTheFolderWithAHeaderExtension) {
    EXPECT_FALSE(checkNewController(document, "C", "").ok);
    EXPECT_FALSE(checkNewController(document, "C", "C.cpp").ok);
    EXPECT_FALSE(checkNewController(document, "C", "C").ok);
    EXPECT_FALSE(checkNewController(document, "C", "../C.h").ok);
    EXPECT_FALSE(checkNewController(document, "C", "sub/../../C.h").ok);
    EXPECT_FALSE(checkNewController(document, "C", "C:/Windows/C.h").ok);
    EXPECT_FALSE(checkNewController(document, "C", "/C.h").ok);
    for (const char* good : {"C.h", "C.hpp", "C.hh", "C.hxx", "C.H"}) {
        EXPECT_TRUE(checkNewController(document, "C", good).ok) << good;
    }
}

TEST_F(CheckNewController, ASubfolderMustAlreadyExist) {
    EXPECT_FALSE(checkNewController(document, "C", "controllers/C.h").ok);
    std::filesystem::create_directories(dir / "controllers");
    EXPECT_TRUE(checkNewController(document, "C", "controllers/C.h").ok);
}

TEST_F(CheckNewController, AnExistingFileIsUsedOnlyIfItDefinesTheClass) {
    write("C.h", "class C {};\n");
    const NewControllerCheck adopted = checkNewController(document, "C", "C.h");
    EXPECT_TRUE(adopted.ok);
    EXPECT_EQ(adopted.message, "C.h already defines C - it will be used");

    write("C.h", "class Other {};\n");
    const NewControllerCheck foreign = checkNewController(document, "C", "C.h");
    EXPECT_FALSE(foreign.ok);
    EXPECT_EQ(foreign.message, "C.h already exists but doesn't define C");

    write("C.h", "class CTwo {};\n");   // a longer name isn't a match
    EXPECT_FALSE(checkNewController(document, "C", "C.h").ok);
}

// ---- NewControllerDialog (its logic, not showModal) ----

using NewControllerDialogTest = Dir;

TEST_F(NewControllerDialogTest, ItsChromeComesFromTheNewuiResource) {
    NewControllerDialog dialog;
    EXPECT_NE(dialog.classNameField(), nullptr);
    EXPECT_NE(dialog.headerField(), nullptr);
    EXPECT_NE(dialog.hintLabel(), nullptr);
    EXPECT_NE(dialog.createButton(), nullptr);
    EXPECT_NE(dialog.cancelButton(), nullptr);
}

TEST_F(NewControllerDialogTest, SetContextSeedsTheFieldsAndSaysWhatCreateWillDo) {
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");
    EXPECT_EQ(wideToUtf8(dialog.classNameField()->text()), "SaveController");
    EXPECT_EQ(wideToUtf8(dialog.headerField()->text()), "SaveController.h");
    EXPECT_TRUE(dialog.canCreate());
    EXPECT_EQ(dialog.hint(), "Will create SaveController.h next to the document");
    EXPECT_EQ(dialog.hintLabel()->text(), dialog.hint());
    EXPECT_TRUE(dialog.createButton()->isEnabled());
}

TEST_F(NewControllerDialogTest, AnInvalidNameDisablesCreateAndTheHintSaysWhy) {
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");

    dialog.classNameField()->setText(L"9bad name");
    EXPECT_FALSE(dialog.canCreate());
    EXPECT_EQ(dialog.hint().rfind("! ", 0), 0u) << dialog.hint();
    EXPECT_FALSE(dialog.createButton()->isEnabled());

    dialog.classNameField()->setText(L"Good");
    EXPECT_TRUE(dialog.canCreate());
    EXPECT_TRUE(dialog.createButton()->isEnabled());
}

TEST_F(NewControllerDialogTest, TheHeaderFollowsTheClassNameUntilTheUserTypesTheirOwn) {
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");

    dialog.classNameField()->setText(L"Foo");
    EXPECT_EQ(wideToUtf8(dialog.headerField()->text()), "Foo.h");
    dialog.classNameField()->setText(L"FooBar");
    EXPECT_EQ(wideToUtf8(dialog.headerField()->text()), "FooBar.h");

    dialog.headerField()->setText(L"custom.h");   // the user's own
    dialog.classNameField()->setText(L"Baz");
    EXPECT_EQ(wideToUtf8(dialog.headerField()->text()), "custom.h") << "no longer follows";
}

TEST_F(NewControllerDialogTest, AcceptCapturesTheChoiceAndClosesWithOk) {
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");
    EXPECT_FALSE(dialog.choice().has_value());

    ASSERT_TRUE(dialog.accept());
    EXPECT_EQ(dialog.result(), newui::DialogResult::Ok);
    ASSERT_TRUE(dialog.choice().has_value());
    EXPECT_EQ(dialog.choice()->className, "SaveController");
    EXPECT_EQ(dialog.choice()->header, "SaveController.h");
}

TEST_F(NewControllerDialogTest, AcceptRefusesAnUnusableChoiceAndLeavesTheDialogOpen) {
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");
    dialog.headerField()->setText(L"../escape.h");

    EXPECT_FALSE(dialog.accept());
    EXPECT_EQ(dialog.result(), newui::DialogResult::None);
    EXPECT_FALSE(dialog.choice().has_value());
}

TEST_F(NewControllerDialogTest, AnExistingHeaderThatDefinesTheClassCanBeChosen) {
    write("SaveController.h", "class SaveController {};\n");
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");
    EXPECT_TRUE(dialog.canCreate());
    EXPECT_EQ(dialog.hint(), "SaveController.h already defines SaveController - it will be used");
    EXPECT_TRUE(dialog.accept());
}

TEST_F(NewControllerDialogTest, AnExistingHeaderWithoutTheClassCannotBeChosen) {
    write("SaveController.h", "class Somebody {};\n");
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");
    EXPECT_FALSE(dialog.canCreate());
    EXPECT_FALSE(dialog.accept());
}

TEST_F(NewControllerDialogTest, CancelClosesWithCancelAndChoosesNothing) {
    NewControllerDialog dialog;
    dialog.setContext(document, "SaveController", "SaveController.h");
    dialog.cancel();
    EXPECT_EQ(dialog.result(), newui::DialogResult::Cancel);
    EXPECT_FALSE(dialog.choice().has_value());
}
