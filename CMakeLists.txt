cmake_minimum_required(VERSION 3.18)
project(herz_hook C)

set(CMAKE_C_STANDARD 11)

# Dobby sebagai static library
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
add_subdirectory(dobby)

add_library(herz_hook SHARED herz_hook.c)
target_include_directories(herz_hook PRIVATE dobby/include)
target_link_libraries(herz_hook dobby_static log dl)
