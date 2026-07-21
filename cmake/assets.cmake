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
add_slang_shader_target(spirv_shaders SOURCES ${SHADER_SLANG_SOURCES})

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
