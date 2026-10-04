#include "cpptools_codegen/classinsertion.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Tooling/Tooling.h>

#include <vector>

#include "quiettool.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace cpptools_codegen {

namespace {

struct Section {
    AccessSpecifier access = AS_private;
    bool hasHeader = false;
    std::string indent;   // the specifier line's own indentation
    std::string body;     // text after the specifier's colon up to the next specifier / closing brace
};

struct Spec {
    AccessSpecifier access;
    std::size_t begin;
    std::size_t afterColon;
};

class MergeCallback : public MatchFinder::MatchCallback {
public:
    void run(const MatchFinder::MatchResult& result) override {
        const CXXRecordDecl* record = result.Nodes.getNodeAs<CXXRecordDecl>("target");
        if (record == nullptr || !record->isThisDeclarationADefinition() || record->getBraceRange().isInvalid()) {
            return;
        }
        const SourceManager& sm = *result.SourceManager;
        found = true;
        openOffset = sm.getFileOffset(record->getBraceRange().getBegin());
        closeOffset = sm.getFileOffset(record->getBraceRange().getEnd());
        leadingAccess = record->isClass() ? AS_private : AS_public;

        specs.clear();
        for (const Decl* decl : record->decls()) {
            const auto* spec = llvm::dyn_cast<AccessSpecDecl>(decl);
            if (spec != nullptr && !spec->isImplicit()) {
                specs.push_back({spec->getAccess(), sm.getFileOffset(spec->getBeginLoc()),
                                 sm.getFileOffset(spec->getColonLoc()) + 1});
            }
        }
    }

    bool found = false;
    std::size_t openOffset = 0;
    std::size_t closeOffset = 0;
    AccessSpecifier leadingAccess = AS_private;
    std::vector<Spec> specs;
};

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isBlank(const std::string& text, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        if (!isSpace(text[i])) {
            return false;
        }
    }
    return true;
}

// Drops blank lines at both ends and trailing whitespace; the first kept line keeps its indentation.
std::string trimBlankLines(const std::string& text) {
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (!isBlank(text, begin, i)) {
                break;
            }
            begin = i + 1;
        }
    }
    if (isBlank(text, begin, text.size())) {
        return std::string();
    }
    std::size_t end = text.size();
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool hasPreprocessorLine(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t lineEnd = text.find('\n', i);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }
        std::size_t first = i;
        while (first < lineEnd && (text[first] == ' ' || text[first] == '\t')) {
            ++first;
        }
        if (first < lineEnd && text[first] == '#') {
            return true;
        }
        i = lineEnd + 1;
    }
    return false;
}

const char* keywordFor(AccessSpecifier access) {
    switch (access) {
    case AS_public: return "public";
    case AS_protected: return "protected";
    default: return "private";
    }
}

} // namespace

AccessMerge mergeAccessSections(const std::string& content, const std::string& className) {
    MergeCallback callback;
    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(hasName(className)).bind("target"), &callback);
    runToolOnCodeQuietly(tooling::newFrontendActionFactory(&finder)->create(), content);

    AccessMerge out;
    out.content = content;
    if (!callback.found) {
        return out;
    }

    // Cut the class body into sections at each specifier.
    std::vector<Section> sections;
    Section leading;
    leading.access = callback.leadingAccess;
    const std::size_t firstEnd = callback.specs.empty() ? callback.closeOffset : callback.specs.front().begin;
    leading.body = content.substr(callback.openOffset + 1, firstEnd - callback.openOffset - 1);
    sections.push_back(leading);
    for (std::size_t i = 0; i < callback.specs.size(); ++i) {
        const Spec& spec = callback.specs[i];
        const std::size_t end = i + 1 < callback.specs.size() ? callback.specs[i + 1].begin : callback.closeOffset;
        Section section;
        section.access = spec.access;
        section.hasHeader = true;
        std::size_t lineStart = spec.begin;
        while (lineStart > 0 && (content[lineStart - 1] == ' ' || content[lineStart - 1] == '\t')) {
            --lineStart;
        }
        section.indent = content.substr(lineStart, spec.begin - lineStart);
        section.body = content.substr(spec.afterColon, end - spec.afterColon);
        sections.push_back(std::move(section));
    }

    // An empty leading section is nothing to merge into - a later explicit specifier keeps its keyword.
    if (trimBlankLines(sections.front().body).empty()) {
        sections.erase(sections.begin());
    }

    // Unchanged when no access repeats (the leading section counts as its own access).
    bool repeats = false;
    for (std::size_t i = 0; i < sections.size() && !repeats; ++i) {
        for (std::size_t j = i + 1; j < sections.size(); ++j) {
            if (sections[i].access == sections[j].access) {
                repeats = true;
                break;
            }
        }
    }
    if (!repeats) {
        out.status = AccessMerge::Status::Unchanged;
        return out;
    }
    for (const Section& section : sections) {
        if (hasPreprocessorLine(section.body)) {
            out.status = AccessMerge::Status::Unsafe;
            return out;
        }
    }

    // One merged section per access, in order of first appearance.
    struct Merged {
        Section header;
        std::vector<std::string> bodies;
    };
    std::vector<Merged> merged;
    for (const Section& section : sections) {
        Merged* target = nullptr;
        for (Merged& m : merged) {
            if (m.header.access == section.access) {
                target = &m;
                break;
            }
        }
        if (target == nullptr) {
            merged.push_back({section, {}});
            target = &merged.back();
        }
        const std::string body = trimBlankLines(section.body);
        if (!body.empty()) {
            target->bodies.push_back(body);
        }
    }

    const std::string eol = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string rebuilt;
    bool first = true;
    for (const Merged& m : merged) {
        if (m.bodies.empty() && !m.header.hasHeader) {
            continue;
        }
        if (!first) {
            rebuilt += eol;   // a blank line between sections
        }
        first = false;
        if (m.header.hasHeader) {
            rebuilt += eol + m.header.indent + keywordFor(m.header.access) + ":";
        }
        for (std::size_t i = 0; i < m.bodies.size(); ++i) {
            rebuilt += (i == 0 ? eol : eol + eol) + m.bodies[i];
        }
    }
    rebuilt += eol;

    out.content = content.substr(0, callback.openOffset + 1) + rebuilt + content.substr(callback.closeOffset);
    out.status = AccessMerge::Status::Merged;
    return out;
}

} // namespace cpptools_codegen
