
file(GLOB sources "*.c" "*.cpp" "*.h")

add_executable(shaderproj ${sources})
target_sources(shaderproj PRIVATE "../glslang/StandAlone/ResourceLimits.cpp")

target_link_libraries(shaderproj glslang SPIRV glfw jsoncpp_static Vulkan-Headers)

if (WIN32)
	target_compile_definitions(shaderproj PRIVATE NOMINMAX)
else()
	target_link_libraries(shaderproj stdc++fs)
endif()


function(compile_shader)

    set(options "")
    set(oneValueArgs SOURCE_FILE OUTPUT_FILE OUTPUT_VAR)
    set(multiValueArgs "")
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_custom_command(
        OUTPUT ${params_OUTPUT_FILE}
        MAIN_DEPENDENCY ${params_SOURCE_FILE}
        COMMAND $<TARGET_FILE:glslangValidator> --target-env vulkan1.2 --quiet ${params_SOURCE_FILE} -o ${params_OUTPUT_FILE} --vn ${params_OUTPUT_VAR})

    target_sources(shaderproj PRIVATE ${params_OUTPUT_FILE})

endfunction()

compile_shader(
    SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.vert.glsl"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/shader-quad.h"
    OUTPUT_VAR "g_QuadVertexShader")

compile_shader(
    SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/blit.frag.glsl"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/shader-blit.h"
    OUTPUT_VAR "g_BlitFragmentShader")

target_include_directories(shaderproj PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
