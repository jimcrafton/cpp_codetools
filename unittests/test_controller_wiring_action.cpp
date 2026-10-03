#include "../extension/NativeEditControls/ControllerBindings.h"
#include "../extension/NativeEditControls/ControllerWiringAction.h"
#include "../extension/NativeEditControls/TextEncoding.h"

#include <cpptools/parser.h>

#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/reflection.h>

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <iterator>

// registerReflectionData() is already run once for this whole binary by test_component_editor.cpp's
// ::testing::Environment, so the real newui classes are registered here.

using namespace CodeToolsVsix;

namespace {

// A controller that compiles on its own (a stand-in newui with the same Delegate::add shape) and
// has nothing wired yet.
const char* kControllerHeader =
    "namespace newui {\n"
    "enum class SyncReturn { Handled, Ignored };\n"
    "template<typename S, typename... A> struct Delegate {\n"
    "    template<typename T> void add(T* i, SyncReturn (T::*m)(S&, A...)) {}\n"
    "};\n"
    "struct Control { Delegate<Control> onClick; };\n"
    "struct Button : Control {};\n"
    "class Component { protected: virtual bool internal_init() { return true; } };\n"
    "class RootController : public Component {};\n"
    "}\n"
    "class SaveDialogController : public newui::RootController {\n"
    "public:\n"
    "    using RootController::RootController;\n"
    "};\n";

class Fixture : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> counter{0};
        dir = std::filesystem::temp_directory_path() /
              ("wiring_action_" + std::to_string(::GetCurrentProcessId()) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(dir);
        document = dir / "dialog.newui";
        header = dir / "SaveDialogController.h";
        controller = {"SaveDialogController", "SaveDialogController.h"};
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    void writeHeader(const std::string& text) { std::ofstream(header, std::ios::binary) << text; }
    std::string headerOnDisk() const {
        std::ifstream in(header, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    newui::Button* addButton(const char* name) {
        auto* button = new newui::Button();
        button->setName(name);
        frame.rootView().addChild(button);
        return button;
    }

    const newui::reflection::Class& buttonClass() {
        const newui::reflection::Class* clazz = newui::reflection::classinfo(std::string("Button"));
        EXPECT_NE(clazz, nullptr);
        return *clazz;
    }

    WireResult wire(newui::Button& button, const char* delegate = "onClick", const std::string& handler = "") {
        WireResult out;
        bool called = false;
        wireDelegate(service, document, controller, button, button.name(), buttonClass(), delegate, handler,
                     [&](WireResult r) { out = std::move(r); called = true; });
        EXPECT_TRUE(called) << "the disk and editor routes complete before wireDelegate returns";
        return out;
    }

    std::vector<std::string> recorded(newui::Button& button, const char* delegate = "onClick") {
        std::vector<const newui::reflection::Delegate*> delegates;
        buttonClass().allDelegates(delegates);
        for (const auto* d : delegates) {
            if (d->name() == delegate) {
                return d->describedListeners(&button);
            }
        }
        return {};
    }

    std::vector<VerifiedBinding> verifyAll() {
        return verifyBindings(collectRecordedBindings(frame.rootView()), "SaveDialogController", headerOnDisk(),
                              cpptools::defaultCompileArgs());
    }

    std::filesystem::path dir, document, header;
    ControllerRef controller;
    DocumentEditService service;
    newui::Frame frame;
};

class FakeEditor : public IEditableDocument {
public:
    explicit FakeEditor(std::wstring text) : text_(std::move(text)) {}
    DocumentSnapshot snapshot() const override { return {text_, version_}; }
    EditStatus applyEdits(std::uint64_t expectedVersion, const std::vector<TextEdit>& edits) override {
        if (expectedVersion != version_) {
            return EditStatus::VersionMismatch;
        }
        if (!applyTextEdits(text_, edits)) {
            return EditStatus::InvalidEdit;
        }
        ++version_;
        return EditStatus::Ok;
    }
    const std::wstring& text() const { return text_; }

private:
    std::wstring text_;
    std::uint64_t version_ = 1;
};

std::size_t countOf(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

TEST_F(Fixture, WiringAddsTheFieldTheHandlerAndTheCallAndRecordsTheDescriptorOnTheControl) {
    writeHeader(kControllerHeader);
    newui::Button* button = addButton("saveButton");

    const WireResult result = wire(*button);
    ASSERT_EQ(result.status, WireStatus::Ok) << static_cast<int>(result.editStatus);
    EXPECT_EQ(result.handlerName, "onSaveButtonClick");
    EXPECT_EQ(result.descriptor, "this@SaveDialogController.onSaveButtonClick");

    const std::string text = headerOnDisk();
    EXPECT_NE(text.find("//@reflect connect=true"), std::string::npos) << text;
    EXPECT_NE(text.find("newui::Button* saveButton_ = nullptr;"), std::string::npos) << text;
    EXPECT_NE(text.find("newui::SyncReturn onSaveButtonClick(newui::Control& sender)"), std::string::npos) << text;
    EXPECT_NE(text.find("saveButton_->onClick.add(this, &SaveDialogController::onSaveButtonClick);"), std::string::npos)
        << text;

    EXPECT_EQ(recorded(*button), std::vector<std::string>{"this@SaveDialogController.onSaveButtonClick"});
}

TEST_F(Fixture, WhatItGeneratesVerifiesCleanAgainstTheDescriptorItRecorded) {
    writeHeader(kControllerHeader);
    newui::Button* button = addButton("saveButton");
    ASSERT_EQ(wire(*button).status, WireStatus::Ok);

    const std::vector<VerifiedBinding> checks = verifyAll();
    ASSERT_EQ(checks.size(), 1u);
    EXPECT_EQ(checks[0].check.status, cpptools::BindingStatus::Ok) << checks[0].check.detail;
}

TEST_F(Fixture, WiringTheSameEventAgainChangesNothingAndRecordsNothingTwice) {
    writeHeader(kControllerHeader);
    newui::Button* button = addButton("saveButton");
    ASSERT_EQ(wire(*button).status, WireStatus::Ok);
    const std::string after = headerOnDisk();

    const WireResult again = wire(*button);
    EXPECT_EQ(again.status, WireStatus::HandlerExists);
    EXPECT_EQ(headerOnDisk(), after);
    EXPECT_EQ(recorded(*button).size(), 1u);
}

TEST_F(Fixture, TwoControlsShareOneInternalInitAndBothVerify) {
    writeHeader(kControllerHeader);
    newui::Button* save = addButton("saveButton");
    newui::Button* cancel = addButton("cancelButton");
    ASSERT_EQ(wire(*save).status, WireStatus::Ok);
    ASSERT_EQ(wire(*cancel).status, WireStatus::Ok);

    EXPECT_EQ(countOf(headerOnDisk(), "internal_init() override"), 1u);   // the base class declares its own
    const std::vector<VerifiedBinding> checks = verifyAll();
    ASSERT_EQ(checks.size(), 2u);
    for (const VerifiedBinding& check : checks) {
        EXPECT_EQ(check.check.status, cpptools::BindingStatus::Ok) << check.viewName << ": " << check.check.detail;
    }
}

TEST_F(Fixture, ACustomHandlerNameIsUsedInTheCodeAndTheDescriptor) {
    writeHeader(kControllerHeader);
    newui::Button* button = addButton("saveButton");
    const WireResult result = wire(*button, "onClick", "saveClicked");
    ASSERT_EQ(result.status, WireStatus::Ok);
    EXPECT_EQ(result.descriptor, "this@SaveDialogController.saveClicked");
    EXPECT_NE(headerOnDisk().find("&SaveDialogController::saveClicked);"), std::string::npos);
}

TEST_F(Fixture, FailuresChangeNothingAndRecordNothing) {
    newui::Button* button = addButton("saveButton");

    EXPECT_EQ(wire(*button).status, WireStatus::HeaderUnreadable);   // no header on disk yet
    EXPECT_TRUE(recorded(*button).empty());

    writeHeader(kControllerHeader);
    EXPECT_EQ(wire(*button, "onNoSuchEvent").status, WireStatus::UnknownDelegate);

    controller.className = "SomeOtherController";
    EXPECT_EQ(wire(*button).status, WireStatus::ControllerClassMissing);

    EXPECT_EQ(headerOnDisk(), kControllerHeader);
    EXPECT_TRUE(recorded(*button).empty());
}

TEST_F(Fixture, AnOpenEditorsTextIsWhatGetsEditedNotTheFileOnDisk) {
    writeHeader(kControllerHeader);
    FakeEditor editor(utf8ToWide(kControllerHeader) + L"// unsaved edit in the editor\n");
    service.registerEditor(header, &editor);
    newui::Button* button = addButton("saveButton");

    ASSERT_EQ(wire(*button).status, WireStatus::Ok);

    EXPECT_NE(editor.text().find(L"onSaveButtonClick"), std::wstring::npos);
    EXPECT_NE(editor.text().find(L"// unsaved edit in the editor"), std::wstring::npos) << "the unsaved text survives";
    EXPECT_EQ(headerOnDisk(), kControllerHeader) << "the disk copy is untouched";
    EXPECT_EQ(recorded(*button).size(), 1u);
    service.unregisterEditor(header, &editor);
}

TEST(DefaultControllerName, IsTheDocumentsNameAsAnIdentifierWithControllerAppended) {
    EXPECT_EQ(defaultControllerClassName("C:\\x\\savedialog.newui"), "SavedialogController");
    EXPECT_EQ(defaultControllerClassName("Main Window.newui"), "MainWindowController");
    EXPECT_EQ(defaultControllerClassName("2fast.newui"), "_2fastController");
    EXPECT_EQ(defaultControllerClassName("my-dialog_v2.newui"), "Mydialog_v2Controller");
    EXPECT_EQ(defaultControllerClassName(""), "DocumentController");
}

TEST_F(Fixture, CreateControllerWritesAnEmptyRootControllerSubclassNextToTheDocument) {
    EXPECT_EQ(createController(document, "SaveDialogController", "SaveDialogController.h"), CreateControllerStatus::Created);
    const std::string text = headerOnDisk();
    EXPECT_EQ(text.rfind("#pragma once", 0), 0u);
    EXPECT_NE(text.find("#include <newui/rootcontroller.h>"), std::string::npos);
    EXPECT_EQ(text.find("#include <newui/controls.h>"), std::string::npos) << "control headers are added when wiring";
    EXPECT_NE(text.find("class SaveDialogController : public newui::RootController {"), std::string::npos) << text;
    EXPECT_NE(text.find("using RootController::RootController;"), std::string::npos);
}

TEST_F(Fixture, CreateControllerNeverOverwritesAndAdoptsAnExistingDefinition) {
    EXPECT_EQ(createController(document, "SaveDialogController", "SaveDialogController.h"), CreateControllerStatus::Created);
    const std::string created = headerOnDisk();
    EXPECT_EQ(createController(document, "SaveDialogController", "SaveDialogController.h"), CreateControllerStatus::Adopted);
    EXPECT_EQ(headerOnDisk(), created);

    writeHeader("// somebody's own file\nclass Unrelated {};\n");
    EXPECT_EQ(createController(document, "SaveDialogController", "SaveDialogController.h"), CreateControllerStatus::ExistsWithout);
    EXPECT_EQ(headerOnDisk(), "// somebody's own file\nclass Unrelated {};\n");

    writeHeader("class SaveDialogControllerTwo {};\n");   // a longer name isn't a match
    EXPECT_EQ(createController(document, "SaveDialogController", "SaveDialogController.h"), CreateControllerStatus::ExistsWithout);
}

TEST_F(Fixture, CreateControllerRefusesNamesThatAreNotIdentifiers) {
    EXPECT_EQ(createController(document, "", "A.h"), CreateControllerStatus::InvalidName);
    EXPECT_EQ(createController(document, "9Lives", "A.h"), CreateControllerStatus::InvalidName);
    EXPECT_EQ(createController(document, "Has Space", "A.h"), CreateControllerStatus::InvalidName);
    EXPECT_EQ(createController(document, "Fine", ""), CreateControllerStatus::InvalidName);
}
