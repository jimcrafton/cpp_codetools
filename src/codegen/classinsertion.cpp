#include "cpptools_codegen/classinsertion.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Tooling/Tooling.h>

using namespace clang;
using namespace clang::ast_matchers;

namespace cpptools_codegen {

namespace {

class BraceRangeCallback : public MatchFinder::MatchCallback {
public:
    explicit BraceRangeCallback(std::optional<BraceRange>& result) : result_(result) {}

    void run(const MatchFinder::MatchResult& result) override {
        const CXXRecordDecl* record = result.Nodes.getNodeAs<CXXRecordDecl>("target");
        if (record == nullptr || !record->isThisDeclarationADefinition()) {
            return;
        }

        const SourceRange braces = record->getBraceRange();
        if (braces.isInvalid()) {
            return;
        }

        const SourceManager& sourceManager = *result.SourceManager;
        BraceRange out;
        out.openOffset = sourceManager.getFileOffset(braces.getBegin());
        out.closeOffset = sourceManager.getFileOffset(braces.getEnd());
        result_ = out;
    }

private:
    std::optional<BraceRange>& result_;
};

} // namespace

std::optional<BraceRange> classInsertionPoint(const std::string& content, const std::string& className) {
    std::optional<BraceRange> result;
    BraceRangeCallback callback(result);

    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(hasName(className)).bind("target"), &callback);

    tooling::runToolOnCode(tooling::newFrontendActionFactory(&finder)->create(), content);

    return result;
}

} // namespace cpptools_codegen
