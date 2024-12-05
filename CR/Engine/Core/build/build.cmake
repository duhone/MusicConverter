block()

set(root "${CMAKE_CURRENT_LIST_DIR}/..")

###############################################
#library
###############################################
set(CR_INTERFACE_HEADERS
    ${root}/interface/core/Log.hpp
    ${root}/interface/core/Reflection.hpp
)

set(CR_INTERFACE_MODULES
    ${root}/interface/Algorithm.ixx
    ${root}/interface/BinaryStream.ixx
    ${root}/interface/BitSet.ixx
    ${root}/interface/Core.ixx
    ${root}/interface/EightCC.ixx
    ${root}/interface/Embedded.ixx
    ${root}/interface/FileHandle.ixx
    ${root}/interface/Function.ixx
    ${root}/interface/Guid.ixx
    ${root}/interface/Handle.ixx
    ${root}/interface/Hash.ixx
    ${root}/interface/Literals.ixx
    ${root}/interface/Locked.ixx
    ${root}/interface/Log.ixx
    ${root}/interface/Random.ixx
    ${root}/interface/Rect.ixx
    ${root}/interface/ScopeExit.ixx
    ${root}/interface/ServiceLocator.ixx
    ${root}/interface/Services.ixx
    ${root}/interface/StorageBuffer.ixx
    ${root}/interface/Table.ixx
    ${root}/interface/Timer.ixx
    ${root}/interface/TypeTraits.ixx
)

set(CR_IMPLEMENTATION
    ${root}/implementation/Log.cxx
    ${root}/implementation/Random.cxx
    ${root}/implementation/Timer.cxx
)

set(CR_BUILD_FILES
    ${root}/build/build.cmake
)

add_library(core)

settingsCR(core)

target_link_libraries(core PUBLIC
    fmt::fmt
    glm::glm
    spdlog::spdlog
)

target_include_directories(core SYSTEM PUBLIC "${root}/interface")

set_property(TARGET core APPEND PROPERTY FOLDER Engine/Packages)

endblock()
