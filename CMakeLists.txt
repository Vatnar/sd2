cmake_minimum_required(VERSION 3.20)
project(sd2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter -Wno-unused-variable
    -fno-exceptions -freflection -mclflushopt
)

add_executable(sd2 src/main.cpp)
set(SHADER_SLANG_SOURCES shaders/shader.slang)

target_include_directories(sd2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
target_link_libraries(sd2 PRIVATE glfw vulkan)

find_program(SLANGC_EXECUTABLE slangc REQUIRED)


function(add_slang_shader_target TARGET)
  cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})
  set(SHADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(ENTRY_POINTS -entry vert_main -entry frag_main)
  set(SHADER_SPV ${SHADERS_DIR}/shader.spv)
  add_custom_command(
    OUTPUT ${SHADERS_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )
  add_custom_command(
    OUTPUT ${SHADER_SPV}
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES}
            -target spirv -profile spirv_1_4 -emit-spirv-directly
            -fvk-use-entrypoint-name ${ENTRY_POINTS} -o ${SHADER_SPV}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
    COMMENT "Compiling Slang Shaders"
    VERBATIM
  )
  add_custom_target(${TARGET} DEPENDS ${SHADER_SPV})
endfunction()

add_slang_shader_target(slang_shaders SOURCES ${SHADER_SLANG_SOURCES})
add_dependencies(sd2 slang_shaders)