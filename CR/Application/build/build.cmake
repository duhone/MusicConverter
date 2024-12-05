block()
set(root "${CMAKE_CURRENT_LIST_DIR}/..")

set(CR_INTERFACE_HEADERS
)

set(CR_INTERFACE_MODULES
)

set(CR_IMPLEMENTATION
  ${root}/implementation/main.cpp
)

set(CR_BUILD_FILES
    ${root}/build/build.cmake
)

add_executable(MusicConverter)

settingsCR(MusicConverter)

target_link_libraries(MusicConverter PRIVATE 
	mp3lame::mp3lame
  fmt::fmt
  simdjson::simdjson
  ftxui::dom
  ftxui::screen
  ftxui::component
  SampleRate::samplerate
  engine
)
target_include_directories(MusicConverter PRIVATE ${DRLIBS_INCLUDE_DIRS})

endblock()