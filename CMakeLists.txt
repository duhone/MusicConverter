cmake_minimum_required (VERSION 3.31 FATAL_ERROR)

set(root "${CMAKE_CURRENT_LIST_DIR}")

# Set experimental flag to enable `import std` support from CMake.
# This must be enabled before C++ language support.
set(CMAKE_EXPERIMENTAL_IMPORT_STD
  # This specific value changes as experimental support evolves. See
  # `Help/dev/experimental.rst` in the CMake source corresponding to
  # your CMake build for the exact value to use.
  "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(MusicConverter LANGUAGES CXX C)

include (${root}/common.cmake)
include (${root}/3rdParty/build/build.cmake)
include (${root}/CR/Engine/build/build.cmake)
include (${root}/CR/Application/build/build.cmake)
