# Copyright (c) 2016-2025, Sascha Willems
# SPDX-License-Identifier: MIT

# Function for building single example
function(buildExample EXAMPLE_NAME)
	SET(EXAMPLE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_NAME})
	message(STATUS "Generating project file for example in ${EXAMPLE_FOLDER}")
	# Main
	file(GLOB SOURCE *.cpp ${BASE_HEADERS} ${EXAMPLE_FOLDER}/*.cpp)
	SET(MAIN_CPP ${EXAMPLE_FOLDER}/${EXAMPLE_NAME}.cpp)
	if(EXISTS ${EXAMPLE_FOLDER}/main.cpp)
		SET(MAIN_CPP ${EXAMPLE_FOLDER}/main.cpp)
	ENDIF()
	if(EXISTS ${EXAMPLE_FOLDER}/${EXAMPLE_NAME}.h)
		SET(MAIN_HEADER ${EXAMPLE_FOLDER}/${EXAMPLE_NAME}.h)
	ENDIF()
	# imgui example requires additional source files
	IF(${EXAMPLE_NAME} STREQUAL "imgui")
		file(GLOB ADD_SOURCE "../external/imgui/*.cpp")
		SET(SOURCE ${SOURCE} ${ADD_SOURCE})
	ENDIF()
	# wayland requires additional source files
	IF(USE_WAYLAND_WSI)
		SET(SOURCE ${SOURCE} ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c)
	ENDIF()
	# Add shaders
	set(SHADER_DIR_GLSL_SAMPLE "../shaders/samples/${EXAMPLE_NAME}")
	file(GLOB SHADERS_GLSL_SAMPLE "${SHADER_DIR_GLSL_SAMPLE}/*.vert" "${SHADER_DIR_GLSL_SAMPLE}/*.frag" "${SHADER_DIR_GLSL_SAMPLE}/*.comp" "${SHADER_DIR_GLSL_SAMPLE}/*.geom" "${SHADER_DIR_GLSL_SAMPLE}/*.tesc" "${SHADER_DIR_GLSL_SAMPLE}/*.tese" "${SHADER_DIR_GLSL_SAMPLE}/*.mesh" "${SHADER_DIR_GLSL_SAMPLE}/*.task" "${SHADER_DIR_GLSL_SAMPLE}/*.rgen" "${SHADER_DIR_GLSL_SAMPLE}/*.rchit" "${SHADER_DIR_GLSL_SAMPLE}/*.rmiss" "${SHADER_DIR_GLSL_SAMPLE}/*.rcall" "${SHADER_DIR_GLSL_SAMPLE}/*.rahit" "${SHADER_DIR_GLSL_SAMPLE}/*.rint" "${SHADER_DIR_GLSL_SAMPLE}/*.glsl")
	source_group("Shaders" FILES ${SHADERS_GLSL_SAMPLE})
	# Add optional readme / tutorial
	file(GLOB README_FILES "${EXAMPLE_FOLDER}/*.md")
	       if(WIN32)
		       add_executable(${EXAMPLE_NAME} WIN32 ${MAIN_CPP} ${SOURCE} ${MAIN_HEADER} ${SHADERS_GLSL_SAMPLE} ${README_FILES})
		       target_link_libraries(${EXAMPLE_NAME} base ${Vulkan_LIBRARY} ${WINLIBS})
	       else(WIN32)
		       add_executable(${EXAMPLE_NAME} ${MAIN_CPP} ${SOURCE} ${MAIN_HEADER} ${SHADERS_GLSL_SAMPLE} ${README_FILES})
		       target_link_libraries(${EXAMPLE_NAME} base )
	       endif(WIN32)

	file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
	set_target_properties(${EXAMPLE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
	if(${EXAMPLE_NAME} STREQUAL "texture3d")
		if(APPLE)
			# SRS - Use MacPorts paths as default since the same on x86 and Apple Silicon, can override for homebrew on cmake command line
			if(NOT OpenMP_omp_LIBRARY AND EXISTS /opt/local/lib/libomp/libomp.dylib)
				set(OpenMP_omp_LIBRARY /opt/local/lib/libomp/libomp.dylib)
			endif()
			if(CMAKE_C_COMPILER_ID MATCHES "Clang\$")
				set(OpenMP_C_FLAGS "-Xclang -fopenmp")
				set(OpenMP_C_LIB_NAMES "omp")
				if(NOT OpenMP_C_INCLUDE_DIR AND EXISTS /opt/local/include/libomp)
					set(OpenMP_C_INCLUDE_DIR /opt/local/include/libomp)
				endif()
			endif()
			if(CMAKE_CXX_COMPILER_ID MATCHES "Clang\$")
				set(OpenMP_CXX_FLAGS "-Xclang -fopenmp")
				set(OpenMP_CXX_LIB_NAMES "omp")
				if(NOT OpenMP_CXX_INCLUDE_DIR AND EXISTS /opt/local/include/libomp)
					set(OpenMP_CXX_INCLUDE_DIR /opt/local/include/libomp)
				endif()
			endif()
		endif()
		find_package(OpenMP)
		if(OpenMP_CXX_FOUND)
			set_target_properties(${EXAMPLE_NAME} PROPERTIES COMPILE_FLAGS ${OpenMP_CXX_FLAGS})
			target_include_directories(${EXAMPLE_NAME} PRIVATE ${OpenMP_CXX_INCLUDE_DIRS})
			target_link_libraries(${EXAMPLE_NAME} ${OpenMP_CXX_LIBRARIES})
		endif()
	endif()

	if(RESOURCE_INSTALL_DIR)
		install(TARGETS ${EXAMPLE_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
	endif()
endfunction(buildExample)

# Build all examples
function(buildExamples)
	foreach(EXAMPLE ${EXAMPLES})
		buildExample(${EXAMPLE})
	endforeach(EXAMPLE)
endfunction(buildExamples)

set(EXAMPLES
	triangle
	texture
	gltfloading
	gltfskinning
	games106_HW1
	pipelines
)

buildExamples()
