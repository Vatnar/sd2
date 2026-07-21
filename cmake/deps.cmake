file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.1/get_cpm.cmake
        ${CMAKE_BINARY_DIR}/cmake/get_cpm.cmake
        SHOW_PROGRESS
)
include(${CMAKE_BINARY_DIR}/cmake/get_cpm.cmake)

##~ Dependencies
CPMAddPackage(
        NAME glm
        GITHUB_REPOSITORY g-truc/glm
        GIT_TAG 1.0.3
)

CPMAddPackage(
        NAME imgui
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG v1.92.8
)
if (imgui_ADDED)
    add_library(imgui STATIC
            "${imgui_SOURCE_DIR}/imgui.cpp"
            "${imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${imgui_SOURCE_DIR}/imgui_demo.cpp"
            "${imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${imgui_SOURCE_DIR}/imgui_widgets.cpp"

            "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
            "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    )

    target_include_directories(imgui PUBLIC
            "${imgui_SOURCE_DIR}"
            "${imgui_SOURCE_DIR}/backends"
    )

    target_compile_definitions(imgui PRIVATE IMGUI_DEFINE_MATH_OPERATORS)

    target_link_libraries(imgui PRIVATE glfw vulkan)
endif ()

CPMAddPackage(
        NAME stb
        GITHUB_REPOSITORY nothings/stb
        GIT_TAG master
        DOWNLOAD_ONLY YES
)

if (stb_ADDED)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE
            "${stb_SOURCE_DIR}"
    )
endif ()
CPMAddPackage(
        NAME tinyobjloader
        GITHUB_REPOSITORY tinyobjloader/tinyobjloader
        VERSION 1.0.6
        DOWNLOAD_ONLY YES
)

if (tinyobjloader_ADDED)
    add_library(tinyobjloader INTERFACE)
    target_include_directories(tinyobjloader INTERFACE
            "${tinyobjloader_SOURCE_DIR}"
    )
endif ()
