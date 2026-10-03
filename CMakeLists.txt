cmake_minimum_required(VERSION 3.24)
project(cpp_codetools VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version header (Major.Minor.Release.Build)
# ============================================
# Same scheme as newui's own (see cmake/GenerateVersion.cmake, which mirrors
# D:\code\newui\cmake\GenerateVersion.cmake) - Build advances by 1 every time the project is
# *built*, not just configured, via a custom target with no declared OUTPUT so every generator
# (Ninja, MSBuild, Make) treats it as always out-of-date and reruns it on every build. Build rolls
# over at 9999 into Release, Release rolls over at 999 into Minor, Minor rolls over at 99 into
# Major - so Major/Minor/Release aren't fixed at configure time either; they're state that can
# change purely from accumulated builds. That state is persisted in VERSION.state at the repo
# root - deliberately a *tracked* source-tree file, not a build-tree one, so version progression
# survives a build/ wipe-and-reconfigure. project(VERSION) above only seeds VERSION.state the very
# first time it's created.
# Product-metadata fields carried alongside the Major.Minor.Release.Build tuple in VERSION.state
# (see cmake/GenerateVersion.cmake) - only used to seed VERSION.state the very first time it
# doesn't exist yet; every build after that just carries forward whatever's already on disk there
# (hand-edit VERSION.state directly to change these, not these defaults).
set(CPPCODETOOLS_AUTHOR "Jim Crafton")
set(CPPCODETOOLS_COPYRIGHT "Copyright (c) 2026 Jim Crafton. All rights reserved.")
set(CPPCODETOOLS_PRODUCT_NAME "codetools++")
set(CPPCODETOOLS_DESCRIPTION "codetools++ native edit control")

add_custom_target(cpptools_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DINITIAL_MAJOR=${cpp_codetools_VERSION_MAJOR}
        -DINITIAL_MINOR=${cpp_codetools_VERSION_MINOR}
        -DINITIAL_RELEASE=${cpp_codetools_VERSION_PATCH}
        -DINITIAL_AUTHOR=${CPPCODETOOLS_AUTHOR}
        -DINITIAL_COPYRIGHT=${CPPCODETOOLS_COPYRIGHT}
        -DINITIAL_PRODUCT_NAME=${CPPCODETOOLS_PRODUCT_NAME}
        -DINITIAL_DESCRIPTION=${CPPCODETOOLS_DESCRIPTION}
        -DSTATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/VERSION.state
        -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/include/cpptools/version.h.in
        -DOUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/include/cpptools/version.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/include/cpptools/version.h
    COMMENT "Generating cpptools/version.h (incrementing build number)"
    VERBATIM
)

# libclang - Dependency
# ======================
# Not vendored (unlike newui's blend2d/wil/json5/googletest) - libclang is a large, separately
# versioned LLVM install, not something to check into this repo. CPPCODETOOLS_LLVM_ROOT points at
# a standalone LLVM install - override with -DCPPCODETOOLS_LLVM_ROOT=... if yours isn't at the
# default.
#
# As of 2026-09-28 this is a from-source LLVM+Clang+clang-tools-extra build (llvmorg-23.1.2, built
# on a separate machine with -DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra and no CRT override -
# the same /MD-by-default recipe already proven for D:\code\llvm-project-llvmorg-19.1.6 (see
# scratch-lang), then `cmake --install`'d and transferred here), not the plain official llvm.org
# binary release (`LLVM-*-win64.exe`) this used to point at - that release only ships libclang, no
# LibTooling/AST/ASTMatchers/Format libs or their CMake package files, which cpptools_codegen
# (below) needs. Deliberately reusing this one root for both rather than keeping the old binary
# release around separately - one LLVM version for the whole project.

set(CPPCODETOOLS_LLVM_ROOT "D:/code/LLVM-23.1.2" CACHE PATH
    "Root of an LLVM+Clang install containing include/clang-c, lib/libclang.lib, bin/libclang.dll, and (for cpptools_codegen) lib/cmake/clang's Clang CMake package")

# NO_CACHE: these must follow CPPCODETOOLS_LLVM_ROOT on every configure. Cached, they kept pointing
# at whatever the very first configure found (an old C:/Program Files/LLVM 19.1.0) after the root
# was moved to a newer install - the tests then ran a libclang too old for the current MSVC STL
# ("STL1000: expected Clang 20 or newer") while the headers/codegen libs came from the new one.
# (Entries cached by an older configure would otherwise still win, so drop them first.)
unset(CPPCODETOOLS_LIBCLANG_INCLUDE_DIR CACHE)
unset(CPPCODETOOLS_LIBCLANG_IMPLIB CACHE)
unset(CPPCODETOOLS_LIBCLANG_DLL CACHE)
find_path(CPPCODETOOLS_LIBCLANG_INCLUDE_DIR clang-c/Index.h NO_CACHE
    HINTS "${CPPCODETOOLS_LLVM_ROOT}/include")
find_library(CPPCODETOOLS_LIBCLANG_IMPLIB NAMES libclang NO_CACHE
    HINTS "${CPPCODETOOLS_LLVM_ROOT}/lib")
find_file(CPPCODETOOLS_LIBCLANG_DLL NAMES libclang.dll NO_CACHE
    HINTS "${CPPCODETOOLS_LLVM_ROOT}/bin")

if(NOT CPPCODETOOLS_LIBCLANG_INCLUDE_DIR OR NOT CPPCODETOOLS_LIBCLANG_IMPLIB OR NOT CPPCODETOOLS_LIBCLANG_DLL)
    message(FATAL_ERROR
        "libclang not found under CPPCODETOOLS_LLVM_ROOT='${CPPCODETOOLS_LLVM_ROOT}'. "
        "Install LLVM (https://github.com/llvm/llvm-project/releases) or pass "
        "-DCPPCODETOOLS_LLVM_ROOT=<path> pointing at your install.")
endif()

add_library(libclang::libclang SHARED IMPORTED)
set_target_properties(libclang::libclang PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CPPCODETOOLS_LIBCLANG_INCLUDE_DIR}"
    IMPORTED_IMPLIB "${CPPCODETOOLS_LIBCLANG_IMPLIB}"
    IMPORTED_LOCATION "${CPPCODETOOLS_LIBCLANG_DLL}")

