cmake_minimum_required (VERSION 3.31)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(MusicConverter)

include (${CMAKE_CURRENT_LIST_DIR}/common.cmake)
include (${CMAKE_CURRENT_LIST_DIR}/3rdParty/build/build.cmake)
include (${CMAKE_CURRENT_LIST_DIR}/CR/Application/build/build.cmake)
