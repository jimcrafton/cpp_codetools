#include "cpptools/compileflags.h"
#include "cpptools/parser.h"

#include <clang-c/CXCompilationDatabase.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

namespace cpptools {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string toStdString(CXString clangString) {
    const char* text = clang_getCString(clangString);
    std::string result = text ? text : "";
    clang_disposeString(clangString);
    return result;
}

// A path made absolute against directory, spelled the way the platform does.
std::string absolutize(const std::string& path, const std::string& directory) {
    fs::path p(path);
    if (p.is_absolute()) {
        return p.lexically_normal().string();
    }
    return (fs::path(directory) / p).lexically_normal().string();
}

bool sameFile(const std::string& a, const std::string& b, const std::string& directory) {
    return lower(fs::path(absolutize(a, directory)).generic_string()) == lower(fs::path(absolutize(b, directory)).generic_string());
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool isClMode(const std::string& compiler) {
    const std::string stem = lower(fs::path(compiler).stem().string());
    return stem == "cl" || stem == "clang-cl";
}

bool isCppSource(const fs::path& file) {
    const std::string ext = lower(file.extension().string());
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++" || ext == ".cppm" || ext == ".ixx";
}

// Options that take a path: joined ("-Ipath", "/Ipath") and/or separate ("-isystem path").
struct PathOption {
    const char* spelling;
    bool joined;
    bool separate;
};
const PathOption kPathOptions[] = {
    {"-I", true, true}, {"/I", true, true}, {"-iquote", true, true}, {"-isystem", false, true},
    {"-idirafter", true, true}, {"-include", false, true}, {"-imsvc", true, true},
    {"/FI", true, true}, {"/external:I", true, true}, {"-external:I", true, true},
};

// Options whose value is a separate argument and which write files (or aren't about parsing).
const char* const kSeparateValueOptionsToDrop[] = { "-o", "-MF", "-MT", "-MQ" };

// cl-style options that write files or only matter to a real build, joined with their value.
const char* const kClOptionsToDrop[] = { "/Fo", "/Fd", "/Fe", "/Fa", "/Fp", "/Fm", "/Yc", "/Yu", "/Y-", "/MP" };
const char* const kClFlagsToDrop[] = { "/c", "/showIncludes", "/FS", "/Zi", "/ZI", "/Z7", "/JMC", "/Gm-", "/nologo" };

#ifdef _WIN32
// The MSVC STL refuses a compiler it doesn't know (VS 2026's wants Clang 20; LLVM 19's libclang is
// older): a static_assert deep in <yvals_core.h> otherwise. This is its own switch for that.
const char* const kStlVersionMismatchDefine = "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH";
#endif

std::vector<std::string> cleanCommandLineImpl(const std::vector<std::string>& commandLine, const std::string& directory,
                                              const std::string& sourceFile, bool* clModeOut) {
    std::vector<std::string> out;
    if (commandLine.empty()) {
        return out;
    }
    const bool clMode = isClMode(commandLine[0]);
    if (clModeOut) {
        *clModeOut = clMode;
    }
    bool hasDriverMode = false;   // libclang's own loader already puts "--driver-mode=" in the commands it infers

    for (std::size_t i = 1; i < commandLine.size(); ++i) {
        std::string arg = commandLine[i];
        if (startsWith(arg, "--driver-mode=")) {
            if (!hasDriverMode) {
                out.push_back(arg);
                hasDriverMode = true;
            }
            continue;
        }
        if (clMode && arg.size() > 1 && arg[0] == '-' && std::isalpha(static_cast<unsigned char>(arg[1])) != 0 &&
            (arg[1] == 'F' || arg[1] == 'Y' || arg[1] == 'M' || arg[1] == 'c' || arg[1] == 'Z' || arg[1] == 'J' || arg[1] == 'G' || arg[1] == 'n')) {
            arg[0] = '/';   // "-Fo..." is "/Fo..." to cl
        }

        // The compile itself, and what only a real build cares about. ("--" ends the options and
        // introduces the source file, which is dropped: what we add after would become input files.)
        if (arg == "-c" || arg == "-MD" || arg == "-MMD" || arg == "-MP" || arg == "-MG" || arg == "--") {
            continue;
        }
        bool dropped = false;
        for (const char* option : kSeparateValueOptionsToDrop) {
            if (arg == option) {
                ++i;   // and its value
                dropped = true;
                break;
            }
            if (std::string(option) == "-o" && startsWith(arg, "-o") && arg.size() > 2 && arg[2] != '-') {
                dropped = true;   // "-ofile"
                break;
            }
        }
        if (dropped) {
            continue;
        }
        if (clMode) {
            for (const char* flag : kClFlagsToDrop) {
                if (arg == flag) {
                    dropped = true;
                    break;
                }
            }
            for (const char* option : kClOptionsToDrop) {
                if (!dropped && startsWith(arg, option)) {
                    dropped = true;
                    break;
                }
            }
            if (dropped) {
                continue;
            }
        }

        // The source file itself (however it's spelled).
        if (!arg.empty() && arg[0] != '-' && !(clMode && arg[0] == '/' && arg.size() > 1 && arg.find('/', 1) == std::string::npos && arg.find('\\') == std::string::npos) &&
            sameFile(arg, sourceFile, directory)) {
            continue;
        }
        if ((arg == "/Tp" || arg == "/Tc") && i + 1 < commandLine.size() && sameFile(commandLine[i + 1], sourceFile, directory)) {
            ++i;
            continue;
        }
        if ((startsWith(arg, "/Tp") || startsWith(arg, "/Tc")) && arg.size() > 3 && sameFile(arg.substr(3), sourceFile, directory)) {
            continue;
        }

        // Relative include paths mean what they meant in the build's directory.
        bool handled = false;
        for (const PathOption& option : kPathOptions) {
            const std::string spelling = option.spelling;
            if (option.separate && arg == spelling && i + 1 < commandLine.size()) {
                out.push_back(arg);
                out.push_back(absolutize(commandLine[++i], directory));
                handled = true;
                break;
            }
            if (option.joined && startsWith(arg, spelling) && arg.size() > spelling.size()) {
                out.push_back(spelling + absolutize(arg.substr(spelling.size()), directory));
                handled = true;
                break;
            }
        }
        if (handled) {
            continue;
        }
        out.push_back(std::move(arg));
    }
    if (clMode && !hasDriverMode) {
        out.insert(out.begin(), "--driver-mode=cl");
    }
    return out;
}

// What an editor wants on top of a build's flags: no complaint about the MSVC STL's compiler check,
// and no "#pragma once in main file" on every header (a header is the main file here).
void addEditorArgs(std::vector<std::string>& args) {
#ifdef _WIN32
    args.push_back(kStlVersionMismatchDefine);
#endif
    args.push_back("-Wno-pragma-once-outside-header");
}

// One compile command, as libclang hands it over.
struct Command {
    std::vector<std::string> args;   // compiler first
    std::string directory;
    std::string file;                // as the database spells it
};

struct DatabaseDeleter {
    void operator()(void* db) const { clang_CompilationDatabase_dispose(static_cast<CXCompilationDatabase>(db)); }
};

Command readCommand(CXCompileCommand raw) {
    Command command;
    command.directory = toStdString(clang_CompileCommand_getDirectory(raw));
    command.file = toStdString(clang_CompileCommand_getFilename(raw));
    const unsigned count = clang_CompileCommand_getNumArgs(raw);
    for (unsigned i = 0; i < count; ++i) {
        command.args.push_back(toStdString(clang_CompileCommand_getArg(raw, i)));
    }
    return command;
}

std::size_t commonDepth(const fs::path& a, const fs::path& b) {
    std::size_t depth = 0;
    auto x = a.begin();
    auto y = b.begin();
    while (x != a.end() && y != b.end() && lower(x->generic_string()) == lower(y->generic_string())) {
        ++depth;
        ++x;
        ++y;
    }
    return depth;
}

// The entry for `file`: its own, or the closest source file's. False when the database has neither.
bool findCommand(const fs::path& databaseDir, const fs::path& file, Command& result, bool& borrowed) {
    CXCompilationDatabase_Error error = CXCompilationDatabase_NoError;
    CXCompilationDatabase database = clang_CompilationDatabase_fromDirectory(databaseDir.string().c_str(), &error);
    if (error != CXCompilationDatabase_NoError || database == nullptr) {
        return false;
    }
    std::unique_ptr<void, DatabaseDeleter> guard(database);

    for (const std::string& spelling : { file.string(), file.generic_string() }) {
        CXCompileCommands commands = clang_CompilationDatabase_getCompileCommands(database, spelling.c_str());
        if (commands != nullptr) {
            const bool any = clang_CompileCommands_getSize(commands) > 0;
            if (any) {
                result = readCommand(clang_CompileCommands_getCommand(commands, 0));
            }
            clang_CompileCommands_dispose(commands);
            if (any) {
                borrowed = false;
                return true;
            }
        }
    }

    // Not built by name (a header, a new file): the closest source file's flags.
    CXCompileCommands all = clang_CompilationDatabase_getAllCompileCommands(database);
    if (all == nullptr) {
        return false;
    }
    const unsigned size = clang_CompileCommands_getSize(all);
    std::size_t bestDepth = 0;
    bool bestSameDirectory = false;
    bool found = false;
    for (unsigned i = 0; i < size; ++i) {
        Command candidate = readCommand(clang_CompileCommands_getCommand(all, i));
        const fs::path source = absolutize(candidate.file, candidate.directory);
        const std::size_t depth = commonDepth(source.parent_path(), file.parent_path());
        const bool sameDirectory = lower(source.parent_path().generic_string()) == lower(file.parent_path().generic_string());
        if (depth < 2) {
            continue;   // nothing but a drive in common
        }
        const bool better = !found || (sameDirectory && !bestSameDirectory) ||
                            (sameDirectory == bestSameDirectory && depth > bestDepth);
        if (better) {
            result = std::move(candidate);
            bestDepth = depth;
            bestSameDirectory = sameDirectory;
            found = true;
        }
    }
    clang_CompileCommands_dispose(all);
    borrowed = true;
    return found;
}

// compile_commands.json files "beside" dir: in dir, then in build*/, cmake-build*/ and out/... below it.
std::vector<fs::path> databasesBeside(const fs::path& dir) {
    std::vector<fs::path> found;
    std::error_code ignored;
    auto consider = [&](const fs::path& candidateDir) {
        if (fs::is_regular_file(candidateDir / "compile_commands.json", ignored)) {
            found.push_back(candidateDir);
        }
    };
    consider(dir);
    if (!found.empty()) {
        return found;
    }
    std::vector<fs::path> subdirectories;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ignored), end; !ignored && it != end; it.increment(ignored)) {
        std::error_code entryError;
        if (!it->is_directory(entryError)) {
            continue;
        }
        const std::string name = lower(it->path().filename().string());
        if (startsWith(name, "build") || startsWith(name, "cmake-build") || name == "out") {
            subdirectories.push_back(it->path());
        }
    }
    for (const fs::path& sub : subdirectories) {
        consider(sub);
        if (lower(sub.filename().string()) == "out") {   // out/build/<config>/ and out/<config>/
            for (fs::directory_iterator level(sub, fs::directory_options::skip_permission_denied, ignored), end; !ignored && level != end; level.increment(ignored)) {
                std::error_code entryError;
                if (!level->is_directory(entryError)) {
                    continue;
                }
                consider(level->path());
                if (lower(level->path().filename().string()) == "build") {
                    for (fs::directory_iterator config(level->path(), fs::directory_options::skip_permission_denied, ignored), cend; !ignored && config != cend; config.increment(ignored)) {
                        consider(config->path());
                    }
                }
            }
        }
    }
    // Several build directories: the one built most recently is the one in use.
    std::sort(found.begin(), found.end(), [&](const fs::path& a, const fs::path& b) {
        return fs::last_write_time(a / "compile_commands.json", ignored) > fs::last_write_time(b / "compile_commands.json", ignored);
    });
    return found;
}