# Every executable that links libclang::libclang needs its own copy of libclang.dll next to it
# at runtime (a plain IMPORTED SHARED target doesn't do this automatically on Windows) - call
# this once per such target.
function(cpptools_copy_libclang_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CPPCODETOOLS_LIBCLANG_DLL}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying libclang.dll for ${target}")
endfunction()

# Clang LibTooling - Dependency (cpptools_codegen)
# ==================================================
# See bluesky/cpp-codegen-plan.md - libclang (above) has no write API at all, so generating new
# C++ source (starting with delegate-wiring, DE-01/CCE-06) needs Clang's own C++ libraries (AST
# Matchers to find WHERE, a Fluent Builder + clang::format::reformat to build/format WHAT) linked
# in-process, not just the C ABI cpptools already uses. Same CPPCODETOOLS_LLVM_ROOT as libclang
# above (see its own comment) - resolved via Clang's own exported CMake package rather than
# hand-listing library names: ClangConfig.cmake's own CLANG_EXPORTED_TARGETS pulls in every
# transitively-required LLVM*.lib itself (~30-40 of them), which would be easy to get wrong by
# hand and silently break on a future LLVM version bump.
find_package(Clang CONFIG QUIET HINTS "${CPPCODETOOLS_LLVM_ROOT}" NO_DEFAULT_PATH)
if(NOT Clang_FOUND)
    message(FATAL_ERROR
        "Clang's CMake package (LibTooling/AST/ASTMatchers/Format) not found under "
        "CPPCODETOOLS_LLVM_ROOT='${CPPCODETOOLS_LLVM_ROOT}'. This needs a from-source LLVM build "
        "configured with -DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra and cmake --install'd (the "
        "official llvm.org binary release doesn't ship these) - see bluesky/cpp-codegen-plan.md, "
        "or pass -DCPPCODETOOLS_LLVM_ROOT=<path> pointing at one.")
