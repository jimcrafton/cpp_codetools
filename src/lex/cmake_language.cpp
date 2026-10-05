#include <lex/cmake_language.h>

#include <lex/cmake_lexer.h>

namespace lex {

namespace {

class CMakeLanguage final : public Language {
public:
    const wchar_t* name() const noexcept override { return L"CMake"; }

    std::unique_ptr<LexerBase> createLexer(std::wstring_view text, LexerOptions options) const override {
        return std::make_unique<CMakeLexer>(text, options);
    }
};

}  // namespace

const Language& cmakeLanguage() {
    static const CMakeLanguage language;
    return language;
}

}  // namespace lex
