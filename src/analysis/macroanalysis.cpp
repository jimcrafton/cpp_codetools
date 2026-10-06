#include "cpptools_analysis/macroanalysis.h"

#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>

#include <algorithm>
#include <cctype>
#include <map>

#include "analysistool.h"

using namespace clang;

namespace cpptools_analysis {

namespace {

std::string joinTokens(Preprocessor& pp, const Token* first, const Token* end) {
    std::string text;
    for (const Token* tok = first; tok != end; ++tok) {
        if (!text.empty()) text += ' ';
        text += pp.getSpelling(*tok);
    }
    return text;
}

// What an argument does that makes evaluating it twice a problem, or empty.
std::string sideEffectOf(Preprocessor& pp, const Token* tokens) {
    for (const Token* tok = tokens; tok->isNot(tok::eof); ++tok) {
        switch (tok->getKind()) {
            case tok::plusplus: return "contains ++";
            case tok::minusminus: return "contains --";
            case tok::equal: case tok::plusequal: case tok::minusequal: case tok::starequal: case tok::slashequal:
                return "assigns";
            default: break;
        }
        if (tok->is(tok::identifier) && (tok + 1)->is(tok::l_paren)) return "calls " + pp.getSpelling(*tok);
    }
    return std::string();
}

// The parameters of a function-like macro that its replacement list mentions more than once, not counting # and ##.
std::vector<std::string> repeatedParamsOf(const MacroInfo& info) {
    std::vector<std::string> repeated;
    const auto tokens = info.tokens();
    for (const IdentifierInfo* param : info.params()) {
        int mentions = 0;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            if (!tokens[i].is(tok::identifier) || tokens[i].getIdentifierInfo() != param) continue;
            const bool stringized = i > 0 && tokens[i - 1].is(tok::hash);
            const bool pasted = (i > 0 && tokens[i - 1].is(tok::hashhash)) || (i + 1 < tokens.size() && tokens[i + 1].is(tok::hashhash));
            if (!stringized && !pasted) ++mentions;
        }
        if (mentions > 1) repeated.push_back(param->getName().str());
    }
    return repeated;
}

// --- stepwise expansion, on text -------------------------------------------------------------------------------

struct Piece {
    std::string text;
    bool space = false;   // whitespace before it
    std::string from;     // the macro whose replacement list wrote it; empty if the use itself did
};

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Just enough of a lexer for macro text: identifiers, numbers, string and character literals, punctuation.
std::vector<Piece> piecesOf(const std::string& text) {
    static const char* const kPairs[] = { "##", "++", "--", "->", "<<", ">>", "&&", "||", "==", "!=", "<=", ">=", "::", "+=", "-=", "*=", "/=" };
    std::vector<Piece> pieces;
    bool space = false;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) { space = true; ++i; continue; }
        std::size_t end = i + 1;
        if (isIdentStart(c)) {
            while (end < text.size() && isIdentChar(text[end])) ++end;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))) {
            while (end < text.size() && (isIdentChar(text[end]) || text[end] == '.' ||
                   ((text[end] == '+' || text[end] == '-') && (text[end - 1] == 'e' || text[end - 1] == 'E')))) ++end;
        } else if (c == '"' || c == '\'') {
            while (end < text.size() && text[end] != c) end += text[end] == '\\' ? 2 : 1;
            end = std::min(end + 1, text.size());
        } else {
            for (const char* pair : kPairs) {
                if (text.compare(i, 2, pair) == 0) { end = i + 2; break; }
            }
        }
        pieces.push_back(Piece{ text.substr(i, end - i), space });
        space = false;
        i = end;
    }
    return pieces;
}

std::string joined(const std::vector<Piece>& pieces) {
    std::string text;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (i > 0 && pieces[i].space) text += ' ';
        text += pieces[i].text;
    }
    return text;
}

