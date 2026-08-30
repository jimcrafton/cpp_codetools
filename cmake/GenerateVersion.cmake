# Regenerates cpptools/version.h with an incremented, rolling-over build number - same scheme
# newui uses (see D:\code\newui\cmake\GenerateVersion.cmake, which this mirrors). Run via
# `cmake -P` from a custom build step (see root CMakeLists.txt's cpp_codetools_version target)
# rather than at `cmake` configure time, so BUILD advances on every build invocation, not just
# the first configure.
#
# Versioning scheme: Major.Minor.Release.Build, each build increments Build by 1. Build rolls
# over at 9999 (resets to 0, Release += 1); Release rolls over at 999 (resets to 0, Minor += 1);
# Minor rolls over at 99 (resets to 0, Major += 1, no ceiling on Major). So the *current*
# Major/Minor/Release aren't fixed - they're state that can advance purely from accumulated
# builds, which is why they're persisted in STATE_FILE (tracked in git, at the repo root - not a
# build-tree file, precisely so a `build/` wipe-and-reconfigure doesn't reset version progression
# back to the INITIAL_* seed).
#
# Expected -D arguments: INITIAL_MAJOR, INITIAL_MINOR, INITIAL_RELEASE (used only the very first
# time, when STATE_FILE doesn't exist yet), STATE_FILE, IN_FILE, OUT_FILE.

set(CPPCODETOOLS_BUILD_ROLLOVER 9999)
set(CPPCODETOOLS_RELEASE_ROLLOVER 999)
set(CPPCODETOOLS_MINOR_ROLLOVER 99)

if(EXISTS "${STATE_FILE}")
    file(READ "${STATE_FILE}" STATE)
    string(STRIP "${STATE}" STATE)
    string(REPLACE "." ";" STATE_LIST "${STATE}")
    list(GET STATE_LIST 0 cpptools_VERSION_MAJOR)
    list(GET STATE_LIST 1 cpptools_VERSION_MINOR)
    list(GET STATE_LIST 2 cpptools_VERSION_RELEASE)
    list(GET STATE_LIST 3 cpptools_VERSION_BUILD)
else()
    set(cpptools_VERSION_MAJOR ${INITIAL_MAJOR})
    set(cpptools_VERSION_MINOR ${INITIAL_MINOR})
    set(cpptools_VERSION_RELEASE ${INITIAL_RELEASE})
    set(cpptools_VERSION_BUILD 0)
endif()

math(EXPR cpptools_VERSION_BUILD "${cpptools_VERSION_BUILD} + 1")
if(cpptools_VERSION_BUILD GREATER ${CPPCODETOOLS_BUILD_ROLLOVER})
    set(cpptools_VERSION_BUILD 0)
    math(EXPR cpptools_VERSION_RELEASE "${cpptools_VERSION_RELEASE} + 1")
    if(cpptools_VERSION_RELEASE GREATER ${CPPCODETOOLS_RELEASE_ROLLOVER})
        set(cpptools_VERSION_RELEASE 0)
        math(EXPR cpptools_VERSION_MINOR "${cpptools_VERSION_MINOR} + 1")
        if(cpptools_VERSION_MINOR GREATER ${CPPCODETOOLS_MINOR_ROLLOVER})
            set(cpptools_VERSION_MINOR 0)
            math(EXPR cpptools_VERSION_MAJOR "${cpptools_VERSION_MAJOR} + 1")
        endif()
    endif()
endif()

set(cpptools_VERSION "${cpptools_VERSION_MAJOR}.${cpptools_VERSION_MINOR}.${cpptools_VERSION_RELEASE}.${cpptools_VERSION_BUILD}")
file(WRITE "${STATE_FILE}" "${cpptools_VERSION}")

configure_file("${IN_FILE}" "${OUT_FILE}" @ONLY)
