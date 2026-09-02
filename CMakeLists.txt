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
# a standalone LLVM install (https://github.com/llvm/llvm-project/releases,
# `LLVM-*-win64.exe`) - override with -DCPPCODETOOLS_LLVM_ROOT=... if yours isn't at the default.

set(CPPCODETOOLS_LLVM_ROOT "C:/Program Files/LLVM" CACHE PATH
    "Root of a standalone LLVM install containing include/clang-c, lib/libclang.lib, bin/libclang.dll")

find_path(CPPCODETOOLS_LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS "${CPPCODETOOLS_LLVM_ROOT}/include")
find_library(CPPCODETOOLS_LIBCLANG_IMPLIB NAMES libclang
    HINTS "${CPPCODETOOLS_LLVM_ROOT}/lib")
find_file(CPPCODETOOLS_LIBCLANG_DLL NAMES libclang.dll
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
    src/log.cpp
    include/cpptools/symbol.h
    include/cpptools/diagnostic.h
    include/cpptools/parser.h
    include/cpptools/log.h)

target_include_directories(cpptools PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(cpptools PUBLIC libclang::libclang)
add_dependencies(cpptools cpptools_version)

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

target_link_libraries(NativeEditControls PRIVATE cpptools newui user32.lib gdi32.lib)
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
    extension/NativeEditControls/TextEncoding.cpp
    extension/NativeEditControls/Logging.cpp)

target_compile_definitions(testharness PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_include_directories(testharness PRIVATE include ${VSEXT_INCLUDES})

target_link_libraries(testharness PRIVATE cpptools newui user32.lib gdi32.lib comdlg32.lib)
add_dependencies(testharness cpptools_version)

cpptools_copy_libclang_dll(NativeEditControls)
cpptools_copy_libclang_dll(testharness)

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