// The text of `info`'s replacement list with `args` put in for its parameters.
std::vector<Piece> substitute(Preprocessor& pp, const MacroInfo& info, const std::string& macro, const std::vector<std::vector<Piece>>& args) {
    std::vector<Piece> out;
    bool paste = false;
    const auto tokens = info.tokens();
    auto paramOf = [&](const Token& tok) { return tok.is(tok::identifier) ? info.getParameterNum(tok.getIdentifierInfo()) : -1; };
    auto argOf = [&](int param) { return static_cast<std::size_t>(param) < args.size() ? args[param] : std::vector<Piece>(); };
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& tok = tokens[i];
        bool space = tok.hasLeadingSpace();
        if (tok.is(tok::hashhash)) { paste = true; continue; }
        if (paste) space = false;
        std::vector<Piece> piece;
        if (tok.is(tok::hash) && info.isFunctionLike() && i + 1 < tokens.size() && paramOf(tokens[i + 1]) >= 0) {
            std::string quoted = "\"";
            for (char c : joined(argOf(paramOf(tokens[i + 1])))) {
                if (c == '"' || c == '\\') quoted += '\\';
                quoted += c;
            }
            piece.push_back(Piece{ quoted + "\"", space, macro });
            ++i;
        } else if (paramOf(tok) >= 0) {
            piece = argOf(paramOf(tok));
            if (!piece.empty()) piece.front().space = space;
        } else {
            piece.push_back(Piece{ pp.getSpelling(tok), space, macro });
        }
        if (paste && !piece.empty() && !out.empty()) piece.front().space = false;
        for (Piece& p : piece) out.push_back(std::move(p));
        paste = false;
    }
    return out;
}

// What the built-in macros stand for at the use being expanded.
struct UseSite {
    std::string file;
    unsigned line = 0;
};

// Expands the macro written at pieces[at] in place. How many pieces replaced it, or -1 if it is not a use of a macro.
int expandAt(Preprocessor& pp, std::vector<Piece>& pieces, std::size_t at, const UseSite& site) {
    if (!isIdentStart(pieces[at].text[0])) return -1;
    const std::string macro = pieces[at].text;
    IdentifierInfo* name = pp.getIdentifierInfo(macro);
    const MacroInfo* info = pp.getMacroInfo(name);
    if (info == nullptr) return -1;

    if (info->isBuiltinMacro()) {
        std::string value;
        if (pieces[at].text == "__FILE__") {
            value = "\"";
            for (char c : site.file) {
                if (c == '"' || c == '\\') value += '\\';
                value += c;
            }
            value += '"';
        } else if (pieces[at].text == "__LINE__") {
            value = std::to_string(site.line);
        } else {
            return -1;   // __DATE__, __COUNTER__ ...: left as written
        }
        const bool space = pieces[at].space;
        pieces[at] = Piece{ value, space, macro };
        return 1;
    }

    std::size_t end = at + 1;
    std::vector<std::vector<Piece>> args;
    if (info->isFunctionLike()) {
        if (end >= pieces.size() || pieces[end].text != "(") return -1;
        int depth = 0;
        std::vector<Piece> current;
        bool closed = false;
        for (; end < pieces.size(); ++end) {
            const std::string& t = pieces[end].text;
            if (t == "(") {
                if (depth++ == 0) continue;
            } else if (t == ")") {
                if (--depth == 0) { closed = true; ++end; break; }
            } else if (t == "," && depth == 1 && !(info->isVariadic() && args.size() + 1 >= info->params().size())) {
                args.push_back(std::move(current));
                current.clear();
                continue;
            }
            current.push_back(pieces[end]);
            if (current.size() == 1) current.front().space = false;
        }
        if (!closed) return -1;
        if (!current.empty() || info->params().size() > 0) args.push_back(std::move(current));
    }
    std::vector<Piece> body = substitute(pp, *info, macro, args);
    if (!body.empty()) body.front().space = pieces[at].space;
    const int count = static_cast<int>(body.size());
    pieces.erase(pieces.begin() + at, pieces.begin() + end);
    pieces.insert(pieces.begin() + at, body.begin(), body.end());
    return count;
}

// What a macro is and where it was defined, read from the preprocessor.
MacroTreeNode describeMacro(Preprocessor& pp, const std::string& name) {
    MacroTreeNode node;
    node.name = name;
    node.origin = MacroOrigin::BuiltIn;
    const MacroInfo* info = pp.getMacroInfo(pp.getIdentifierInfo(name));
    if (info == nullptr || info->isBuiltinMacro()) return node;
    SourceManager& sm = pp.getSourceManager();
    const SourceLocation at = info->getDefinitionLoc();
    const PresumedLoc presumed = sm.getPresumedLoc(at);
    if (presumed.isValid()) node.definedLine = presumed.getLine();
    if (sm.isWrittenInMainFile(at)) {
        node.origin = MacroOrigin::File;
        node.definedHere = true;
        return node;
    }
    const std::string where = presumed.isValid() ? presumed.getFilename() : std::string();
    if (where == "<command line>") node.origin = MacroOrigin::CommandLine;
    else if (where == "<built-in>" || where.empty()) node.origin = MacroOrigin::BuiltIn;
    else { node.origin = MacroOrigin::Header; node.definedIn = where; }
    return node;
}

