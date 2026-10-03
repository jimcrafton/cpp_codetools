#include "cpptools_codegen/methodinsertion.h"

#include <clang/AST/Stmt.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Tooling/Tooling.h>

#include "quiettool.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace cpptools_codegen {

namespace {

class InsertionOffsetCallback : public MatchFinder::MatchCallback {
public:
    explicit InsertionOffsetCallback(std::optional<std::size_t>& result) : result_(result) {}

    void run(const MatchFinder::MatchResult& result) override {
        const CXXMethodDecl* method = result.Nodes.getNodeAs<CXXMethodDecl>("target");
        if (method == nullptr || !method->hasBody()) {
            return;
        }

        const auto* body = llvm::dyn_cast<CompoundStmt>(method->getBody());
        if (body == nullptr) {
            return;
        }

        SourceLocation insertLoc = body->getRBracLoc();
        if (!body->body_empty()) {
            if (const auto* lastReturn = llvm::dyn_cast<ReturnStmt>(body->body_back())) {
                insertLoc = lastReturn->getBeginLoc();
            }
        }

        result_ = result.SourceManager->getFileOffset(insertLoc);
    }

private:
    std::optional<std::size_t>& result_;
};

} // namespace

std::optional<std::size_t> functionBodyInsertionPoint(const std::string& content, const std::string& className,
                                                        const std::string& methodName) {
    std::optional<std::size_t> result;
    InsertionOffsetCallback callback(result);

    MatchFinder finder;
    finder.addMatcher(
        cxxMethodDecl(hasName(methodName), ofClass(hasName(className))).bind("target"),
        &callback);

    runToolOnCodeQuietly(tooling::newFrontendActionFactory(&finder)->create(), content);

    return result;
}

} // namespace cpptools_codegen