endif()

# find_package(Clang) above only knows one configuration's library paths - whichever
# CMAKE_BUILD_TYPE the CPPCODETOOLS_LLVM_ROOT tree itself was built+installed with (Release, a
# /MD build). This project's own build is a multi-config MSBuild tree though, and
# NativeEditControls/testharness build --config Debug (/MDd) - linking cpptools_codegen's
# Release-CRT Clang static libs into a Debug target is exactly the LNK2038 CRT mismatch recorded
# in bluesky/cpp-codegen-plan.md's "First slice" section. Rather than hand-listing the ~30-40
# transitively-pulled LLVM*/clang* imported targets a second time, patch an IMPORTED_LOCATION_DEBUG
# onto every already-declared imported target above whose name matches a .lib under a separate,
# Debug-configured (/MDd) install of the *same* LLVM version - CMake's imported-target config
# matching (case-insensitive) then picks the Debug path automatically for a --config Debug build
# and the Release path (set by find_package(Clang) above) for --config Release, same as any other
# multi-config imported target.
set(CPPCODETOOLS_LLVM_ROOT_DEBUG "D:/code/LLVM-23.1.2_debug" CACHE PATH
    "Root of a Debug (/MDd) install of the same LLVM+Clang version as CPPCODETOOLS_LLVM_ROOT (built with -DCMAKE_BUILD_TYPE=Debug, then cmake --install'd) - patches in Debug-configuration library paths for cpptools_codegen's Clang imported targets so it can link into a --config Debug build. Leave unset/nonexistent to skip - cpptools_codegen will then only link in Release builds.")

if(EXISTS "${CPPCODETOOLS_LLVM_ROOT_DEBUG}")
    file(GLOB _cpptools_debug_clang_libs "${CPPCODETOOLS_LLVM_ROOT_DEBUG}/lib/*.lib")
    foreach(_cpptools_debug_lib ${_cpptools_debug_clang_libs})
        get_filename_component(_cpptools_debug_lib_target "${_cpptools_debug_lib}" NAME_WE)
        if(TARGET ${_cpptools_debug_lib_target})
            get_target_property(_cpptools_debug_lib_configs ${_cpptools_debug_lib_target} IMPORTED_CONFIGURATIONS)
            if(NOT _cpptools_debug_lib_configs)
                set(_cpptools_debug_lib_configs "")
            endif()
            if(NOT "DEBUG" IN_LIST _cpptools_debug_lib_configs)
                list(APPEND _cpptools_debug_lib_configs DEBUG)
                set_property(TARGET ${_cpptools_debug_lib_target} PROPERTY IMPORTED_CONFIGURATIONS ${_cpptools_debug_lib_configs})
            endif()
            set_property(TARGET ${_cpptools_debug_lib_target} PROPERTY IMPORTED_LOCATION_DEBUG "${_cpptools_debug_lib}")
        endif()
    endforeach()
    unset(_cpptools_debug_clang_libs)
else()
    message(STATUS
        "CPPCODETOOLS_LLVM_ROOT_DEBUG='${CPPCODETOOLS_LLVM_ROOT_DEBUG}' not found - cpptools_codegen "
        "will only link in Release builds (see its CMakeLists.txt comment above).")
endif()

# newui - Dependency
# ===================
# Pulled from git (the "cpptools" branch of jimcrafton/newui - a branch cut specifically for
# this project to build against) rather than vendored as a static copy, unlike googletest below.
# EXCLUDE_FROM_ALL means declaring/populating it makes its targets (newui, newui_app, every
# examples/* and tools/reflectgen target) known to CMake without building any of them until
# something actually links against one - nothing does yet (no UI work in this repo so far), so
# this only adds the dependency, it doesn't change what actually gets built by default.

include(FetchContent)

FetchContent_Declare(newui
    GIT_REPOSITORY https://github.com/jimcrafton/newui.git
    GIT_TAG cpptools
    GIT_SHALLOW TRUE
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/newui
)

FetchContent_GetProperties(newui)
if(NOT newui_POPULATED)
    FetchContent_Populate(newui)
