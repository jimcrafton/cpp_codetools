# cpp_codetools

A small C++ library and CLI tool that parse C++ source with **libclang** and produce a symbol
outline (namespaces, classes, functions, methods, fields, ... as a real nested tree) plus
compiler diagnostics. This is the foundation for a larger set of C++ code tools; later phases may
add a real project-aware parse (a `compile_commands.json`) and, eventually, a native editor.

## What's here

| Path | Role |
|---|---|
| `include/cpptools/symbol.h` | `Symbol`/`SymbolKind`/`SourceLocation` - the outline data model. |
| `include/cpptools/diagnostic.h` | `Diagnostic`/`Severity` - compiler diagnostics from a parse. |
| `include/cpptools/parser.h`, `src/parser.cpp` | `cpptools::Parser` - the libclang wrapper. |
| `tools/cppoutline/main.cpp` | CLI: parses a file, prints diagnostics then the outline. |
| `unittests/test_parser.cpp` | GoogleTest suite over `Parser`. |
| `3rdparty/googletest` | Vendored GoogleTest (v1.17.0), same copy `newui` uses. |

## The `newui` dependency

This repo also pulls in [`newui`](https://github.com/jimcrafton/newui) (a separate Win32/Blend2D
UI framework, same author) via CMake `FetchContent`, from its `cpptools` branch, into
`3rdparty/newui` - in preparation for the eventual editor work, not used by anything here yet.
It's fetched fresh via git on every clean configure (not a committed copy), so `3rdparty/newui/`
is gitignored. The first `cmake -S . -B build` after a clean clone will:

1. Clone `newui`'s `cpptools` branch into `3rdparty/newui`.
2. Bootstrap `newui`'s own reflectgen Python venv if it's missing (`python -m venv .venv` +
   `pip install -r requirements.txt` under `3rdparty/newui/tools/reflectgen`) - `newui`'s own
   build requires this and won't set it up itself, so `cpp_codetools`'s `CMakeLists.txt` does it
   automatically. This step needs network access the first time; see `CLAUDE.md` if you'd rather
   run it by hand and skip the auto-bootstrap.

`newui`'s targets (`newui`, `newui_app`, its examples, its tests) are declared but not built by
default (`EXCLUDE_FROM_ALL`) - nothing here links against `newui` yet. To build the library
itself directly: `cmake --build build/_deps/newui-build --config Debug --target newui` (see
`CLAUDE.md` for why the nested build dir is needed).

## Working with libclang

`cpptools::Parser` wraps libclang's C API (`clang-c/Index.h`) behind a small, real-C++ surface:

```cpp
cpptools::Parser parser;
cpptools::ParseResult result = parser.parseFile("foo.cpp");
// or, without touching disk (e.g. a live, unsaved editor buffer):
cpptools::ParseResult result = parser.parseBuffer("foo.cpp", bufferContents);

// result.symbols   - a tree of cpptools::Symbol (namespaces/classes/.../methods/fields)
// result.diagnostics - compiler errors/warnings/notes from the parse
```

A few things worth understanding if you're extending this:

- **One `CXIndex` per `Parser`, one `CXTranslationUnit` per parse call.** Nothing is cached or
  reused across calls yet - each `parseFile`/`parseBuffer` call is a fresh, independent parse.
- **Only symbols physically in the file being parsed are collected** -
  `clang_Location_isFromMainFile()` filters out anything pulled in via `#include`. Parsing a
  header that includes `<vector>` won't produce hundreds of STL symbols in the outline.
- **Only an "outline-worthy" subset of cursor kinds is kept** - namespaces, classes/structs/
  unions/enums, class templates, functions/methods/constructors/destructors, fields/variables,
  and typedefs. Everything else (statements, expressions, literals, template instantiation
  noise, ...) is skipped. Extend `isOutlineWorthy`/`toSymbolKind` in `src/parser.cpp` together if
  you need another kind.
- **No real compile flags yet.** Every parse uses a fixed placeholder (`-std=c++17 -xc++`,
  `defaultCompileArgs()` in `parser.h`) - fine for a single standalone file, not accurate for a
  file that's part of a real project with its own include paths/defines. Both `parseFile` and
  `parseBuffer` already take a `compileArgs` override, so a future `compile_commands.json` loader
  (via `clang_CompilationDatabase_fromDirectory`/`clang_CompilationDatabase_getCompileCommands`)
  can plug in without changing the API.
- **Diagnostics come along for free.** They're pulled off the same `CXTranslationUnit` the
  outline walk already produced (`clang_getNumDiagnostics`/`clang_getDiagnostic`), so a parse
  failure (bad syntax, a missing `#include`, ...) shows up as `Diagnostic` entries rather than an
  exception or a silent empty result.
- **`libclang.dll` has to sit next to any executable that uses it.** The root `CMakeLists.txt`'s
  `cpptools_copy_libclang_dll(target)` helper copies it post-build; call it for any new target
  that links `cpptools` (or `libclang::libclang` directly).

## Building

Requires a standalone LLVM install (tested with LLVM 19.1.0 from the
[llvm-project releases page](https://github.com/llvm/llvm-project/releases),
`LLVM-*-win64.exe`, installed to the default `C:\Program Files\LLVM`) - VS's own bundled
`clang-tidy`/`clang-format` do **not** ship `libclang.lib`/the `clang-c` headers, so that alone
isn't enough. If yours is installed somewhere else, pass
`-DCPPCODETOOLS_LLVM_ROOT=<path-to-your-LLVM-install>` when configuring.

From a Visual Studio 2026 developer shell (or via `vcvars64.bat`, see below):

```powershell
cmake -S . -B build
cmake --build build --config Debug --target cppoutline cpptools_tests
```

If `cmake`/`msbuild` aren't already on `PATH`, activate the VS environment first, in the same
shell invocation. **Use PowerShell for this, not Git Bash** - Git Bash's MSYS layer mangles a
lone `/c`/`/d` argument before `cmd.exe` ever sees it, silently turning the whole command into a
no-op:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d D:\code\cpp_codetools && cmake --build build --config Debug --target cppoutline cpptools_tests'
```

Output:
- `build\tools\cppoutline\Debug\cppoutline.exe`
- `build\unittests\Debug\cpptools_tests.exe`

## Trying it out

```
build\tools\cppoutline\Debug\cppoutline.exe path\to\some\file.cpp
```

Prints any diagnostics (to stderr) followed by the indented symbol outline (to stdout). Since
there's no real include-path/compile-flags support yet, parsing a file with non-trivial
`#include`s will report those as unresolved (that's expected - see "No real compile flags yet"
above) while still successfully producing an outline for whatever the file itself defines.

## Testing

```
build\unittests\Debug\cpptools_tests.exe
```

Runs the `ParserTest` suite directly (9 cases: each symbol kind, tree nesting, `#include`
exclusion, invalid-code diagnostics, and `parseFile`'s disk read) - **not** wired into `ctest`,
matching `newui`'s own testing convention.
