block()

set(root "${CMAKE_CURRENT_LIST_DIR}/..")

###############################################
#library
###############################################
set(INTERFACE_FILES
    ${root}/interface/MemoryMappedFile.ixx
    ${root}/interface/PathUtils.ixx
    ${root}/interface/Platform.ixx
    ${root}/interface/platform/windows/CRWindows.h
)

set(SOURCE_FILES
    ${root}/implementation/windows/MemoryMappedFile.cxx
    ${root}/implementation/windows/PathUtils.cxx
)

set(BUILD_FILES
    ${root}/build/build.cmake
)

add_library(platform 
  ${INTERFACE_FILES} 
  ${SOURCE_FILES} 
  ${BUILD_FILES}
)

settingsCR(platform)

target_link_libraries(platform PUBLIC
	core
)

target_include_directories(platform SYSTEM PUBLIC "${root}/interface")

set_property(TARGET platform APPEND PROPERTY FOLDER Engine/Packages)
	
endblock()