// `use.written` expanded a macro at a time: the leftmost one that can be, every place it is written. Fills the
// steps (the first is the text itself), the tree of which macro's replacement list brought in which, and the
// final text split by the macro that wrote each part.
void expandUse(Preprocessor& pp, MacroUseInfo& use, const UseSite& site) {
    std::vector<Piece> pieces = piecesOf(use.written);
    use.steps.push_back(MacroStep{ std::string(), joined(pieces) });

    struct Flat { MacroTreeNode node; int parent; };
    std::vector<Flat> flat;
    auto record = [&](const std::string& name, const std::string& from) {
        int parent = -1;
        if (!flat.empty()) {
            parent = 0;
            for (std::size_t k = 0; k < flat.size(); ++k) {
                if (flat[k].node.name == from) { parent = static_cast<int>(k); break; }
            }
        }
        for (const Flat& f : flat) {
            if (f.parent == parent && f.node.name == name) return;
        }
        flat.push_back(Flat{ describeMacro(pp, name), parent });
    };

    for (int round = 0; round < 24; ++round) {
        bool expanded = false;
        for (std::size_t i = 0; i < pieces.size() && !expanded; ++i) {
            const std::string name = pieces[i].text;
            const std::string from = pieces[i].from;
            int count = expandAt(pp, pieces, i, site);
            if (count < 0) continue;
            record(name, from);
            for (std::size_t j = i + static_cast<std::size_t>(count); j < pieces.size();) {
                const std::string again = pieces[j].from;
                count = pieces[j].text == name ? expandAt(pp, pieces, j, site) : -1;
                if (count >= 0) record(name, again);
                j += count >= 0 ? static_cast<std::size_t>(std::max(count, 1)) : 1;
            }
            use.steps.push_back(MacroStep{ name, joined(pieces) });
            expanded = true;
        }
        if (!expanded) break;
    }

    for (std::size_t k = flat.size(); k-- > 1;) {
        flat[static_cast<std::size_t>(flat[k].parent)].node.children.insert(flat[static_cast<std::size_t>(flat[k].parent)].node.children.begin(),
                                                                          std::move(flat[k].node));
    }
    if (!flat.empty()) use.tree = std::move(flat.front().node);
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        use.spans.push_back(MacroSpan{ (k > 0 && pieces[k].space ? " " : "") + pieces[k].text, pieces[k].from });
    }
}

class Collector : public PPCallbacks {
public:
    Collector(Preprocessor& pp, MacroAnalysis& out) : pp_(pp), sm_(pp.getSourceManager()), out_(out) {}

    // outermost use start (raw location) -> index in out_.uses, to attach the expansion text to
    std::map<SourceLocation::UIntTy, std::size_t> useAt;
    std::vector<InactiveRegion> regions;

    void MacroDefined(const Token& nameTok, const MacroDirective* directive) override {
        if (!sm_.isWrittenInMainFile(nameTok.getLocation())) return;
        const MacroInfo* info = directive->getMacroInfo();
        MacroDefinitionInfo def;
        def.name = nameTok.getIdentifierInfo()->getName().str();
        def.line = sm_.getSpellingLineNumber(nameTok.getLocation());
        def.functionLike = info->isFunctionLike();
        for (const IdentifierInfo* param : info->params()) def.params.push_back(param->getName().str());
        def.body = joinTokens(pp_, info->tokens_begin(), info->tokens_end());
        def.repeatedParams = repeatedParamsOf(*info);
        definedAt_[def.name] = out_.definitions.size();
        out_.definitions.push_back(std::move(def));
    }

