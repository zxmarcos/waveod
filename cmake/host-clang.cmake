# Toolchain for the PC builds (simulator, unit tests): clang, GNU-style driver, against the MSVC C runtime on
# Windows. Which clang: WAVEOD_HOST_CC (cache or environment), else the one Visual Studio installs (component
# "C++ Clang Compiler for Windows", VC/Tools/Llvm/x64/bin), else clang on the PATH.
if(NOT WAVEOD_HOST_CC AND DEFINED ENV{WAVEOD_HOST_CC})
    set(WAVEOD_HOST_CC "$ENV{WAVEOD_HOST_CC}")
endif()
if(NOT WAVEOD_HOST_CC)
    set(_hints "")
    if(CMAKE_HOST_WIN32)
        set(_pf "$ENV{ProgramFiles}")
        file(GLOB _vs LIST_DIRECTORIES true "${_pf}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin")
        list(APPEND _hints ${_vs})
    endif()
    find_program(WAVEOD_HOST_CC clang HINTS ${_hints} NO_CACHE)
    if(NOT WAVEOD_HOST_CC)
        message(FATAL_ERROR "clang not found: install Visual Studio's \"C++ Clang Compiler for Windows\" component "
                            "or set WAVEOD_HOST_CC")
    endif()
endif()
set(WAVEOD_HOST_CC "${WAVEOD_HOST_CC}" CACHE FILEPATH "clang for the PC builds")
set(CMAKE_C_COMPILER "${WAVEOD_HOST_CC}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WAVEOD_HOST_CC)

include("${CMAKE_CURRENT_LIST_DIR}/ninja.cmake")
