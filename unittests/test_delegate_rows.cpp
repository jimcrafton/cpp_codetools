#include "../extension/NativeEditControls/PropertiesModel.h"

#include <newui/controls.h>
#include <newui/reflection.h>

#include <gtest/gtest.h>

// registerReflectionData() is already run once for this whole binary by test_component_editor.cpp's
// ::testing::Environment, so the real newui classes are registered here.

using CodeToolsVsix::PropertiesModel;
using cpptools::BindingCheck;
using cpptools::BindingStatus;

namespace {

PropertiesModel::Node delegateNode(PropertiesModel& model, const char* delegateName) {
    const std::size_t rootChildren = model.childCount(std::vector<std::size_t>{});
    for (std::size_t i = 0; i < rootChildren; ++i) {
        if (model.nodeAt({i}).kind != PropertiesModel::Kind::DelegatesHeader) {
            continue;
        }
        const std::size_t count = model.childCount({i});
        for (std::size_t j = 0; j < count; ++j) {
            PropertiesModel::Node node = model.nodeAt({i, j});
            if (node.delegate != nullptr && node.delegate->name() == delegateName) {
                return node;
            }
        }
    }
    return {};
}

void record(newui::Button& button, const char* descriptor) {
    const newui::reflection::Class* clazz = newui::reflection::classinfo(std::string("Button"));
    ASSERT_NE(clazz, nullptr);
    std::vector<const newui::reflection::Delegate*> delegates;
    clazz->allDelegates(delegates);
    for (const auto* d : delegates) {
        if (d->name() == "onClick") {
            ASSERT_TRUE(d->addDescriptorListener(&button, descriptor));
            return;
        }
    }
    FAIL() << "no onClick delegate";
}

class DelegateRowText : public ::testing::Test {
protected:
    void SetUp() override {
        button.setName("saveButton");
        model.setSelection(&button);
        node = delegateNode(model, "onClick");
        ASSERT_NE(node.delegate, nullptr);
    }

    // A presenter answering every recorded handler with `status`.
    void answer(BindingStatus status) {
        PropertiesModel::DelegatePresenter presenter;
        presenter.status = [status](newui::Component*, const std::string&, const std::string&) {
            return std::optional<BindingCheck>(BindingCheck{status, "detail"});
        };
        model.setDelegatePresenter(std::move(presenter));
    }

    newui::Button button;
    PropertiesModel model;
    PropertiesModel::Node node;
};

}  // namespace

TEST(HandlerLabel, IsTheMethodForTheControllerAndObjectDotMethodForAMember) {
    EXPECT_EQ(PropertiesModel::handlerLabelOf("this@SaveDialogController.onSaveButtonClick"), "onSaveButtonClick");
    EXPECT_EQ(PropertiesModel::handlerLabelOf("presenter_@Presenter.onSave"), "presenter_.onSave");
}

TEST(HandlerLabel, LeavesAnythingThatIsNotObjectAtClassMethodAsItIs) {
    for (const char* other : {"someFreeFunction", "a@b", "@C.m", "o@C.", ""}) {
        EXPECT_EQ(PropertiesModel::handlerLabelOf(other), other);
    }
}

TEST(StatusPhrase, EveryProblemHasWordsAndOkHasNone) {
    EXPECT_TRUE(PropertiesModel::statusPhrase(BindingStatus::Ok).empty());
    for (BindingStatus s : {BindingStatus::ControllerMissing, BindingStatus::ObjectMissing, BindingStatus::MethodMissing,
                            BindingStatus::HeaderErrors, BindingStatus::SignatureMismatch, BindingStatus::NotWired,
                            BindingStatus::CannotVerify}) {
        EXPECT_FALSE(PropertiesModel::statusPhrase(s).empty()) << static_cast<int>(s);
    }
}

TEST_F(DelegateRowText, AnEventWithNothingRecordedSaysSoByDefault) {
    EXPECT_EQ(model.delegateRowText(node), "(no listeners)");
}

TEST_F(DelegateRowText, ThePresentersHintReplacesTheDefaultButABlankOneDoesNot) {
    PropertiesModel::DelegatePresenter presenter;
    presenter.emptyText = [] { return std::string("(double-click to add a handler)"); };
    model.setDelegatePresenter(presenter);
    EXPECT_EQ(model.delegateRowText(node), "(double-click to add a handler)");

    presenter.emptyText = [] { return std::string(); };
    model.setDelegatePresenter(presenter);
    EXPECT_EQ(model.delegateRowText(node), "(no listeners)");
}

TEST_F(DelegateRowText, ARecordedHandlerShowsByNameWithNoPresenterAtAll) {
    record(button, "this@SaveDialogController.onSaveButtonClick");
    EXPECT_EQ(model.delegateRowText(node), "onSaveButtonClick");
}

TEST_F(DelegateRowText, AHandlerThatCheckedOutOrWasNeverCheckedShowsPlain) {
    record(button, "this@SaveDialogController.onSaveButtonClick");
    answer(BindingStatus::Ok);
    EXPECT_EQ(model.delegateRowText(node), "onSaveButtonClick");

    PropertiesModel::DelegatePresenter unchecked;
    unchecked.status = [](newui::Component*, const std::string&, const std::string&) { return std::optional<BindingCheck>(); };
    model.setDelegatePresenter(unchecked);
    EXPECT_EQ(model.delegateRowText(node), "onSaveButtonClick");
}

TEST_F(DelegateRowText, AProblemIsMarkedAndSaidInWords) {
    record(button, "this@SaveDialogController.onGone");
    answer(BindingStatus::MethodMissing);
    EXPECT_EQ(model.delegateRowText(node), "! onGone (method missing)");
    answer(BindingStatus::SignatureMismatch);
    EXPECT_EQ(model.delegateRowText(node), "! onGone (signature changed)");
    answer(BindingStatus::NotWired);
    EXPECT_EQ(model.delegateRowText(node), "! onGone (not wired)");
    answer(BindingStatus::HeaderErrors);
    EXPECT_EQ(model.delegateRowText(node), "! onGone (header doesn't compile)");
}

TEST_F(DelegateRowText, SomethingThatCouldNotBeCheckedGetsAQuestionMarkNotAWarning) {
    record(button, "presenter_@Presenter.onSave");
    answer(BindingStatus::CannotVerify);
    EXPECT_EQ(model.delegateRowText(node), "? presenter_.onSave (can't verify)");
}

TEST_F(DelegateRowText, ThePresenterIsAskedAboutTheRightControlEventAndDescriptor) {
    record(button, "this@C.m");
    std::string seenOwner, seenDelegate, seenDescriptor;
    PropertiesModel::DelegatePresenter presenter;
    presenter.status = [&](newui::Component* owner, const std::string& delegateName, const std::string& descriptor) {
        seenOwner = static_cast<newui::SubView*>(owner)->name();
        seenDelegate = delegateName;
        seenDescriptor = descriptor;
        return std::optional<BindingCheck>();
    };
    model.setDelegatePresenter(presenter);
    model.delegateRowText(node);
    EXPECT_EQ(seenOwner, "saveButton");
    EXPECT_EQ(seenDelegate, "onClick");
    EXPECT_EQ(seenDescriptor, "this@C.m");
}
