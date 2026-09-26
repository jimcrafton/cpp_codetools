#pragma once

// Convenience header: the whole library.
//
//   namespace lex          LexerBase, LexerOptions, Token, LineIndex, KeywordTable,
//                          Json5Lexer, json5Language(),
//                          SyntaxHighlighter, Theme, Language, highlightLine()
//   namespace lex::json5   token kinds, parse(), ASTNode, structural hashing,
//                          diff(), flattenUnified() / flattenSplit(), ViewState
//
// Include individual headers (<lex/json5_parser.h>, ...) to keep compile times down.

#include "lexer_base.h"
#include "json5_lexer.h"
#include "json5_ast.h"
#include "json5_parser.h"
#include "json5_hash.h"
#include "json5_diff.h"
#include "json5_presentation.h"
#include "highlight.h"
#include "json5_language.h"
