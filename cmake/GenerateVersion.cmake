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
# STATE_FILE is a small KEY=VALUE text file, one entry per line - VERSION (the Major.Minor.Release.
# Build tuple this script itself owns) alongside a handful of product-metadata fields (AUTHOR,
# COPYRIGHT, PRODUCT_NAME, DESCRIPTION) that this script doesn't compute, only carries through
# unchanged from one run to the next so NativeEditControl.rc has a single source of truth for them
# instead of hand-duplicated literals. Splitting the whole file on "." (as a bare
# "Major.Minor.Release.Build" file allowed) would misparse COPYRIGHT's own "." characters, hence
# KEY=VALUE lines instead of a bare version string.
#
# Expected -D arguments: INITIAL_MAJOR, INITIAL_MINOR, INITIAL_RELEASE, INITIAL_AUTHOR,
# INITIAL_COPYRIGHT, INITIAL_PRODUCT_NAME, INITIAL_DESCRIPTION (used only the very first time,
# when STATE_FILE doesn't exist yet), STATE_FILE, IN_FILE, OUT_FILE.

set(CPPCODETOOLS_BUILD_ROLLOVER 9999)
set(CPPCODETOOLS_RELEASE_ROLLOVER 999)
set(CPPCODETOOLS_MINOR_ROLLOVER 99)

if(EXISTS "${STATE_FILE}")
    file(STRINGS "${STATE_FILE}" STATE_LINES)

    set(cpptools_VERSION "")
    set(cpptools_AUTHOR "${INITIAL_AUTHOR}")
    set(cpptools_COPYRIGHT "${INITIAL_COPYRIGHT}")
    set(cpptools_PRODUCT_NAME "${INITIAL_PRODUCT_NAME}")
    set(cpptools_DESCRIPTION "${INITIAL_DESCRIPTION}")

    foreach(STATE_LINE ${STATE_LINES})
        if(STATE_LINE MATCHES "^([A-Z_]+)=(.*)$")
            set(STATE_KEY "${CMAKE_MATCH_1}")
            set(STATE_VALUE "${CMAKE_MATCH_2}")
            set(cpptools_${STATE_KEY} "${STATE_VALUE}")
        endif()
    endforeach()

    string(REPLACE "." ";" VERSION_LIST "${cpptools_VERSION}")
    list(GET VERSION_LIST 0 cpptools_VERSION_MAJOR)
    list(GET VERSION_LIST 1 cpptools_VERSION_MINOR)
    list(GET VERSION_LIST 2 cpptools_VERSION_RELEASE)
    list(GET VERSION_LIST 3 cpptools_VERSION_BUILD)
else()
    set(cpptools_VERSION_MAJOR ${INITIAL_MAJOR})
    set(cpptools_VERSION_MINOR ${INITIAL_MINOR})
    set(cpptools_VERSION_RELEASE ${INITIAL_RELEASE})
    set(cpptools_VERSION_BUILD 0)
    set(cpptools_AUTHOR "${INITIAL_AUTHOR}")
    set(cpptools_COPYRIGHT "${INITIAL_COPYRIGHT}")
    set(cpptools_PRODUCT_NAME "${INITIAL_PRODUCT_NAME}")
    set(cpptools_DESCRIPTION "${INITIAL_DESCRIPTION}")
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

file(WRITE "${STATE_FILE}"
    "VERSION=${cpptools_VERSION}\n"
    "AUTHOR=${cpptools_AUTHOR}\n"
    "COPYRIGHT=${cpptools_COPYRIGHT}\n"
    "PRODUCT_NAME=${cpptools_PRODUCT_NAME}\n"
    "DESCRIPTION=${cpptools_DESCRIPTION}\n"
)

configure_file("${IN_FILE}" "${OUT_FILE}" @ONLY)
