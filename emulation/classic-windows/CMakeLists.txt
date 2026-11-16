cmake_minimum_required(VERSION 3.10)
project(ClassicWindows C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
  message(FATAL_ERROR "Use Retro68's retroppc.toolchain.cmake with Multiversal Interfaces")
endif()

add_application(ClassicWindowAgent agent.c agent.r CREATOR "ZcWA")
target_compile_options(ClassicWindowAgent PRIVATE -Wall -Wextra -Werror)
