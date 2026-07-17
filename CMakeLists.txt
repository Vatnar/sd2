cmake_minimum_required(VERSION 3.20)
project(sd2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IMGUI_TRACE_ALLOC "Trace imgui memory allocations" OFF)
option(SD2_TRACY_ENABLE   "Enable Tracy instrumentation" OFF)

add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter -Wno-unused-variable
        -fno-exceptions -freflection -mclflushopt -fno-omit-frame-pointer
)

if(IMGUI_TRACE_ALLOC)
  add_compile_definitions(IMGUI_TRACE_ALLOC)
endif()

set(CPP_INCLUDE
        src/internal/arena.cpp
        src/internal/vk_helpers.cpp
        src/internal/debug_ui.cpp
)

add_library(imgui STATIC vendor/imgui_lib.cpp)
target_include_directories(imgui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor
                                          ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8
                                          ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8/backends)
target_compile_definitions(imgui PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PRIVATE glfw vulkan)

add_executable(sd2 src/sd2_main.cpp ${CPP_INCLUDE})
set_source_files_properties(${CPP_INCLUDE} PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(sd2 PRIVATE vendor/tracy/public)
if(SD2_TRACY_ENABLE)
  target_compile_definitions(sd2 PRIVATE TRACY_ENABLE)
  target_sources(sd2 PRIVATE vendor/tracy/public/TracyClient.cpp)
endif()

set(SHADER_SLANG_SOURCES assets/shaders/shader.slang)

target_include_directories(sd2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor
                                       ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8
                                       ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8/backends)
target_link_libraries(sd2 PRIVATE glfw vulkan imgui)

find_program(SLANGC_EXECUTABLE slangc REQUIRED)


function(add_slang_shader_target TARGET)
    cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})
    set(SHADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets/shaders)
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

file(GLOB_RECURSE TEXTURE_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/*)
foreach (TEX ${TEXTURE_SOURCES})
    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/${TEX})
    set(DST ${CMAKE_CURRENT_BINARY_DIR}/assets/textures/${TEX})
    add_custom_command(
            OUTPUT ${DST}
            COMMAND ${CMAKE_COMMAND} -E copy ${SRC} ${DST}
            DEPENDS ${SRC}
    )
    list(APPEND TEXTURE_OUTPUTS ${DST})
endforeach ()
add_custom_target(textures DEPENDS ${TEXTURE_OUTPUTS})
add_dependencies(sd2 textures)

file(GLOB_RECURSE MODEL_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/assets/models
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/*)
foreach (MOD ${MODEL_SOURCES})
    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/${MOD})
    set(DST ${CMAKE_CURRENT_BINARY_DIR}/assets/models/${MOD})
    add_custom_command(
            OUTPUT ${DST}
            COMMAND ${CMAKE_COMMAND} -E copy ${SRC} ${DST}
            DEPENDS ${SRC}
    )
    list(APPEND MODEL_OUTPUTS ${DST})
endforeach ()
add_custom_target(models DEPENDS ${MODEL_OUTPUTS})
add_dependencies(sd2 models)

add_custom_target(run
        COMMAND sd2
        DEPENDS sd2
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
)