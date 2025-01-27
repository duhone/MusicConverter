block()

set(root "${CMAKE_CURRENT_LIST_DIR}/..")

set(CR_INTERFACE_HEADERS
    ${root}/libopusenc/include/opusenc.h
)

set(CR_INTERFACE_MODULES
)

set(CR_IMPLEMENTATION
    ${root}/libopusenc/src/ogg_packer.c
    ${root}/libopusenc/src/opus_header.c
    ${root}/libopusenc/src/opusenc.c
    ${root}/libopusenc/src/picture.c
    ${root}/libopusenc/src/resample.c
    ${root}/libopusenc/src/unicode_support.c
)

set(CR_BUILD_FILES
    ${root}/build/build.cmake
)

add_library(libopusenc OBJECT)

settings3rdParty(libopusenc)

target_compile_definitions(libopusenc PRIVATE
    RANDOM_PREFIX=libopusenc
    OUTSIDE_SPEEX
    FLOATING_POINT
    PACKAGE_VERSION="0.2.1"
    PACKAGE_NAME="libopusenc"
    OPE_BUILD)

target_include_directories(libopusenc SYSTEM PRIVATE "${root}/libopusenc/src")
target_include_directories(libopusenc SYSTEM PUBLIC "${root}/libopusenc/include")
target_link_libraries(libopusenc PUBLIC 
  Opus::opus
)

endblock()