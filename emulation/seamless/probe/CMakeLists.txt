cmake_minimum_required(VERSION 3.10)
project(ClassicWindowProbe C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
  message(FATAL_ERROR "Use Retro68's retroppc.toolchain.cmake with Multiversal Interfaces")
endif()

add_application(ClassicWindowProbe probe.c probe.r)
target_compile_definitions(ClassicWindowProbe PRIVATE CLASSIC_PROBE_CAPTURE=1)
target_compile_options(ClassicWindowProbe PRIVATE -Wall -Wextra -Werror)

add_application(ClassicDrawingFixture probe.c probe.r CREATOR "ZcDF")
target_compile_options(ClassicDrawingFixture PRIVATE -Wall -Wextra -Werror)

add_application(ClassicCaptureHelper helper.c probe.r CREATOR "ZcSH")
target_compile_options(ClassicCaptureHelper PRIVATE -Wall -Wextra -Werror)
