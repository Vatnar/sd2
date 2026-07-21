cmake_minimum_required(VERSION 3.20)
project(sd2 LANGUAGES CXX)

include(cmake/deps.cmake)

# sd2
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CPP_INCLUDE
        src/internal/base_arena.cpp
        src/internal/vk_helpers.cpp
        src/internal/debug_ui.cpp
)
add_executable(sd2 src/sd2_main.cpp ${CPP_INCLUDE})
set_source_files_properties(${CPP_INCLUDE} PROPERTIES HEADER_FILE_ONLY TRUE)

target_compile_options(sd2 PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter -Wno-unused-variable
        -fno-exceptions -freflection -mclflushopt -fno-omit-frame-pointer -march=native -flto
)

target_include_directories(sd2 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(sd2 PRIVATE
        glfw
        vulkan
        imgui
        glm
        stb
        tinyobjloader
)

include(cmake/assets.cmake)
add_dependencies(sd2 spirv_shaders)
add_dependencies(sd2 textures)
add_dependencies(sd2 models)