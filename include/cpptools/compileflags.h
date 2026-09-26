#pragma once

#include <string>
#include <vector>

namespace cpptools {

// The flags to parse one file with, and where they came from.
struct CompileFlags {
    // Ready for Parser::parseBuffer(): no compiler name, no source file, no output options; relative
    // include paths made absolute; "--driver-mode=cl" first when they were an MSVC command line.
    std::vector<std::string> args;
    // The compile_commands.json or compile_flags.txt they came from - empty when nothing was found
    // and args are just defaultCompileArgs() (no include paths: <angle> and "project" includes of
    // the file won't be found).
    std::string origin;

    bool fromProject() const { return !origin.empty(); }
};

// The flags for filePath, as a build would give them:
//  * from the nearest compile_commands.json - looked for beside each directory from the file's
//    upward, and in its build*/, cmake-build*/ and out/... subdirectories (the newest one wins) - the
//    file's own entry; or, for a file with none (a header, a new file), the entry of the closest
//    source file (same directory first, then the most path in common);
//  * else from the nearest compile_flags.txt (one flag per line, relative paths against its own
//    directory) - and one in the very directory of a compile_commands.json wins over it, because
//    that's the order libclang's own loader uses;
//  * else defaultCompileArgs().
// A header is parsed as C++ whatever the language of the entry it borrowed flags from. Never throws.
CompileFlags compileFlagsFor(const std::string& filePath);

// The part of compileFlagsFor() that turns one compile command into parse arguments: commandLine is
// the whole command (compiler first), directory what its relative paths are relative to, and
// sourceFile the file it compiles (dropped from the result).
std::vector<std::string> cleanCommandLine(const std::vector<std::string>& commandLine,
                                          const std::string& directory, const std::string& sourceFile);

} // namespace cpptools
