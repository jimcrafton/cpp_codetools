#pragma once

#include <cstdio>

#include <gtest/gtest.h>

// Failure count - the checks print extra context (the failing source) when it goes up.
inline int g_failures = 0;

// Reports through GoogleTest (file, line, condition), so each run*Tests() fails its TEST
// (lex_tests.cpp).
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            ADD_FAILURE_AT(__FILE__, __LINE__) << #cond;                                   \
        }                                                                                  \
    } while (0)

void runLexerBaseTests();
void runJson5LexerTests();
void runJson5ParserTests();
void runJson5DiffTests();
void runJson5PresentationTests();
void runHighlightTests();
