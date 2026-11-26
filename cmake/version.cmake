# Writes OUT = `#define WAVEOD_VERSION "..."`, the version the pedal shows (GLOBAL > SISTEMA, protocol info):
#   vX.Y.Z                a release: HEAD is the commit tagged vX.Y.Z (X.Y.Z from SRC/VERSION), nothing uncommitted
#   vX.Y.Z+<hash>         any other build (SemVer build metadata: the short git hash)
#   vX.Y.Z+<hash>.dirty   with uncommitted changes
#   vX.Y.Z+dev            no git at all
# WAVEOD_VERSION in the environment wins (tools/buildfw.py builds a revision outside the checkout). The file is
# rewritten only when it changes, so the two files that include it are rebuilt only then.
#   cmake -DSRC=<source dir> -DOUT=<header> -P cmake/version.cmake
if(DEFINED ENV{WAVEOD_VERSION})
    set(v "$ENV{WAVEOD_VERSION}")
else()
    file(STRINGS "${SRC}/VERSION" semver LIMIT_COUNT 1)
    string(STRIP "${semver}" semver)
    set(v "v${semver}")
    execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${SRC}" OUTPUT_VARIABLE hash
                    RESULT_VARIABLE r OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT r EQUAL 0 OR hash STREQUAL "")
        set(v "${v}+dev")
    else()
        execute_process(COMMAND git tag --points-at HEAD WORKING_DIRECTORY "${SRC}" OUTPUT_VARIABLE tags
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY "${SRC}"
                        OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REPLACE "\n" ";" tags "${tags}")
        list(FIND tags "${v}" tagged)
        if(tagged EQUAL -1 OR NOT changes STREQUAL "")
            set(v "${v}+${hash}")
            if(NOT changes STREQUAL "")
                set(v "${v}.dirty")
            endif()
        endif()
    endif()
endif()
set(text "#define WAVEOD_VERSION \"${v}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
else()
    set(old "")
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
endif()