endif()

# newui's own CMakeLists.txt (add_subdirectory()'d below) hard-requires its reflectgen Python
# venv to already exist - a FATAL_ERROR from that project's cmake/ReflectGen.cmake otherwise,
# since it deliberately won't set one up itself (see that file's own comment). A fresh git fetch
# never includes it (.venv is gitignored in that repo), so bootstrap it here once, automatically,
# rather than requiring a manual step after every fresh clone of *this* repo. This does mean the
# first configure can hit the network (pip) - see README.md if you'd rather do this by hand and
# skip that.
set(NEWUI_REFLECTGEN_DIR "${newui_SOURCE_DIR}/tools/reflectgen")
set(NEWUI_REFLECTGEN_PYTHON "${NEWUI_REFLECTGEN_DIR}/.venv/Scripts/python.exe")

if(NOT EXISTS "${NEWUI_REFLECTGEN_PYTHON}")
    message(STATUS "newui reflectgen venv not found - bootstrapping at ${NEWUI_REFLECTGEN_DIR}/.venv")

    execute_process(
        COMMAND python -m venv .venv
        WORKING_DIRECTORY "${NEWUI_REFLECTGEN_DIR}"
        RESULT_VARIABLE NEWUI_REFLECTGEN_VENV_RESULT
    )
    if(NOT NEWUI_REFLECTGEN_VENV_RESULT EQUAL 0)
        message(FATAL_ERROR
            "newui reflectgen venv bootstrap failed: 'python -m venv .venv' exited with "
            "${NEWUI_REFLECTGEN_VENV_RESULT}. Run the steps in "
            "3rdparty/newui/tools/reflectgen/README.md's Setup section by hand instead.")
    endif()

    # Invoked as the venv's own python -m pip (not .venv/Scripts/pip.exe directly) - more
    # reliable than the pip.exe shim, which was observed once during development to silently
    # report success while installing into the wrong environment.
    execute_process(
        COMMAND "${NEWUI_REFLECTGEN_PYTHON}" -m pip install -r requirements.txt
        WORKING_DIRECTORY "${NEWUI_REFLECTGEN_DIR}"
        RESULT_VARIABLE NEWUI_REFLECTGEN_PIP_RESULT
    )
    if(NOT NEWUI_REFLECTGEN_PIP_RESULT EQUAL 0)
        message(FATAL_ERROR
            "newui reflectgen venv bootstrap failed: 'pip install -r requirements.txt' exited "
            "with ${NEWUI_REFLECTGEN_PIP_RESULT}.")
    endif()

    message(STATUS "newui reflectgen venv ready.")
endif()

add_subdirectory(${newui_SOURCE_DIR} ${newui_BINARY_DIR} EXCLUDE_FROM_ALL)

add_library(cpptools STATIC
    src/parser.cpp
    src/compileflags.cpp
    src/log.cpp
    src/delegatebindings.cpp
    include/cpptools/delegatebindings.h
    include/cpptools/symbol.h
    include/cpptools/compileflags.h
    include/cpptools/diagnostic.h
    include/cpptools/parser.h
    include/cpptools/log.h)

