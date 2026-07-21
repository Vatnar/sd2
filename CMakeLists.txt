cmake_minimum_required(VERSION 3.20)
project(sd2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IMGUI_TRACE_ALLOC "Trace imgui memory allocations" OFF)

add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter -Wno-unused-variable
        -fno-exceptions -freflection -mclflushopt -fno-omit-frame-pointer -march=native -flto
)

if (IMGUI_TRACE_ALLOC)
    add_compile_definitions(IMGUI_TRACE_ALLOC)
endif ()
add_library(imgui STATIC vendor/imgui_lib.cpp)
target_include_directories(imgui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8/backends)
target_compile_definitions(imgui PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PRIVATE glfw vulkan)

set(CPP_INCLUDE
        src/internal/base_arena.cpp
        src/internal/vk_helpers.cpp
        src/internal/debug_ui.cpp
)


add_executable(sd2 src/sd2_main.cpp ${CPP_INCLUDE})
set_source_files_properties(${CPP_INCLUDE} PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(sd2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui-1.92.8/backends)
target_link_libraries(sd2 PRIVATE glfw vulkan imgui)

find_program(SLANGC_EXECUTABLE slangc REQUIRED)


function(add_slang_shader_target TARGET)
    cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})
    set(SHADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets/shaders)
    set(ENTRY_POINTS -entry vert_main -entry frag_main)
    set(ALL_OUTPUTS "")
    foreach (SRC ${SHADER_SOURCES})
        get_filename_component(FILENAME ${SRC} NAME_WLE)
        set(SPV_PATH ${SHADERS_DIR}/${FILENAME}.spv)
        list(APPEND ALL_OUTPUTS ${SPV_PATH})
        add_custom_command(
                OUTPUT ${SPV_PATH}
                COMMAND ${SLANGC_EXECUTABLE} ${SRC}
                -target spirv -profile spirv_1_4 -emit-spirv-directly
                -fvk-use-entrypoint-name ${ENTRY_POINTS} -o ${SPV_PATH}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                DEPENDS ${SRC}
                COMMENT "Compiling ${SRC}"
                VERBATIM
        )
    endforeach ()
    add_custom_command(
            OUTPUT ${SHADERS_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
            DEPENDS ${ALL_OUTPUTS}
    )
    add_custom_target(${TARGET} DEPENDS ${ALL_OUTPUTS})
endfunction()

set(SHADER_SLANG_SOURCES assets/shaders/shader.slang assets/shaders/line.slang)
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