// The lex library's checks (brought over from lexer-base), one GoogleTest per suite - each
// CHECK failure is reported with its own file and line (test_util.h).
#include "test_util.h"

TEST(Lex, LexerBase) { runLexerBaseTests(); }
TEST(Lex, Json5Lexer) { runJson5LexerTests(); }
TEST(Lex, Json5Parser) { runJson5ParserTests(); }
TEST(Lex, Json5Diff) { runJson5DiffTests(); }
TEST(Lex, Json5Presentation) { runJson5PresentationTests(); }
TEST(Lex, Highlight) { runHighlightTests(); }
