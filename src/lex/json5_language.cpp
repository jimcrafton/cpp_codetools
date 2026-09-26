#include <lex/json5_language.h>

#include <lex/json5_lexer.h>

namespace lex {

namespace {

class Json5Language final : public Language {
public:
    const wchar_t* name() const noexcept override { return L"JSON5"; }

    std::unique_ptr<LexerBase> createLexer(std::wstring_view text, LexerOptions options) const override {
        return std::make_unique<Json5Lexer>(text, options);
    }

    void styleLine(std::wstring_view text, const Token* tokens, std::size_t count,
                   StyleId* out) const override {
        Language::styleLine(text, tokens, count, out);

        for (std::size_t i = 0; i < count; ++i) {
            if (!canBeKey(text, tokens[i])) continue;

            std::size_t j = i + 1;
            while (j < count && (isTrivia(tokens[j].cls) || tokens[j].cls == TokenClass::Comment)) ++j;
            if (j < count && tokens[j].kind == json5::Colon) out[i] = StyleId::PropertyName;
        }
    }

private:
    static bool canBeKey(std::wstring_view text, const Token& t) noexcept {
        // A string continued across lines is never a complete key on this line.
        if (t.has(TokenFlag_Continued) || t.has(TokenFlag_Resumed)) return false;
        switch (t.kind) {
        case json5::String:
        case json5::Identifier:
        case json5::Null:
        case json5::True:
        case json5::False:
            return true;
        case json5::Number: {  // only the words Infinity / NaN are identifier names
            const std::wstring_view s = text.substr(t.offset, t.length);
            return s == L"Infinity" || s == L"NaN";
        }
        default:
            return false;
        }
    }
};

}  // namespace

const Language& json5Language() {
    static const Json5Language language;
    return language;
}

}  // namespace lex
