#
# Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.10)

project(shaderproj)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if (MSVC)
	macro(replace_msvcrt var value)
		string(REGEX REPLACE "/M[TD]d?\\s*" "" ${var} ${${var}})
		set(${var} "${${var}} ${value}")
	endmacro(replace_msvcrt)
	
	replace_msvcrt(CMAKE_C_FLAGS_DEBUG "/MTd")
	replace_msvcrt(CMAKE_C_FLAGS_MINSIZEREL "/MT")
	replace_msvcrt(CMAKE_C_FLAGS_RELEASE "/MT")
	replace_msvcrt(CMAKE_C_FLAGS_RELWITHDEBINFO "/MT")

	replace_msvcrt(CMAKE_CXX_FLAGS_DEBUG "/MTd")
	replace_msvcrt(CMAKE_CXX_FLAGS_MINSIZEREL "/MT")
	replace_msvcrt(CMAKE_CXX_FLAGS_RELEASE "/MT")
	replace_msvcrt(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT")

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

	# disable the deprecation warnings in JsonCpp
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")

endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

option(BUILD_EXTERNAL "" OFF)
option(SKIP_GLSLANG_INSTALL "" ON)
option(ENABLE_SPVREMAPPER "" OFF)
option(ENABLE_GLSLANG_BINARIES "" ON)
option(ENABLE_HLSL "" OFF)
option(ENABLE_CTEST "" OFF)
option(OVERRIDE_MSVCCRT "" OFF)

add_subdirectory(glslang)

add_subdirectory(Vulkan-Headers)

option(GLFW_BUILD_EXAMPLES "" OFF)
option(GLFW_BUILD_TESTS "" OFF)
option(GLFW_BUILD_DOCS "" OFF)
option(GLFW_INSTALL "" OFF)

add_subdirectory(glfw)

include(jsoncpp.cmake)

add_subdirectory(src)