target_include_directories(cpptools PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(cpptools PUBLIC libclang::libclang)
add_dependencies(cpptools cpptools_version)

# cpptools_codegen - Clang LibTooling-based C++ source generation (bluesky/cpp-codegen-plan.md).
# Deliberately separate from cpptools (above), not folded into it - cpptools's own consumers
# (cppoutline, the existing NativeEditControls outline/diagnostics/rename/occurrence-highlighting
# code) only ever need the small, stable libclang C ABI; this pulls in Clang's much larger,
# non-ABI-stable C++ libraries (clangTooling/clangASTMatchers, see the Clang LibTooling dependency
# section above), which nothing else in this repo needs yet. First real slice, per the design
# doc's own "suggested first slice": classInsertionPoint() - given a class's current source, where
# does a new member go - proven with AST Matchers/getBraceRange() before any text-generation
# (Fluent Builder) work starts.
add_library(cpptools_codegen STATIC
    src/codegen/classinsertion.cpp
    include/cpptools_codegen/classinsertion.h
    src/codegen/methodbuilder.cpp
    include/cpptools_codegen/methodbuilder.h
    src/codegen/classbuilder.cpp
    include/cpptools_codegen/classbuilder.h
    src/codegen/methodinsertion.cpp
    include/cpptools_codegen/methodinsertion.h
    src/codegen/delegatewiring.cpp
    include/cpptools_codegen/delegatewiring.h
    src/codegen/fieldinsertion.cpp
    include/cpptools_codegen/fieldinsertion.h
    src/codegen/controllerwiring.cpp
    include/cpptools_codegen/controllerwiring.h)

target_include_directories(cpptools_codegen PUBLIC include)
# LLVM's own exported CMake targets (ClangTargets.cmake) don't set INTERFACE_INCLUDE_DIRECTORIES/
# INTERFACE_COMPILE_DEFINITIONS on themselves at all (confirmed - a known LLVM CMake quirk, unlike
# a "modern" CMake package) - LLVM_INCLUDE_DIRS/LLVM_DEFINITIONS (set by LLVMConfig.cmake, pulled
# in transitively by find_package(Clang) above) are the documented way to add them by hand.
# PRIVATE: only classinsertion.cpp itself needs Clang's headers - the public
# cpptools_codegen/classinsertion.h above is deliberately Clang-opaque (plain std types only), so
# consumers of this library don't need them too.
target_include_directories(cpptools_codegen PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(cpptools_codegen PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(cpptools_codegen PUBLIC clangTooling clangASTMatchers)

# lex - lossless lexing/parsing, semantic diff and syntax highlighting (JSON5 and C++ lexing),
# brought over from lexer-base. Standard library only, so the editors can highlight without
# pulling in libclang the way cpptools does.
add_library(lex STATIC
    src/lex/lexer_base.cpp
    src/lex/json5_lexer.cpp
    src/lex/json5_parser.cpp
    src/lex/json5_hash.cpp
    src/lex/json5_diff.cpp
    src/lex/json5_presentation.cpp
    src/lex/highlight.cpp
    src/lex/json5_language.cpp
    src/lex/cpp_lexer.cpp
    src/lex/cpp_language.cpp
    include/lex/lex.h
    include/lex/lexer_base.h
    include/lex/json5_lexer.h
    include/lex/json5_ast.h
    include/lex/json5_parser.h
    include/lex/json5_hash.h
    include/lex/json5_diff.h
    include/lex/json5_presentation.h
    include/lex/highlight.h
    include/lex/json5_language.h
    include/lex/cpp_lexer.h
    include/lex/cpp_language.h)

target_include_directories(lex PUBLIC include)
if(MSVC)
    target_compile_options(lex PRIVATE /utf-8)
endif()

add_subdirectory(tools/cppoutline)

# codetools++ VSIX support - extension/NativeEditControls.dll, the native HWND edit surface
# CodeToolsVsix.csproj (the VSIX/C# package - extension/CodeToolsVsix/) P/Invokes into. A normal
# CMake SHARED library target, not a hand-written vcxproj - target_link_libraries(... PRIVATE
# cpptools newui) below gets every one of cpptools'/newui's own PUBLIC include dirs and link
# libraries (libclang, blend2d, Comctl32/Uxtheme/Ole32/..., see newui's own top-level
# CMakeLists.txt) transitively, with nothing to hand-maintain here. There is no separate C
# API/bridge library - it calls cpptools::Parser and hosts a real newui::RootView both in-process
# (see CppEditorControl.cpp/EditThreadHost.cpp). CodeToolsVsix.csproj's own build invokes
# `cmake --build --target NativeEditControls` against this build tree before deploying the result
# alongside itself - nothing needs to be add_subdirectory()'d here for that.
add_library(NativeEditControls SHARED
    extension/NativeEditControls/NativeEditControlApi.h
    extension/NativeEditControls/CppEditor.h
    extension/NativeEditControls/CppEditor.cpp
    extension/NativeEditControls/DesignerEditor.h
    extension/NativeEditControls/DesignerEditor.cpp
    extension/NativeEditControls/PropertyEditor.h
    extension/NativeEditControls/PropertyEditor.cpp
    extension/NativeEditControls/CalloutPlacement.h
    extension/NativeEditControls/CalloutPlacement.cpp
    extension/NativeEditControls/ViewStyleRegistry.h
    extension/NativeEditControls/ViewStyleRegistry.cpp
    extension/NativeEditControls/GradientEditorDialog.h
    extension/NativeEditControls/GradientEditorDialog.cpp
    extension/NativeEditControls/ColorPicker.h
    extension/NativeEditControls/ColorPicker.cpp
    extension/NativeEditControls/ColorEditorDialog.h
    extension/NativeEditControls/ColorEditorDialog.cpp
    extension/NativeEditControls/PaintUtils.h
    extension/NativeEditControls/PaintUtils.cpp
    extension/NativeEditControls/ColorChoices.h
    extension/NativeEditControls/ColorChoices.cpp
    extension/NativeEditControls/ComponentEditor.h
    extension/NativeEditControls/ComponentEditor.cpp
    extension/NativeEditControls/MenuDesigner.h
    extension/NativeEditControls/MenuDesigner.cpp
    extension/NativeEditControls/SourceView.h
    extension/NativeEditControls/SourceView.cpp
    extension/NativeEditControls/HighlightController.h
    extension/NativeEditControls/HighlightController.cpp
    extension/NativeEditControls/FindEngine.h
    extension/NativeEditControls/FindEngine.cpp
    extension/NativeEditControls/DocumentEditService.h
    extension/NativeEditControls/DocumentEditService.cpp
    extension/NativeEditControls/HostEditorBridge.h
    extension/NativeEditControls/HostEditorBridge.cpp
    extension/NativeEditControls/CodegenEditAdapter.h
    extension/NativeEditControls/CodegenEditAdapter.cpp
    extension/NativeEditControls/WiringRequestBuilder.h
    extension/NativeEditControls/WiringRequestBuilder.cpp
    extension/NativeEditControls/ControllerRef.h
    extension/NativeEditControls/ControllerRef.cpp
    extension/NativeEditControls/ControllerBindings.h
    extension/NativeEditControls/ControllerBindings.cpp
    extension/NativeEditControls/ControllerWiringAction.h
    extension/NativeEditControls/ControllerWiringAction.cpp
    extension/NativeEditControls/NewControllerDialog.h
    extension/NativeEditControls/NewControllerDialog.cpp
    extension/NativeEditControls/IncludeManager.h
    extension/NativeEditControls/IncludeManager.cpp
    extension/NativeEditControls/IncludePlanner.h
    extension/NativeEditControls/IncludePlanner.cpp
    extension/NativeEditControls/FindReplaceController.h
    extension/NativeEditControls/FindReplaceController.cpp
    extension/NativeEditControls/MinimapStrip.h
    extension/NativeEditControls/MinimapStrip.cpp
    extension/NativeEditControls/CppHighlight.h
    extension/NativeEditControls/CppHighlight.cpp
    extension/NativeEditControls/CppDiagnostics.h
    extension/NativeEditControls/CppDiagnostics.cpp
    extension/NativeEditControls/DesignerClipboard.h
    extension/NativeEditControls/DesignerClipboard.cpp
    extension/NativeEditControls/DesignerArrange.h
    extension/NativeEditControls/DesignerArrange.cpp
    extension/NativeEditControls/ToolboxRegistry.h
    extension/NativeEditControls/ToolboxRegistry.cpp
    extension/NativeEditControls/Toolbox.h
    extension/NativeEditControls/Toolbox.cpp
    extension/NativeEditControls/Workspace.h
    extension/NativeEditControls/Workspace.cpp
    extension/NativeEditControls/CanvasWell.h
    extension/NativeEditControls/CanvasWell.cpp
    extension/NativeEditControls/ViewDesignerController.h
    extension/NativeEditControls/ViewDesignerController.cpp
    extension/NativeEditControls/ViewDesignerModel.h
    extension/NativeEditControls/ViewDesignerModel.cpp
    extension/NativeEditControls/PropertiesModel.h
    extension/NativeEditControls/PropertiesModel.cpp
    extension/NativeEditControls/PropertyItem.h
    extension/NativeEditControls/PropertyItem.cpp
    extension/NativeEditControls/PropertiesGrid.h
    extension/NativeEditControls/PropertiesGrid.cpp
    extension/NativeEditControls/PickerRow.h
    extension/NativeEditControls/IssuesPopup.h
    extension/NativeEditControls/IssuesPopup.cpp
    extension/NativeEditControls/IssuePeek.cpp
    extension/NativeEditControls/IssuePeek.h
    extension/NativeEditControls/PropertiesPanel.h
    extension/NativeEditControls/PropertiesPanel.cpp
    extension/NativeEditControls/DocumentOutline.h
    extension/NativeEditControls/DocumentOutline.cpp
    extension/NativeEditControls/SelectionOverlay.h
    extension/NativeEditControls/SelectionOverlay.cpp
    extension/NativeEditControls/LayoutEditingPolicy.h
    extension/NativeEditControls/LayoutEditingPolicy.cpp


    extension/NativeEditControls/NativeEditControlApi.cpp
    extension/NativeEditControls/TextEncoding.h
    extension/NativeEditControls/TextEncoding.cpp
    extension/NativeEditControls/Logging.h
    extension/NativeEditControls/Logging.cpp
    extension/NativeEditControls/dllmain.cpp
    extension/NativeEditControls/NativeEditControl.rc
    extension/NativeEditControls/NativeEditor.cpp)

target_compile_definitions(NativeEditControls PRIVATE
    NATIVEEDITCONTROL_EXPORTS
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

set(VSEXT_INCLUDES "C:/Program Files/Microsoft Visual Studio/18/Community/VSSDK/VisualStudioIntegration/Common/Inc")


target_include_directories(NativeEditControls PUBLIC include ${VSEXT_INCLUDES})

target_link_libraries(NativeEditControls PRIVATE cpptools cpptools_codegen newui lex user32.lib gdi32.lib)
add_dependencies(NativeEditControls cpptools_version)

# testharness - a plain newui::Application/newui::Frame .exe (extension/NativeEditControls/
# testharness/testharness.cpp) for exercising NativeEditControls' editing code directly, without
# going through VS/the managed VSIX layer at all. Compiles NativeEditor.cpp/CppEditor.cpp/
# DesignerEditor.cpp/TextEncoding.cpp/Logging.cpp directly into this .exe rather than linking
# NativeEditControls.dll - NativeEditor/NativeEditManager/CppEditor/DesignerEditor are plain
# classes, never __declspec(dllexport)'d (only the extern "C" functions in
# NativeEditControlApi.cpp are), so the DLL's import library wouldn't expose them for this
# harness to call directly the way it does (createEditor(newui::RootView*, ...), editor->load()/
# save()) - see NativeEditor.h's own comment on that overload.
add_executable(testharness
    extension/NativeEditControls/testharness/testharness.cpp
    extension/NativeEditControls/NativeEditor.cpp
    extension/NativeEditControls/CppEditor.cpp
    extension/NativeEditControls/DesignerEditor.cpp
    extension/NativeEditControls/PropertyEditor.cpp
    extension/NativeEditControls/CalloutPlacement.cpp
    extension/NativeEditControls/ViewStyleRegistry.cpp
    extension/NativeEditControls/GradientEditorDialog.cpp
    extension/NativeEditControls/ColorPicker.cpp
    extension/NativeEditControls/ColorEditorDialog.cpp
    extension/NativeEditControls/PaintUtils.cpp
    extension/NativeEditControls/ColorChoices.cpp
    extension/NativeEditControls/ComponentEditor.cpp
    extension/NativeEditControls/MenuDesigner.cpp
    extension/NativeEditControls/SourceView.cpp
    extension/NativeEditControls/HighlightController.cpp
    extension/NativeEditControls/IncludeManager.cpp
    extension/NativeEditControls/IncludePlanner.cpp
    extension/NativeEditControls/NewControllerDialog.cpp
    extension/NativeEditControls/ControllerWiringAction.cpp
    extension/NativeEditControls/ControllerBindings.cpp
    extension/NativeEditControls/ControllerRef.cpp
    extension/NativeEditControls/WiringRequestBuilder.cpp
    extension/NativeEditControls/CodegenEditAdapter.cpp
    extension/NativeEditControls/HostEditorBridge.cpp
    extension/NativeEditControls/DocumentEditService.cpp
    extension/NativeEditControls/FindEngine.cpp
    extension/NativeEditControls/FindReplaceController.cpp
    extension/NativeEditControls/MinimapStrip.cpp
    extension/NativeEditControls/CppHighlight.cpp
    extension/NativeEditControls/CppDiagnostics.cpp
    extension/NativeEditControls/DesignerClipboard.cpp
    extension/NativeEditControls/DesignerArrange.cpp
    extension/NativeEditControls/ToolboxRegistry.cpp
    extension/NativeEditControls/Toolbox.cpp
    extension/NativeEditControls/Workspace.cpp
    extension/NativeEditControls/CanvasWell.cpp
    extension/NativeEditControls/ViewDesignerController.cpp
    extension/NativeEditControls/ViewDesignerModel.cpp
    extension/NativeEditControls/PropertiesModel.cpp
    extension/NativeEditControls/PropertyItem.cpp
    extension/NativeEditControls/PropertiesGrid.cpp
    extension/NativeEditControls/IssuesPopup.cpp
    extension/NativeEditControls/IssuePeek.cpp
    extension/NativeEditControls/DocumentTabs.cpp
    extension/NativeEditControls/PropertiesPanel.cpp
    extension/NativeEditControls/DocumentOutline.cpp
    extension/NativeEditControls/SelectionOverlay.cpp
    extension/NativeEditControls/LayoutEditingPolicy.cpp
    extension/NativeEditControls/DirectoryTree.cpp
    extension/NativeEditControls/TextEncoding.cpp
    extension/NativeEditControls/Logging.cpp)

target_compile_definitions(testharness PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_include_directories(testharness PRIVATE include ${VSEXT_INCLUDES})

target_link_libraries(testharness PRIVATE cpptools cpptools_codegen newui lex user32.lib gdi32.lib comdlg32.lib)
add_dependencies(testharness cpptools_version)

cpptools_copy_libclang_dll(NativeEditControls)
cpptools_copy_libclang_dll(testharness)

# Icon/image resources (extension/NativeEditControls/Resources/Images/...) - pulled from the
# bluesky/ mockups, resolved at runtime via newui::Bundle::loadImage()/resourcesDir(). Copied via
# newui's own newui_add_resources() helper (3rdparty/newui/CMakeLists.txt) for both the real DLL
# and testharness, since Bundle::resourcesDir() is always <exe dir>/Resources - not shared between
# targets, so both need their own copy next to their own .exe/.dll.
newui_add_resources(NativeEditControls "${CMAKE_CURRENT_SOURCE_DIR}/extension/NativeEditControls/Resources")
newui_add_resources(testharness "${CMAKE_CURRENT_SOURCE_DIR}/extension/NativeEditControls/Resources")

# GoogleTest - Testing Framework
# ==============================
# Vendored under 3rdparty/ as a permanent part of the project (copied from newui's own vendored
# copy, release v1.17.0) - same setup as newui's own top-level CMakeLists.txt. Guarded by
# `if(NOT TARGET gtest_main)` because newui's own CMakeLists.txt (pulled in above via
# FetchContent) vendors and add_subdirectory()s its own copy of googletest too - EXCLUDE_FROM_ALL
# on the newui directory keeps its targets out of the default build, but doesn't stop CMake from
# *defining* them at configure time, so without this guard the two copies collide on the same
# target names (CMP0002). Whichever copy configures first wins; either is fine since both are the
# same googletest release.

if(NOT TARGET gtest_main)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    add_subdirectory(3rdparty/googletest)
endif()

enable_testing()
add_subdirectory(unittests)