    void MacroExpands(const Token& nameTok, const MacroDefinition& definition, SourceRange range, const MacroArgs* args) override {
        const std::string name = nameTok.getIdentifierInfo()->getName().str();
        if (range.getBegin().isMacroID()) {   // written inside another macro's body
            if (inOuter_) out_.uses.back().nested.push_back(name);
            return;
        }
        if (!sm_.isWrittenInMainFile(range.getBegin())) {
            inOuter_ = false;
            return;
        }
        const unsigned offset = sm_.getFileOffset(range.getBegin());
        if (inOuter_ && offset < outerEnd_) {   // inside an outer use's arguments
            out_.uses.back().nested.push_back(name);
            return;
        }

        MacroUseInfo use;
        use.name = name;
        use.line = sm_.getSpellingLineNumber(range.getBegin());
        use.column = sm_.getSpellingColumnNumber(range.getBegin());
        use.written = Lexer::getSourceText(CharSourceRange::getTokenRange(range), sm_, pp_.getLangOpts()).str();
        auto defined = definedAt_.find(name);
        if (defined != definedAt_.end()) {
            use.definedHere = true;
            use.definedLine = out_.definitions[defined->second].line;
        }

        const MacroInfo* info = definition.getMacroInfo();
        if (info != nullptr) {
            const SourceLocation at = info->getDefinitionLoc();
            if (sm_.isWrittenInMainFile(at)) {
                use.origin = MacroOrigin::File;
            } else {
                const PresumedLoc presumed = sm_.getPresumedLoc(at);
                const std::string where = presumed.isValid() ? presumed.getFilename() : std::string();
                if (where == "<command line>") use.origin = MacroOrigin::CommandLine;
                else if (where == "<built-in>" || where.empty()) use.origin = MacroOrigin::BuiltIn;
                else { use.origin = MacroOrigin::Header; use.definedIn = where; }
            }
        }
        const PresumedLoc here = sm_.getPresumedLoc(range.getBegin());
        expandUse(pp_, use, UseSite{ here.isValid() ? here.getFilename() : "", static_cast<unsigned>(use.line) });
        if (info != nullptr && info->isFunctionLike() && args != nullptr) {
            const std::vector<std::string> repeated = repeatedParamsOf(*info);
            const auto params = info->params();
            for (unsigned i = 0; i < args->getNumMacroArguments() && i < params.size(); ++i) {
                const std::string param = params[i]->getName().str();
                if (std::find(repeated.begin(), repeated.end(), param) == repeated.end()) continue;
                const Token* argTokens = args->getUnexpArgument(i);
                const std::string reason = sideEffectOf(pp_, argTokens);
                if (reason.empty()) continue;
                MacroWarning warning;
                warning.line = use.line;
                warning.macro = name;
                warning.param = param;
                warning.argument = joinTokens(pp_, argTokens, argTokens + MacroArgs::getArgLength(argTokens));
                warning.reason = reason;
                out_.warnings.push_back(std::move(warning));
            }
        }

        inOuter_ = true;
        outerEnd_ = sm_.getFileOffset(range.getEnd()) + 1;
        useAt[range.getBegin().getRawEncoding()] = out_.uses.size();
        out_.uses.push_back(std::move(use));
    }

    void SourceRangeSkipped(SourceRange range, SourceLocation) override {
        if (!sm_.isWrittenInMainFile(range.getBegin())) return;
        InactiveRegion region;
        region.startLine = sm_.getSpellingLineNumber(range.getBegin());
        region.endLine = sm_.getSpellingLineNumber(range.getEnd());
        regions.push_back(region);
    }

private:
    Preprocessor& pp_;
    SourceManager& sm_;
    MacroAnalysis& out_;
    std::map<std::string, std::size_t> definedAt_;
    bool inOuter_ = false;
    unsigned outerEnd_ = 0;
};

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    const std::size_t last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

// The word after the '#' of a directive line ("if", "ifdef", "endif"), or empty if the line is not one.
std::string directiveWord(const std::string& line) {
    const std::string text = trimmed(line);
    if (text.empty() || text[0] != '#') return std::string();
    std::size_t at = 1;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    std::size_t end = at;
    while (end < text.size() && isIdentChar(text[end])) ++end;
    return text.substr(at, end - at);
}

