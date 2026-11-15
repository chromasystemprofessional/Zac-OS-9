cmake_minimum_required(VERSION 3.10)
project(ClassicOpenHelper C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
	message(FATAL_ERROR "Use Retro68's retroppc.toolchain.cmake with Multiversal Interfaces")
endif()

# Original guest helper; no Apple headers or libraries are used.
add_application(ClassicOpenHelper helper.c CREATOR "ZcOH")
target_compile_options(ClassicOpenHelper PRIVATE -Wall -Wextra -Werror)
option(CLASSIC_OPEN_DEBUG "Log bridge setup to the Unix volume" OFF)
if(CLASSIC_OPEN_DEBUG)
	target_compile_definitions(ClassicOpenHelper PRIVATE HELPER_DEBUG=1)
endif()
