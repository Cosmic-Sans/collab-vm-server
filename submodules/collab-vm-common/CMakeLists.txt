cmake_minimum_required(VERSION 3.8)

set(CAPNP_ROOT "" CACHE PATH "Path to the pre-existing Cap'n Proto install dir")

project(collab-vm-common VERSION 2.0.0 LANGUAGES CXX)

macro(generate_capnp_schemas)
	capnp_generate_cpp(GUAC_CAPNP_SRCS GUAC_CAPNP_HDRS CollabVm.capnp Guacamole.capnp)
endmacro()

if (NOT ${CAPNP_ROOT} STREQUAL "")
	set(CAPNP_EXECUTABLE ${CAPNP_ROOT}/bin/capnp)
	set(CAPNPC_CXX_EXECUTABLE ${CAPNP_ROOT}/bin/capnpc-c++)
	set(CAPNP_INCLUDE_DIRECTORY ${CAPNP_ROOT}/include)
	include(${CMAKE_CURRENT_SOURCE_DIR}/submodules/capnproto/c++/cmake/CapnProtoMacros.cmake)
	generate_capnp_schemas()
endif()
# Override Cap'n Proto's default test option
option(BUILD_TESTING "Build Cap'n Proto unit tests and enable CTest 'check' target." OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/capnproto/c++ EXCLUDE_FROM_ALL)
if (NOT ${CAPNP_ROOT} STREQUAL "")
	# Don't generate the compiler tool
  #set_target_properties(capnp_tool PROPERTIES EXCLUDE_FROM_ALL TRUE)
	#set_target_properties(capnpc_cpp PROPERTIES EXCLUDE_FROM_ALL TRUE)
else()
	generate_capnp_schemas()
endif()

file(GLOB ${PROJECT_NAME}_SRCS "*.hpp")
add_library(${PROJECT_NAME} ${${PROJECT_NAME}_SRCS} ${GUAC_CAPNP_HDRS} ${GUAC_CAPNP_SRCS})
set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED YES
	CXX_EXTENSIONS NO
)
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} CapnProto::capnp)

