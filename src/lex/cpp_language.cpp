#include <lex/cpp_language.h>

#include <lex/cpp_lexer.h>

namespace lex {

namespace {

class CppLanguage final : public Language {
public:
    const wchar_t* name() const noexcept override { return L"C++"; }

    std::unique_ptr<LexerBase> createLexer(std::wstring_view text, LexerOptions options) const override {
        return std::make_unique<CppLexer>(text, options);
    }
};

}  // namespace

const Language& cppLanguage() {
    static const CppLanguage language;
    return language;
}

}  // namespace lex