// Adds the identifiers a condition line names (not the directive word, `defined`, or what is in a comment).
void addConditionMacros(const std::string& line, std::vector<std::string>& macros) {
    std::string text = line;
    const std::size_t comment = text.find("//");
    if (comment != std::string::npos) text.resize(comment);
    const std::string directive = directiveWord(text);
    std::size_t at = text.find('#');
    at = at == std::string::npos ? 0 : text.find(directive, at) + directive.size();
    static const char* const kSkip[] = { "defined", "__has_include", "__has_include_next", "__has_cpp_attribute", "__has_feature",
                                         "__has_builtin", "true", "false", "and", "or", "not" };
    while (at < text.size()) {
        if (!isIdentStart(text[at])) { ++at; continue; }
        std::size_t end = at;
        while (end < text.size() && isIdentChar(text[end])) ++end;
        const std::string name = text.substr(at, end - at);
        at = end;
        bool skip = false;
        for (const char* word : kSkip) skip = skip || name == word;
        if (!skip && std::find(macros.begin(), macros.end(), name) == macros.end()) macros.push_back(name);
    }
}

// The macros behind the region whose opening directive is on 1-based line `directiveLine`: that line's own condition and,
// for an #else / #elif, the conditions before it in the same #if chain.
std::vector<std::string> conditionMacros(const std::vector<std::string>& lines, std::size_t directiveLine) {
    std::vector<std::string> macros;
    if (directiveLine == 0 || directiveLine > lines.size()) return macros;
    const std::string first = directiveWord(lines[directiveLine - 1]);
    if (first == "if" || first == "ifdef" || first == "ifndef" || first == "elif") addConditionMacros(lines[directiveLine - 1], macros);
    if (first != "else" && first != "elif") return macros;
    int depth = 0;
    for (std::size_t line = directiveLine - 1; line >= 1; --line) {
        const std::string word = directiveWord(lines[line - 1]);
        if (word == "endif") {
            ++depth;
        } else if (word == "if" || word == "ifdef" || word == "ifndef") {
            if (depth == 0) {
                addConditionMacros(lines[line - 1], macros);
                break;
            }
            --depth;
        } else if (word == "elif" && depth == 0) {
            addConditionMacros(lines[line - 1], macros);
        }
    }
    return macros;
}

class MacroAction : public PreprocessorFrontendAction {
public:
    MacroAction(MacroAnalysis& out, const std::string& content) : out_(out), content_(content) {}

protected:
    void ExecuteAction() override {
        Preprocessor& pp = getCompilerInstance().getPreprocessor();
        SourceManager& sm = pp.getSourceManager();
        auto collector = std::make_unique<Collector>(pp, out_);
        Collector& seen = *collector;
        pp.addPPCallbacks(std::move(collector));

        // Every token that came out of a macro, grouped by where the outermost expansion started.
        std::map<SourceLocation::UIntTy, std::string> expanded;
        pp.EnterMainSourceFile();
        Token tok;
        do {
            pp.Lex(tok);
            if (!tok.getLocation().isMacroID()) continue;
            std::string& text = expanded[sm.getExpansionLoc(tok.getLocation()).getRawEncoding()];
            if (!text.empty()) text += ' ';
            text += pp.getSpelling(tok);
        } while (tok.isNot(tok::eof));

        for (const auto& use : seen.useAt) {
            auto found = expanded.find(use.first);
            if (found != expanded.end()) out_.uses[use.second].expansion = found->second;
        }

        // The directive that opened each skipped region: the line the range starts on.
        std::vector<std::string> lines;
        for (std::size_t from = 0; from <= content_.size();) {
            const std::size_t end = content_.find('\n', from);
            lines.push_back(content_.substr(from, end == std::string::npos ? std::string::npos : end - from));
            if (end == std::string::npos) break;
            from = end + 1;
        }
        for (InactiveRegion region : seen.regions) {
            for (std::size_t line = std::min(region.startLine, lines.size()); line >= 1; --line) {
                const std::string text = trimmed(lines[line - 1]);
                if (!text.empty() && text[0] == '#') {
                    region.directive = text;
                    region.macros = conditionMacros(lines, line);
                    break;
                }
            }
            // clang's range runs from the opening directive to the one that closes the region: the skipped lines are between
            region.startLine += 1;
            if (region.endLine > region.startLine) region.endLine -= 1;
            else region.endLine = region.startLine;
            out_.inactive.push_back(std::move(region));
        }
    }

private:
    MacroAnalysis& out_;
    const std::string& content_;
};

}  // namespace

MacroAnalysis analyzeMacros(const std::string& content, const std::string& path, const std::vector<std::string>& args) {
    MacroAnalysis result;
    result.ok = runAnalysisTool(std::make_unique<MacroAction>(result, content), content, path, args);
    return result;
}

}  // namespace cpptools_analysis
