cmake_minimum_required(VERSION 3.10)
project(ClassicOpenHelper C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
	message(FATAL_ERROR "Use Retro68's retroppc.toolchain.cmake with Multiversal Interfaces")
endif()

# Original guest helper; no Apple headers or libraries are used.
add_application(ClassicOpenHelper helper.c CREATOR "ZcOH")
target_compile_options(ClassicOpenHelper PRIVATE -Wall -Wextra -Werror)
