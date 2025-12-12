cmake_minimum_required(VERSION 3.20)
project(hxo_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# Fetch SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Engine shared library
add_library(engine SHARED
    src/engine.cpp
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine PRIVATE SDL3::SDL3 Vulkan::Vulkan)

# Copy SDL3 shared lib next to engine lib for runtime
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:engine>
)

# Shader compilation - try glslc first, fall back to glslangValidator
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
find_program(GLSLANG glslangValidator)

if(GLSLC)
    set(SHADER_COMPILER ${GLSLC})
    set(SHADER_COMPILE_CMD "${GLSLC} <INPUT> -o <OUTPUT>")
elseif(GLSLANG)
    set(SHADER_COMPILER ${GLSLANG})
    set(SHADER_COMPILE_CMD "${GLSLANG} -V <INPUT> -o <OUTPUT>")
else()
    message(FATAL_ERROR "No shader compiler found. Install glslang-tools or Vulkan SDK.")
endif()

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

set(SHADERS
    ${SHADER_DIR}/triangle.vert
    ${SHADER_DIR}/triangle.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_SPV ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    if(GLSLC)
        add_custom_command(
            OUTPUT ${SHADER_SPV}
            COMMAND ${GLSLC} ${SHADER} -o ${SHADER_SPV}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${SHADER_NAME}"
        )
    else()
        add_custom_command(
            OUTPUT ${SHADER_SPV}
            COMMAND ${GLSLANG} -V ${SHADER} -o ${SHADER_SPV}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${SHADER_NAME}"
        )
    endif()
    list(APPEND SHADER_SPVS ${SHADER_SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPVS})
add_dependencies(engine shaders)

# Copy compiled shaders next to engine lib
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADER_OUT_DIR}
        $<TARGET_FILE_DIR:engine>/shaders
)
