#include "cpptools_codegen/fieldinsertion.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Tooling/Tooling.h>

#include "cpptools_codegen/classinsertion.h"
#include "quiettool.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace cpptools_codegen {

namespace {

class FoundCallback : public MatchFinder::MatchCallback {
public:
    explicit FoundCallback(bool& found) : found_(found) {}
    void run(const MatchFinder::MatchResult&) override { found_ = true; }

private:
    bool& found_;
};

} // namespace

bool classHasMember(const std::string& content, const std::string& className, const std::string& memberName) {
    bool found = false;
    FoundCallback callback(found);

    MatchFinder finder;
    finder.addMatcher(namedDecl(hasName(memberName), hasParent(cxxRecordDecl(hasName(className)))).bind("member"),
                      &callback);
    runToolOnCodeQuietly(tooling::newFrontendActionFactory(&finder)->create(), content);
    return found;
}

std::string connectFieldDeclaration(const std::string& type, const std::string& name, const std::string& indent) {
    return indent + "//@reflect connect=true\n" + indent + type + "* " + name + " = nullptr;\n";
}

std::optional<DelegateWiringEdit> planConnectField(const std::string& content, const std::string& className,
                                                    const std::string& type, const std::string& name) {
    const std::optional<MemberInsertion> where = memberInsertionPoint(content, className, MemberAccess::Private);
    if (!where.has_value() || classHasMember(content, className, name)) {
        return std::nullopt;
    }
    return DelegateWiringEdit{where->offset, where->prefix + connectFieldDeclaration(type, name) + where->suffix};
}

} // namespace cpptools_codegen