std::vector<std::string> readFlagsFile(const fs::path& file) {
    std::vector<std::string> args;
    std::ifstream in(file);
    std::string line;
    const std::string directory = file.parent_path().string();
    while (std::getline(in, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.pop_back();
        }
        std::size_t first = 0;
        while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first])) != 0) {
            ++first;
        }
        line.erase(0, first);
        if (!line.empty()) {
            args.push_back(line);
        }
    }
    // Reuse the command-line cleaning for relative include paths (a made-up compiler name up front).
    std::vector<std::string> commandLine{ "clang" };
    commandLine.insert(commandLine.end(), args.begin(), args.end());
    return cleanCommandLineImpl(commandLine, directory, std::string(), nullptr);
}

} // namespace

std::vector<std::string> cleanCommandLine(const std::vector<std::string>& commandLine, const std::string& directory,
                                          const std::string& sourceFile) {
    std::vector<std::string> args = cleanCommandLineImpl(commandLine, directory, sourceFile, nullptr);
    addEditorArgs(args);
    return args;
}

CompileFlags compileFlagsFor(const std::string& filePath) {
    CompileFlags flags;
    flags.args = defaultCompileArgs();
    try {
        std::error_code ignored;
        const fs::path file = fs::absolute(fs::path(filePath), ignored).lexically_normal();
        const bool cppSource = isCppSource(file);

        fs::path dir = file.parent_path();
        for (int level = 0; level < 16 && !dir.empty(); ++level) {
            for (const fs::path& databaseDir : databasesBeside(dir)) {
                Command command;
                bool borrowed = false;
                if (!findCommand(databaseDir, file, command, borrowed)) {
                    continue;
                }
                bool clMode = false;
                std::vector<std::string> args = cleanCommandLineImpl(command.args, command.directory,
                    absolutize(command.file, command.directory), &clMode);
                addEditorArgs(args);
                // A header (or anything not a C++ source) is still parsed as C++.
                if (!cppSource) {
                    args.push_back(clMode ? "/TP" : "-xc++");
                }
                flags.args = std::move(args);
                // libclang loads a compile_flags.txt in the same directory in preference to the JSON
                // database (and offers no way to choose): say which one it really was.
                const fs::path flagsFile = databaseDir / "compile_flags.txt";
                flags.origin = (fs::is_regular_file(flagsFile, ignored) ? flagsFile : databaseDir / "compile_commands.json").string();
                return flags;
            }

            const fs::path flagsFile = dir / "compile_flags.txt";
            if (fs::is_regular_file(flagsFile, ignored)) {
                std::vector<std::string> args = readFlagsFile(flagsFile);
                addEditorArgs(args);
                bool hasLanguage = false;
                for (const std::string& arg : args) {
                    hasLanguage = hasLanguage || startsWith(arg, "-x") || startsWith(arg, "/T");
                }
                if (!hasLanguage) {
                    args.push_back("-xc++");
                }
                flags.args = std::move(args);
                flags.origin = flagsFile.string();
                return flags;
            }

            const fs::path parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    } catch (...) {
        // A path or a file that can't be read: the defaults will do.
    }
    return flags;
}

} // namespace cpptools
