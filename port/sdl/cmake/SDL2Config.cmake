# The native SDL2 subset for the Warp port, as the SDL2::SDL2 target that
# DevilutionX's CMake looks for (DEVILUTIONX_SYSTEM_SDL2 ON, SDL2_DIR here).
if(NOT TARGET msdl)
  get_filename_component(_MSDL_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  file(GLOB _MSDL_SRC "${_MSDL_DIR}/src/*.c")
  add_library(msdl STATIC ${_MSDL_SRC})
  target_include_directories(msdl PUBLIC "${_MSDL_DIR}/sdl2inc")
  target_compile_options(msdl PRIVATE -Wall -Wno-unused-parameter -fno-lto)
  add_library(SDL2::SDL2 ALIAS msdl)
  set(SDL2_INCLUDE_DIRS "${_MSDL_DIR}/sdl2inc")
  set(SDL2_LIBRARIES msdl)
endif()
set(SDL2_FOUND TRUE)
set(SDL2_VERSION 2.33.0)
