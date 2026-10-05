#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cpptools_analysis {

// One #define in the analyzed file itself (not in the headers it includes).
struct MacroDefinitionInfo {
    std::string name;
    std::size_t line = 0;                  // 1-based
    bool functionLike = false;
    std::vector<std::string> params;
    std::string body;                      // the replacement list, tokens joined by single spaces
    // Parameters the body mentions more than once (not counting # and ##): an argument with a side effect is
    // then evaluated more than once.
    std::vector<std::string> repeatedParams;
};

// One step of a use's expansion: the text after `macro` was expanded. The first step is the use as written (macro empty).
struct MacroStep {
    std::string macro;
    std::string text;
};

// Where a macro came from.
enum class MacroOrigin { File, Header, CommandLine, BuiltIn };

// One place a macro was used in the file, outermost use only (a macro inside another's argument is listed
// under `nested` of the outer one).
struct MacroUseInfo {
    std::string name;
    std::size_t line = 0;                  // 1-based
    std::size_t column = 0;                // 1-based
    std::string written;                   // the use as written: "MAX(a, b)"
    std::string expansion;                 // what it became, tokens joined by single spaces; empty for an empty expansion
    std::vector<std::string> nested;       // macros expanded while expanding this one, in order, repeats kept
    bool definedHere = false;              // its definition is in the analyzed file
    std::size_t definedLine = 0;           // when definedHere
    MacroOrigin origin = MacroOrigin::File;
    std::string definedIn;                 // the header, when origin is Header
    // The use expanded one macro at a time, leftmost first, each step's text after that macro was replaced: the
    // first is the use as written, the last has nothing left to expand. Arguments are substituted as written
    // (the compiler expands them first, so the order can differ from its own; the last step is the same).
    std::vector<MacroStep> steps;
};

// A use whose argument looks like it has a side effect and whose macro mentions that parameter more than once.
struct MacroWarning {
    std::size_t line = 0;
    std::string macro;
    std::string param;
    std::string argument;
    std::string reason;                    // "contains ++", "calls a function", ...
};

// A range the preprocessor skipped: the body of an #if that was false, an #else not taken.
struct InactiveRegion {
    std::size_t startLine = 0;             // first skipped line, 1-based
    std::size_t endLine = 0;               // last skipped line
    std::string directive;                 // the #if / #ifdef / #elif / #else line that opened the region
};

struct MacroAnalysis {
    bool ok = false;                       // false: the tool could not run at all
    std::vector<MacroDefinitionInfo> definitions;
    std::vector<MacroUseInfo> uses;
    std::vector<MacroWarning> warnings;
    std::vector<InactiveRegion> inactive;
};

// Preprocesses `content` as the file `path` with the compile arguments `args` (what cpptools::compileFlagsFor
// gives: -I, -D, -std=...; not the file name) and reports its macros. `content` replaces what is on disk for
// that file, so an unsaved buffer is analyzed as it is; its #includes are read from disk. Diagnostics are dropped.
MacroAnalysis analyzeMacros(const std::string& content, const std::string& path, const std::vector<std::string>& args = {});

}  // namespace cpptools_analysis
