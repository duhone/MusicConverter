block()

set(root "${CMAKE_CURRENT_LIST_DIR}/..")

include (${root}/core/build/build.cmake)
include (${root}/Platform/build/build.cmake)

set(INTERFACE_FILES
    ${root}/interface/Engine.ixx
)

set(BUILD_FILES
    ${root}/build/build.cmake
)

add_library(engine 
    ${INTERFACE_FILES} 
    ${BUILD_FILES}
)

settingsCR(engine)

target_link_libraries(engine PUBLIC
    core
    platform
)

set_property(TARGET engine APPEND PROPERTY FOLDER Engine)

endblock()