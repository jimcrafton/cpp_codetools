#include "cpptools_codegen/classinsertion.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Tooling/Tooling.h>

#include "quiettool.h"

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

AccessSpecifier toClang(MemberAccess access) {
    switch (access) {
    case MemberAccess::Public: return AS_public;
    case MemberAccess::Protected: return AS_protected;
    default: return AS_private;
    }
}

const char* accessKeyword(MemberAccess access) {
    switch (access) {
    case MemberAccess::Public: return "public";
    case MemberAccess::Protected: return "protected";
    default: return "private";
    }
}

// Collects the end offset of the last section whose access matches, plus where the class closes.
class SectionCallback : public MatchFinder::MatchCallback {
public:
    SectionCallback(MemberAccess access, std::optional<std::size_t>& sectionEnd, std::optional<BraceRange>& braces)
        : wanted_(toClang(access)), sectionEnd_(sectionEnd), braces_(braces) {}

    void run(const MatchFinder::MatchResult& result) override {
        const CXXRecordDecl* record = result.Nodes.getNodeAs<CXXRecordDecl>("target");
        if (record == nullptr || !record->isThisDeclarationADefinition() || record->getBraceRange().isInvalid()) {
            return;
        }

        const SourceManager& sourceManager = *result.SourceManager;
        BraceRange out;
        out.openOffset = sourceManager.getFileOffset(record->getBraceRange().getBegin());
        out.closeOffset = sourceManager.getFileOffset(record->getBraceRange().getEnd());
        braces_ = out;
        sectionEnd_.reset();

        AccessSpecifier current = record->isClass() ? AS_private : AS_public;
        bool leadingHasMembers = false;
        bool inMatching = false;
        bool leading = true;
        for (const Decl* decl : record->decls()) {
            if (decl->isImplicit()) {
                continue;
            }
            if (const auto* spec = llvm::dyn_cast<AccessSpecDecl>(decl)) {
                if (inMatching) {
                    sectionEnd_ = sourceManager.getFileOffset(spec->getBeginLoc());
                } else if (leading && current == wanted_ && leadingHasMembers) {
                    sectionEnd_ = sourceManager.getFileOffset(spec->getBeginLoc());
                }
                leading = false;
                current = spec->getAccess();
                inMatching = current == wanted_;
                if (inMatching) {
                    sectionEnd_ = out.closeOffset;
                }
            } else if (leading) {
                leadingHasMembers = true;
            }
        }
        if (leading && current == wanted_ && leadingHasMembers) {
            sectionEnd_ = out.closeOffset;
        }
    }

private:
    AccessSpecifier wanted_;
    std::optional<std::size_t>& sectionEnd_;
    std::optional<BraceRange>& braces_;
};

} // namespace

std::optional<MemberInsertion> memberInsertionPoint(const std::string& content, const std::string& className,
                                                      MemberAccess access) {
    std::optional<std::size_t> sectionEnd;
    std::optional<BraceRange> braces;
    SectionCallback callback(access, sectionEnd, braces);

    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(hasName(className)).bind("target"), &callback);
    runToolOnCodeQuietly(tooling::newFrontendActionFactory(&finder)->create(), content);

    if (!braces.has_value()) {
        return std::nullopt;
    }

    MemberInsertion insertion;
    if (!sectionEnd.has_value()) {
        insertion.offset = braces->closeOffset;
        insertion.prefix = std::string("\n") + accessKeyword(access) + ":\n";
        return insertion;
    }

    // Insert at the start of the line that ends the section (the next specifier or the closing
    // brace), after the section's last member.
    std::size_t lineStart = *sectionEnd;
    while (lineStart > 0 && (content[lineStart - 1] == ' ' || content[lineStart - 1] == '\t')) {
        --lineStart;
    }
    const bool ownLine = lineStart == 0 || content[lineStart - 1] == '\n';
    insertion.offset = ownLine ? lineStart : *sectionEnd;

    bool blankBefore = false;
    if (ownLine && lineStart >= 2) {
        std::size_t p = lineStart - 1;
        if (p > 0 && content[p - 1] == '\r') {
            --p;
        }
        blankBefore = p > 0 && content[p - 1] == '\n';
    }
    if (!ownLine || !blankBefore) {
        insertion.prefix = "\n";
    }
    if (*sectionEnd != braces->closeOffset) {
        insertion.suffix = "\n";
    }
    return insertion;
}

std::optional<BraceRange> classInsertionPoint(const std::string& content, const std::string& className) {
    std::optional<BraceRange> result;
    BraceRangeCallback callback(result);

    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(hasName(className)).bind("target"), &callback);

    runToolOnCodeQuietly(tooling::newFrontendActionFactory(&finder)->create(), content);

    return result;
}

} // namespace cpptools_codegen
