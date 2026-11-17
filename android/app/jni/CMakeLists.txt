cmake_minimum_required(VERSION 3.18)
project(TargetSpeed C)

# SDL2 source is fetched into third_party/SDL2 by scripts/setup_android.sh
set(SDL2_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/SDL2")
if(NOT EXISTS "${SDL2_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 not found at ${SDL2_DIR}. Run scripts/setup_android.sh first.")
endif()

add_subdirectory("${SDL2_DIR}" SDL2 EXCLUDE_FROM_ALL)

# SDLActivity loads "libmain.so" by default and calls SDL_main() inside it.
add_library(main SHARED "${CMAKE_CURRENT_LIST_DIR}/../../../src/main.c")
target_compile_options(main PRIVATE -Wall -Wextra)
target_link_libraries(main PRIVATE SDL2::SDL2 m)
