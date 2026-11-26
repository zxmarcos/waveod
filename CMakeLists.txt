# waveod build: firmware for the QME-200 (Cortex-M7, Arm Toolchain for Embedded) and the PC builds (simulator, unit
# tests; clang), from the same sources. Two build trees, one per toolchain (CMakePresets.json):
#
#   cmake --preset fw   && cmake --build --preset fw     build/fw/waveod.elf        (pack it with tools/mkfw.py)
#   cmake --preset host && cmake --build --preset host   build/host/waveod-sim.exe  (renders the UI to PNG files)
#                                                        build/host/waveod-tests.exe (the unit tests)
cmake_minimum_required(VERSION 3.25)
project(waveod C)

include(cmake/sources.cmake)
list(TRANSFORM WAVEOD_APP_SOURCES PREPEND src/)
list(TRANSFORM WAVEOD_BOARD_SOURCES PREPEND src/)
list(TRANSFORM WAVEOD_HOST_SOURCES PREPEND src/)
list(TRANSFORM WAVEOD_TESTED_SOURCES PREPEND src/)
list(TRANSFORM WAVEOD_TEST_SOURCES PREPEND tests/)
list(TRANSFORM WAVEOD_VERSION_SOURCES PREPEND src/)

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/lvgl/src/*.c")
if(NOT LVGL_SOURCES)
    message(FATAL_ERROR "third_party/lvgl is missing: git submodule update --init")
endif()

set(WAVEOD_FW OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(WAVEOD_FW ON)
endif()

# the optimisation is set here, per target, not by the build type
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_MINSIZEREL "")

set(COMMON_FLAGS
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing
    # separate multiply and add, like the original's vmla / vmls: the same rounding in the DSP code
    -ffp-contract=off
    -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE)
set(LVGL_FLAGS -std=gnu11 -w -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE)
set(APP_INCLUDES src config third_party/lvgl)
set(LVGL_INCLUDES config third_party/lvgl src/ui)

# the version (cmake/version.cmake), rewritten at every build when it changes, included by the files that show it
set(VERSION_H "${CMAKE_BINARY_DIR}/generated/waveod_version.h")
add_custom_target(waveod_version
    COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${VERSION_H} -P "${CMAKE_SOURCE_DIR}/cmake/version.cmake"
    BYPRODUCTS "${VERSION_H}" VERBATIM)
set_source_files_properties(${WAVEOD_VERSION_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${VERSION_H}" OBJECT_DEPENDS "${VERSION_H}")

if(WAVEOD_FW)
    # ---- firmware
    set(FW_OPT -O2 -DNDEBUG -fomit-frame-pointer -gdwarf-4)
    set(FW_FLAGS
        -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -fno-unwind-tables
        -fno-asynchronous-unwind-tables -fno-stack-protector -fno-sanitize=all)
    set(FW_LD "${CMAKE_SOURCE_DIR}/src/board/qme200/qme200.ld")

    # compiler-rt for this CPU (the multilib the flags select)
    separate_arguments(_cpu UNIX_COMMAND "${CMAKE_C_FLAGS}")
    execute_process(COMMAND "${CMAKE_C_COMPILER}" --target=${CMAKE_C_COMPILER_TARGET} ${_cpu} -print-libgcc-file-name
                    OUTPUT_VARIABLE BUILTINS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    file(TO_CMAKE_PATH "${BUILTINS}" BUILTINS)

    # LVGL stays out of the link-time optimisation: the linker script puts liblvgl.a's code in the SDRAM by file name
    add_library(lvgl STATIC ${LVGL_SOURCES})
    target_include_directories(lvgl PRIVATE ${LVGL_INCLUDES} config/freestanding)
    target_compile_options(lvgl PRIVATE ${FW_OPT} ${LVGL_FLAGS} -ffunction-sections -fdata-sections -fno-sanitize=all)

    add_executable(waveod ${WAVEOD_APP_SOURCES} ${WAVEOD_BOARD_SOURCES})
    add_dependencies(waveod waveod_version)
    # the flash lock test command (editor/wv_msgs.c, 0x1F00001F): test builds only (preset fw-locktest)
    option(WAVEOD_LOCK_TEST "flash lock test command, never for a release" OFF)
    if(WAVEOD_LOCK_TEST)
        target_compile_definitions(waveod PRIVATE WAVEOD_LOCK_TEST=1)
    endif()
    set_target_properties(waveod PROPERTIES OUTPUT_NAME waveod.elf LINK_DEPENDS "${FW_LD}")
    target_include_directories(waveod PRIVATE ${APP_INCLUDES} config/freestanding)
    target_compile_options(waveod PRIVATE ${FW_OPT} -flto ${COMMON_FLAGS} ${FW_FLAGS})
    target_link_options(waveod PRIVATE -flto -Wl,--lto-O3 -Wl,-O3 -T "${FW_LD}" -Wl,--gc-sections
                        -Wl,--entry=reset_handler -Wl,-Map=${CMAKE_BINARY_DIR}/waveod.map)
    target_link_libraries(waveod PRIVATE lvgl "${BUILTINS}")
    # the C and math library: native code, so the calls the compiler generates find them after the LTO
    set_source_files_properties(src/board/qme200/libc.c src/board/qme200/libm.c PROPERTIES COMPILE_OPTIONS "-fno-lto")
    set_property(SOURCE src/board/qme200/libm.c APPEND PROPERTY COMPILE_OPTIONS "-fno-math-errno")
    add_custom_command(TARGET waveod POST_BUILD COMMAND "${CMAKE_SIZE}" -A "$<TARGET_FILE:waveod>" VERBATIM)
else()
    # ---- PC builds
    # this PC's CPU (fmaf is the FMA instruction, as on the pedal)
    set(HOST_OPT -O2 -g -march=native)
    set(HOST_FLAGS -DWAVEOD_HOST)
    if(WIN32)
        list(APPEND HOST_FLAGS -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES)
    endif()
    # undefined behaviour traps in waveod's code (not LVGL's)
    set(HOST_UB -fsanitize=undefined -fsanitize-trap=undefined)

    add_library(lvgl_host STATIC ${LVGL_SOURCES})
    target_include_directories(lvgl_host PRIVATE ${LVGL_INCLUDES})
    target_compile_options(lvgl_host PRIVATE ${HOST_OPT} ${LVGL_FLAGS} ${HOST_FLAGS})

    add_executable(waveod-sim ${WAVEOD_APP_SOURCES} ${WAVEOD_HOST_SOURCES})
    add_dependencies(waveod-sim waveod_version)
    target_include_directories(waveod-sim PRIVATE ${APP_INCLUDES})
    target_compile_options(waveod-sim PRIVATE ${HOST_OPT} ${HOST_UB} ${COMMON_FLAGS} ${HOST_FLAGS})
    target_link_libraries(waveod-sim PRIVATE lvgl_host)

    add_executable(waveod-tests ${WAVEOD_TESTED_SOURCES} ${WAVEOD_TEST_SOURCES})
    target_include_directories(waveod-tests PRIVATE ${APP_INCLUDES} tests)
    target_compile_options(waveod-tests PRIVATE ${HOST_OPT} ${HOST_UB} ${COMMON_FLAGS} ${HOST_FLAGS})

    # cmake --build --preset host --target check   (the tests that need the original firmware: tools/wvbuild.py)
    add_custom_target(check COMMAND waveod-tests WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" USES_TERMINAL VERBATIM)
endif()
