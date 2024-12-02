set(root "${CMAKE_CURRENT_LIST_DIR}/..")

set(INTERFACE_FILES
)

set(SOURCE_FILES
    ${root}/source/main.cpp
)

set(BUILD_FILES
    ${root}/build/build.cmake
)

add_executable(MusicConverter 
  ${INTERFACE_FILES} 
  ${SOURCE_FILES} 
  ${BUILD_FILES}
)

settingsCR(MusicConverter)

target_link_libraries(MusicConverter PRIVATE 
	FLAC::FLAC 
	mp3lame::mp3lame
  fmt::fmt
  simdjson::simdjson
  ftxui::dom
  ftxui::screen
  ftxui::component
)
