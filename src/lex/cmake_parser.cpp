#include <lex/cmake_parser.h>

#include <lex/cmake_lexer.h>

#include <algorithm>

namespace lex {
namespace cmake {

namespace {

std::wstring lowered(std::wstring_view text) {
    std::wstring out(text);
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return out;
}

bool isOpener(const std::wstring& lower) {
    return lower == L"if" || lower == L"foreach" || lower == L"while" || lower == L"function" ||
           lower == L"macro" || lower == L"block";
}

bool isCloser(const std::wstring& lower) {
    return lower == L"endif" || lower == L"endforeach" || lower == L"endwhile" || lower == L"endfunction" ||
           lower == L"endmacro" || lower == L"endblock";
}

// The opener a closer ends ("endforeach" -> "foreach").
std::wstring openerOf(const std::wstring& closerLower) { return closerLower.substr(3); }

}  // namespace

bool Argument::isDynamic() const noexcept {
    if (kind == ArgKind::Bracket || kind == ArgKind::Paren) return false;   // a bracket argument is literal
    return value.find(L"${") != std::wstring::npos || value.find(L"$<") != std::wstring::npos ||
           value.find(L"$ENV{") != std::wstring::npos || value.find(L"$CACHE{") != std::wstring::npos;
}

bool Command::is(std::wstring_view lowerName) const noexcept {
    if (name.size() != lowerName.size()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        wchar_t c = name[i];
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        if (c != lowerName[i]) return false;
    }
    return true;
}

bool Command::opensBlock() const noexcept { return isOpener(lowered(name)); }
bool Command::closesBlock() const noexcept { return isCloser(lowered(name)); }

std::vector<std::wstring> ParseResult::enclosing(const Command& command) const {
    std::vector<std::wstring> openers;
    for (int at = command.parent; at >= 0 && at < static_cast<int>(commands.size()); at = commands[static_cast<std::size_t>(at)].parent) {
        openers.push_back(lowered(commands[static_cast<std::size_t>(at)].name));
    }
    return openers;
}

ParseResult parse(std::wstring_view source) {
    ParseResult result;
    CMakeLexer lexer(source);
    const std::vector<Token> tokens = lexer.tokenize();

    auto errorAt = [&result](const Token& token, const wchar_t* message) {
        ParseError error;
        error.offset = token.offset;
        error.length = token.length;
        error.line = token.line;
        error.column = token.column;
        error.message = message;
        result.errors.push_back(std::move(error));
    };
    auto textOf = [source](const Token& token) { return source.substr(token.offset, token.length); };

    std::vector<int> open;   // indexes of the block openers still waiting for their end
    std::size_t i = 0;
    auto skipTrivia = [&](std::size_t from) {
        while (from < tokens.size() && (isTrivia(tokens[from].cls) || tokens[from].cls == TokenClass::Comment)) {
            if (tokens[from].cls == TokenClass::Comment) {
                Comment comment;
                comment.offset = tokens[from].offset;
                comment.end = tokens[from].end();
                comment.line = tokens[from].line;
                comment.bracket = tokens[from].kind == kind::BlockComment;
                result.comments.push_back(comment);
            }
            ++from;
        }
        return from;
    };

    while (i < tokens.size()) {
        const Token& token = tokens[i];
        if (isTrivia(token.cls) || token.cls == TokenClass::Comment) {
            i = skipTrivia(i);
            continue;
        }
        if (token.kind != cmake::CommandName) {
            errorAt(token, L"expected a command name");
            ++i;
            continue;
        }

        Command command;
        command.name = std::wstring(textOf(token));
        command.offset = token.offset;
        command.nameEnd = token.end();
        command.line = token.line;
        command.endLine = token.endLine;
        command.end = token.end();

        // Only whitespace and comments may sit between the name and its '('.
        std::size_t next = skipTrivia(i + 1);
        if (next >= tokens.size() || tokens[next].kind != cmake::LParen) {
            errorAt(token, L"expected '(' after the command name");
            i = next;
            continue;
        }
        command.openParen = tokens[next].offset;
        command.end = tokens[next].end();
        command.endLine = tokens[next].endLine;

        int nesting = 0;
        std::size_t j = next + 1;
        bool closed = false;
        while (j < tokens.size()) {
            const Token& t = tokens[j];
            if (isTrivia(t.cls)) { ++j; continue; }
            if (t.cls == TokenClass::Comment) { j = skipTrivia(j); continue; }

            if (t.kind == cmake::RParen && nesting == 0) {
                command.closeParen = t.offset;
                command.end = t.end();
                command.endLine = t.endLine;
                closed = true;
                ++j;
                break;
            }

            Argument arg;
            arg.offset = t.offset;
            arg.end = t.end();
            arg.line = t.line;
            arg.valueOffset = t.offset;
            arg.valueEnd = t.end();
            switch (t.kind) {
            case cmake::LParen:
                arg.kind = ArgKind::Paren;
                ++nesting;
                break;
            case cmake::RParen:
                arg.kind = ArgKind::Paren;
                --nesting;
                break;
            case cmake::Quoted:
                arg.kind = ArgKind::Quoted;
                arg.valueOffset = t.offset + 1;
                arg.valueEnd = t.has(TokenFlag_Unterminated) ? t.end() : t.end() - 1;
                if (t.has(TokenFlag_Unterminated)) errorAt(t, L"unterminated quoted argument");
                break;
            case cmake::Bracket: {
                arg.kind = ArgKind::Bracket;
                const std::wstring_view raw = textOf(t);
                std::size_t level = 0;
                while (1 + level < raw.size() && raw[1 + level] == L'=') ++level;
                arg.valueOffset = t.offset + level + 2;
                arg.valueEnd = t.has(TokenFlag_Unterminated) ? t.end() : t.end() - (level + 2);
                if (arg.valueEnd < arg.valueOffset) arg.valueEnd = arg.valueOffset;
                // A newline right after the opening bracket is not part of the value.
                if (arg.valueOffset < arg.valueEnd) {
                    if (source[arg.valueOffset] == L'\r' && arg.valueOffset + 1 < arg.valueEnd && source[arg.valueOffset + 1] == L'\n') arg.valueOffset += 2;
                    else if (source[arg.valueOffset] == L'\n' || source[arg.valueOffset] == L'\r') arg.valueOffset += 1;
                }
                if (t.has(TokenFlag_Unterminated)) errorAt(t, L"unterminated bracket argument");
                break;
            }
            default:
                arg.kind = ArgKind::Unquoted;
                break;
            }
            arg.value = std::wstring(source.substr(arg.valueOffset, arg.valueEnd - arg.valueOffset));
            command.args.push_back(std::move(arg));
            command.end = t.end();
            command.endLine = t.endLine;
            ++j;
        }
        if (!closed) {
            command.complete = false;
            errorAt(token, L"the command is missing its ')'");
        }

        // Block structure.
        const std::wstring lower = lowered(command.name);
        const std::size_t index = result.commands.size();
        if (isCloser(lower)) {
            const std::wstring wanted = openerOf(lower);
            int match = -1;
            for (int k = static_cast<int>(open.size()) - 1; k >= 0; --k) {
                if (lowered(result.commands[static_cast<std::size_t>(open[static_cast<std::size_t>(k)])].name) == wanted) {
                    match = k;
                    break;
                }
            }
            if (match < 0) {
                errorAt(token, L"no matching opening command");
                command.parent = open.empty() ? -1 : open.back();
            } else {
                open.resize(static_cast<std::size_t>(match) + 1);   // anything left open inside it is dropped
                command.parent = result.commands[static_cast<std::size_t>(open.back())].parent;
                open.pop_back();
            }
        } else if ((lower == L"elseif" || lower == L"else") && !open.empty()
                   && lowered(result.commands[static_cast<std::size_t>(open.back())].name) == L"if") {
            command.parent = result.commands[static_cast<std::size_t>(open.back())].parent;
        } else {
            command.parent = open.empty() ? -1 : open.back();
            if (isOpener(lower)) open.push_back(static_cast<int>(index));
        }
        result.commands.push_back(std::move(command));
        i = j;
    }
    for (int unclosed : open) {
        const Command& c = result.commands[static_cast<std::size_t>(unclosed)];
        ParseError error;
        error.offset = c.offset;
        error.length = c.nameEnd - c.offset;
        error.line = c.line;
        error.message = L"the block is never closed";
        result.errors.push_back(std::move(error));
    }
    std::sort(result.comments.begin(), result.comments.end(), [](const Comment& a, const Comment& b) { return a.offset < b.offset; });
    result.comments.erase(std::unique(result.comments.begin(), result.comments.end(),
                              [](const Comment& a, const Comment& b) { return a.offset == b.offset; }),
        result.comments.end());
    return result;
}

}  // namespace cmake
}  // namespace lex
