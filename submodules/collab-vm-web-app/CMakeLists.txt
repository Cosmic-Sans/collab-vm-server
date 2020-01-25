cmake_minimum_required(VERSION 3.8)

option(BUILD_WASM "Build WebAssembly" ON)
set(COLLAB_VM_COMMON "submodules/collab-vm-common" CACHE PATH "Path to collab-vm-common directory")
set(COLLAB_VM_COMMON_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/submodules/collab-vm-common" CACHE PATH "Path to output collab-vm-common binaries")

project(collab-vm-web-app VERSION 2.0.0 LANGUAGES CXX)

add_executable(${PROJECT_NAME} protocol.cpp)

add_subdirectory(${COLLAB_VM_COMMON} ${COLLAB_VM_COMMON_BINARY_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${COLLAB_VM_COMMON})
target_link_libraries(${PROJECT_NAME} collab-vm-common)
set_target_properties(capnp_tool PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(capnpc_cpp PROPERTIES EXCLUDE_FROM_ALL TRUE)
set(LINK_FLAGS "--bind -s ENVIRONMENT=web")
if(${BUILD_WASM})
  set(LINK_FLAGS "${LINK_FLAGS} -s WASM=1")
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED YES
	LINK_FLAGS ${LINK_FLAGS}
)

