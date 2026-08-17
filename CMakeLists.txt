cmake_minimum_required(VERSION 3.18)
project(herz_hook C)

set(CMAKE_C_STANDARD 11)

# Dobby sebagai static library
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
# Dobby asm (closure_bridge_arm64.asm) tidak kompatibel dgn clang integrated assembler
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -fno-integrated-as")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-integrated-as")
add_subdirectory(dobby)

add_library(herz_hook SHARED herz_hook.c)
target_include_directories(herz_hook PRIVATE dobby/include)
target_link_libraries(herz_hook dobby_static log dl)
