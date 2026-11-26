# Ninja for both builds: CMAKE_MAKE_PROGRAM if given, else ninja on the PATH, else the one Visual Studio installs
# next to its CMake (Common7/IDE/CommonExtensions/Microsoft/CMake/{CMake/bin,Ninja}).
if(NOT CMAKE_MAKE_PROGRAM)
    get_filename_component(_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
    find_program(CMAKE_MAKE_PROGRAM ninja HINTS "${_cmake_bin}/../../Ninja")
endif()
