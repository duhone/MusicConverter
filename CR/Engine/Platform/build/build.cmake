block()

set(root "${CMAKE_CURRENT_LIST_DIR}/..")

###############################################
#library
###############################################
set(CR_INTERFACE_HEADERS
    ${root}/interface/platform/windows/CRWindows.h 
)

set(CR_INTERFACE_MODULES
    ${root}/interface/MemoryMappedFile.ixx
    ${root}/interface/PathUtils.ixx
    ${root}/interface/Platform.ixx
)

set(CR_IMPLEMENTATION
    ${root}/implementation/windows/MemoryMappedFile.cxx
    ${root}/implementation/windows/PathUtils.cxx
)

set(CR_BUILD_FILES
    ${root}/build/build.cmake
)

add_library(platform)

settingsCR(platform)

target_link_libraries(platform PUBLIC
	core
)

target_include_directories(platform SYSTEM PUBLIC "${root}/interface")

set_property(TARGET platform APPEND PROPERTY FOLDER Engine/Packages)
	
endblock()