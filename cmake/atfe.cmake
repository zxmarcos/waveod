# Toolchain for the QME-200 firmware: Arm Toolchain for Embedded (clang + lld), Cortex-M7 with FPv5-D16.
# Where it is: ATFE_ROOT (cache or environment), else D:/DevTools/ATfE-23.1.0-Windows-x86_64.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ATFE_ROOT)
    if(DEFINED ENV{ATFE_ROOT})
        set(ATFE_ROOT "$ENV{ATFE_ROOT}")
    else()
        set(ATFE_ROOT "D:/DevTools/ATfE-23.1.0-Windows-x86_64")
    endif()
endif()
file(TO_CMAKE_PATH "${ATFE_ROOT}" ATFE_ROOT)
set(ATFE_ROOT "${ATFE_ROOT}" CACHE PATH "Arm Toolchain for Embedded")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ATFE_ROOT)
if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
endif()

set(CMAKE_C_COMPILER "${ATFE_ROOT}/bin/clang${_exe}")
set(CMAKE_ASM_COMPILER "${ATFE_ROOT}/bin/clang${_exe}")
set(CMAKE_AR "${ATFE_ROOT}/bin/llvm-ar${_exe}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ATFE_ROOT}/bin/llvm-ranlib${_exe}" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${ATFE_ROOT}/bin/llvm-objcopy${_exe}" CACHE FILEPATH "")
set(CMAKE_SIZE "${ATFE_ROOT}/bin/llvm-size${_exe}" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET armv7em-none-eabihf)
set(CMAKE_ASM_COMPILER_TARGET armv7em-none-eabihf)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# the CPU: the RT1062's Cortex-M7 with the double-precision FPU; no C library headers (config/freestanding has
# what LVGL needs), the compiler's own (stdint.h, stddef.h, ...) stay
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -ffreestanding -nostdlibinc")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -fuse-ld=lld")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

include("${CMAKE_CURRENT_LIST_DIR}/ninja.cmake")
