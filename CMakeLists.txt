cmake_minimum_required(VERSION 3.7)

enable_language(C CXX)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

if(NOT MSVC)
  find_package(Filesystem REQUIRED)
  set(FILESYSTEM_LIBRARY std::filesystem)
else()
  # MSVC doesn't require linking to a filesystem library
  set(FILESYSTEM_LIBRARY "")
endif()

set(Boost_INCLUDE_DIRS "")
foreach(boost_library IN ITEMS
  algorithm
  align
  any
  array
  asio
  assert
  beast
  bind
  concept_check
  config
  container
  container_hash
  core
  detail
  endian
  foreach
  format
  function
  functional
  integer
  intrusive
  io
  iterator
  logic
  move
  mp11
  mpl
  multi_index
  numeric_conversion
  optional
  predef
  preprocessor
  property_tree
  range
  serialization
  smart_ptr
  static_assert
  system
  test
  throw_exception
  tuple
  type_index
  type_traits
  utility
  winapi
  )
    list(APPEND Boost_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/submodules/boost-${boost_library}/include")
  endforeach()

find_path(sqlite-modern-cpp_INCLUDE_DIR
          sqlite_modern_cpp.h
          PATHS "${CMAKE_SOURCE_DIR}/submodules/sqlite_modern_cpp/hdr/")

find_path(clipp_INCLUDE_DIR
          clipp.h
          PATHS "${CMAKE_SOURCE_DIR}/submodules/clipp/include/")

if (BUILD_SHARED_LIBS)
  message(FATAL_ERROR "Building shared versions"
          " of FreeRDP, libvncserver, or Cap'N Proto is not supported")
endif()
# Create a list containing all variables passed as command line arguments
# to this script so they can be forwarded to ExternalProject_Add()
# This must be at the beginning of this script.
# Source: https://cmake.org/pipermail/cmake/2018-January/067002.html
get_cmake_property(CACHE_VARS CACHE_VARIABLES)
foreach(CACHE_VAR ${CACHE_VARS})
  if("${${CACHE_VAR}}" STREQUAL ""
     OR CACHE_VAR STREQUAL "CMAKE_INSTALL_PREFIX")
    continue()
  endif()
  get_property(CACHE_VAR_TYPE CACHE ${CACHE_VAR} PROPERTY TYPE)
  if(CACHE_VAR_TYPE STREQUAL "INTERNAL")
    continue()
  endif()
  if(CACHE_VAR_TYPE STREQUAL "UNINITIALIZED")
    set(CACHE_VAR_TYPE ":STRING")
  else()
    set(CACHE_VAR_TYPE ":${CACHE_VAR_TYPE}")
  endif()
  string(REPLACE ";" "\;" CL_ARG "${${CACHE_VAR}}")
  list(APPEND CL_ARGS "-D${CACHE_VAR}${CACHE_VAR_TYPE}=${CL_ARG}")
endforeach()

if (NOT DEFINED DEPENDENCIES_BINARY_DIR)
  set(DEPENDENCIES_BINARY_DIR ${CMAKE_BINARY_DIR}/dependencies)
endif()

set(FREERDP_BINARY_DIR ${DEPENDENCIES_BINARY_DIR}/FreeRDP)
set(FREERDP_INSTALL_DIR ${DEPENDENCIES_BINARY_DIR}/FreeRDP-install)
file(MAKE_DIRECTORY ${FREERDP_BINARY_DIR} ${FREERDP_INSTALL_DIR})
if (NOT WIN32)
  # Override FreeRDP's FindOpenSSL.cmake to use vcpkg's port
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/cmake/FindOpenSSL.cmake
        "${CMAKE_SOURCE_DIR}/submodules/FreeRDP/cmake/FindOpenSSL.cmake"
    WORKING_DIRECTORY ${FREERDP_BINARY_DIR})
endif()
# Configure FreeRDP
execute_process(
  COMMAND ${CMAKE_COMMAND}
    -G ${CMAKE_GENERATOR} ${CL_ARGS}
    -DCMAKE_INSTALL_PREFIX:PATH=${FREERDP_INSTALL_DIR}
    -DCMAKE_INSTALL_LIBDIR:STRING=lib
    -DBUILD_SHARED_LIBS:BOOL=OFF
    ${CMAKE_SOURCE_DIR}/submodules/FreeRDP
  WORKING_DIRECTORY ${FREERDP_BINARY_DIR})
# Build FreeRDP
execute_process(
  COMMAND ${CMAKE_COMMAND}
    --build ${FREERDP_BINARY_DIR}
    --config ${CMAKE_BUILD_TYPE}
    --target install
  WORKING_DIRECTORY ${FREERDP_BINARY_DIR})
# Patch header for static linking
file(READ "${FREERDP_INSTALL_DIR}/include/freerdp2/freerdp/api.h" FREERDP_API_HEADER)
string(REPLACE
  "#define FREERDP_API __declspec(dllimport)"
  "#define FREERDP_API"
  FREERDP_API_HEADER "${FREERDP_API_HEADER}")
file(WRITE "${FREERDP_INSTALL_DIR}/include/freerdp2/freerdp/api.h" "${FREERDP_API_HEADER}")

set(WinPR_DIR "${FREERDP_INSTALL_DIR}/lib/cmake/WinPR2/")
find_package(WinPR REQUIRED CONFIG)
set(FreeRDP_DIR "${FREERDP_INSTALL_DIR}/lib/cmake/FreeRDP2/")
find_package(FreeRDP REQUIRED CONFIG)
set(FreeRDP-Client_DIR "${FREERDP_INSTALL_DIR}/lib/cmake/FreeRDP-Client2/")
find_package(FreeRDP-Client REQUIRED CONFIG)

set(COLLAB_VM_COMMON "submodules/collab-vm-common" CACHE PATH "Path to collab-vm-common directory")
set(COLLAB_VM_COMMON_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/submodules/collab-vm-common" CACHE PATH "Path to output collab-vm-common binaries")

project(collab-vm-server VERSION 2.0.0)

set(ARGON2_ROOT "${CMAKE_SOURCE_DIR}/submodules/argon2")
set(ARGON2_INCLUDE_DIR "${ARGON2_ROOT}/include")
set(ARGON2_SOURCES src/argon2.c src/core.c src/blake2/blake2b.c src/thread.c src/encoding.c src/opt.c)
string(REPLACE ";" ";${ARGON2_ROOT}/" ARGON2_SOURCES ";${ARGON2_SOURCES}")
if(NOT EXISTS "${ARGON2_INCLUDE_DIR}/argon2.h")
  message(SEND_ERROR "Can't find argon2.h in ${ARGON2_INCLUDE_DIR}")
endif()
foreach(ARGON2_FILE ${ARGON2_SOURCES})
	if(NOT EXISTS "${ARGON2_FILE}")
		message(SEND_ERROR "Can't find ${ARGON2_FILE}")
	endif()
endforeach()

add_library(argon2 ${ARGON2_SOURCES})
if(MSVC)
	# TODO: Find a better way to prevent the argon2 functions from being exported with MVSC
	#set_target_properties(argon2 PROPERTIES COMPILE_FLAGS -Ddllexport)
	# The MSVC equivalient of -isystem is /external:I
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
	# Suppress all warnings for external headers
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:external /external:W0")
endif()
target_include_directories(argon2 PUBLIC ${ARGON2_INCLUDE_DIR})

find_package(OpenSSL REQUIRED)

find_package(SQLite3 CONFIG REQUIRED)

#set(BUILD_TESTING OFF)
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
#set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MTd")
#add_subdirectory(submodules/capnproto/c++)

# NOTE: The Cap'n Proto compiler doesn't generate files to the correct directory
#set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
#capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS submodules/collab-vm-common/CollabVm.capnp)
add_subdirectory(${COLLAB_VM_COMMON} ${COLLAB_VM_COMMON_BINARY_DIR})
add_definitions("-DCAPNP_VERSION_STR=${CapnProto_VERSION}")

add_definitions("-DPROJECT_VERSION=${PROJECT_VERSION}")
add_definitions(
  # Prevent Boost from trying to link
  -DBOOST_ALL_NO_LIB
  # Boost Asio options
  -DBOOST_ASIO_HAS_MOVE
  -DBOOST_ASIO_HAS_STD_ATOMIC
  -DBOOST_ASIO_HAS_VARIADIC_TEMPLATES
  -DBOOST_ASIO_HAS_STD_CHRONO
  -DBOOST_ASIO_DISABLE_BOOST_ARRAY
  -DBOOST_ASIO_DISABLE_BOOST_ASSERT
  -DBOOST_ASIO_DISABLE_BOOST_BIND
  -DBOOST_ASIO_DISABLE_BOOST_CHRONO
  -DBOOST_ASIO_DISABLE_BOOST_DATE_TIME
  -DBOOST_ASIO_DISABLE_BOOST_LIMITS
  -DBOOST_ASIO_DISABLE_BOOST_REGEX
  -DBOOST_ASIO_DISABLE_BOOST_STATIC_CONSTANT
  -DBOOST_ASIO_DISABLE_BOOST_THROW_EXCEPTION
  -DBOOST_ASIO_DISABLE_BOOST_WORKAROUND
  -DBOOST_ASIO_NO_DEPRECATED)

# Why is this necessary?
if(MSVC)
  add_definitions(-D_WIN32_WINNT=0x0501)
endif()

set(CMAKE_CXX_STANDARD 17)
add_executable(${PROJECT_NAME} Main.cpp Database/Database.cpp ${CAPNP_SRCS} ${CAPNP_HDRS})

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /bigobj)
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/MANIFEST:NO")
endif()

find_package(PNG REQUIRED)

find_package(unofficial-cairo CONFIG)
set(Cairo_LIBRARY unofficial::cairo::cairo)

include(ExternalProject)
set(LIBVNCSERVER_CMAKE_CACHE_ARGS ${CL_ARGS})
list(FILTER LIBVNCSERVER_CMAKE_CACHE_ARGS EXCLUDE REGEX "CMAKE_INSTALL_PREFIX")
set(LIBVNCSERVER_INSTALL_DIR ${CMAKE_BINARY_DIR}/libvncserver)
set(LIBVNCSERVER_LIBRARIES ${LIBVNCSERVER_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}vncclient${CMAKE_STATIC_LIBRARY_SUFFIX})
set(LIBVNCSERVER_INCLUDE_DIRS ${LIBVNCSERVER_INSTALL_DIR}/include)
if (WIN32)
  set(LIBVNCSERVER_CMAKE_EXE_LINKER_FLAGS -DCMAKE_EXE_LINKER_FLAGS:STRING=crypt32.lib)
else()
  # Workaround for vcpkg's libpng port
  set(LIBVNCSERVER_CMAKE_MODULE_PATH 
    "-DCMAKE_MODULE_PATH:PATH=${CMAKE_SOURCE_DIR}/cmake/")
endif()
# Replace backslashes with forward slashes to avoid errors
# when using ExternalProject_Add(CMAKE_CACHE_ARGS ...)
string(REPLACE "\\" "\\\\" LIBVNCSERVER_CMAKE_CACHE_ARGS "${LIBVNCSERVER_CMAKE_CACHE_ARGS}")
ExternalProject_Add(libvncserver SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/libvncserver"
  DEPENDS OpenSSL::SSL OpenSSL::Crypto PNG::PNG
  CMAKE_CACHE_ARGS ${LIBVNCSERVER_CMAKE_CACHE_ARGS}
    -DCMAKE_INSTALL_PREFIX:PATH=${LIBVNCSERVER_INSTALL_DIR}
    -DCMAKE_INSTALL_LIBDIR:STRING=lib
    "${LIBVNCSERVER_CMAKE_MODULE_PATH}"
    -DWITH_LZO:BOOL=OFF
    -DWITH_SASL:BOOL=OFF
    -DWITH_SDL:BOOL=OFF
    -DWITH_GNUTLS:BOOL=OFF
    -DWITH_GCRYPT:BOOL=OFF
    -DWITH_FFMPEG:BOOL=OFF
    -DWITH_WEBSOCKETS:BOOL=OFF
    # libnvcserver doesn't support shared libraries on Windows
    -DBUILD_SHARED_LIBS:BOOL=OFF
		${LIBVNCSERVER_CMAKE_EXE_LINKER_FLAGS}
	BUILD_BYPRODUCTS ${LIBVNCSERVER_LIBRARIES}
	)
add_dependencies(${PROJECT_NAME} libvncserver)

add_library(libvncclient STATIC IMPORTED)
set_target_properties(libvncclient PROPERTIES
  IMPORTED_LOCATION "${LIBVNCSERVER_LIBRARIES}"
  INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto")

find_package(JPEG REQUIRED)

set(GUACAMOLE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/submodules/guacamole-server)
file(GLOB GUACAMOLE_SOURCES
      ${GUACAMOLE_ROOT_DIR}/src/common/*.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/instruction*.cpp
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/log.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/image-stream.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/jpeg.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/png.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/layer.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/buffer.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/cursor.c
      ${GUACAMOLE_ROOT_DIR}/src/guacenc/display*.c
      ${GUACAMOLE_ROOT_DIR}/src/libguac/protocol.cpp
      ${GUACAMOLE_ROOT_DIR}/src/libguac/timestamp.cpp
      ${GUACAMOLE_ROOT_DIR}/src/libguac/user-handlers.cpp
      ${GUACAMOLE_ROOT_DIR}/src/libguac/*.c
      ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/*.c
      ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/guac_rdpsnd/*.c
      ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/_generated_keymaps.c
      ${GUACAMOLE_ROOT_DIR}/src/protocols/vnc/*.c)

list(REMOVE_ITEM GUACAMOLE_SOURCES
  ${GUACAMOLE_ROOT_DIR}/src/libguac/wait-fd.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/socket-broadcast.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/socket-fd.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/socket-nest.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/socket-ssl.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/socket-tee.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/socket-wsa.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/id.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/user-handshake.c
  ${GUACAMOLE_ROOT_DIR}/src/libguac/encode-webp.c
  ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/rdp_fs.c
  ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/audio_input.c
  ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/rdp_print_job.c
  ${GUACAMOLE_ROOT_DIR}/src/protocols/vnc/sftp.c)
add_library(guacamole ${GUACAMOLE_SOURCES})
target_link_libraries(guacamole PRIVATE ${Cairo_LIBRARY} CapnProto::capnp collab-vm-common
  PNG::PNG ${JPEG_LIBRARIES} freerdp freerdp-client libvncclient OpenSSL::SSL OpenSSL::Crypto)
add_dependencies(guacamole freerdp libvncserver)
set(GUACAMOLE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/common" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/libguac" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/libguac/guacamole" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src" ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp ${COLLAB_VM_COMMON_BINARY_DIR})
target_compile_definitions(guacamole PRIVATE -DENABLE_WINPR)
target_include_directories(guacamole PUBLIC ${GUACAMOLE_INCLUDE_DIRS} ${FreeRDP_INCLUDE_DIR} ${FreeRDP-Client_INCLUDE_DIR} ${WinPR_INCLUDE_DIR} ${LIBVNCSERVER_INCLUDE_DIRS} ${JPEG_INCLUDE_DIR} ${GUACAMOLE_ROOT_DIR})
if (MSVC)
  find_package(pthreads REQUIRED)
  if (TARGET PThreads4W::PThreads4W)
    target_link_libraries(guacamole PRIVATE PThreads4W::PThreads4W)
  else()
    target_link_libraries(guacamole PRIVATE ${PTHREADS_LIBRARIES})
  endif()
	target_include_directories(guacamole PUBLIC ${GUACAMOLE_ROOT_DIR}/src/win-compat)
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
	${FreeRDP_INCLUDE_DIR}
	${LIBVNCSERVER_INCLUDE_DIRS}
	${COLLAB_VM_COMMON}
  ${OPENSSL_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/submodules/GSL/include
  ${Boost_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIR}
  ${sqlite-modern-cpp_INCLUDE_DIR}
  ${clipp_INCLUDE_DIR}
  ${ARGON2_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE
  argon2 CapnProto::capnp ${Cairo_LIBRARY} collab-vm-common
  guacamole OpenSSL::Crypto OpenSSL::SSL sqlite3 ${FILESYSTEM_LIBRARY})

install(TARGETS ${PROJECT_NAME} DESTINATION .)
if(MSVC)
  install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION . OPTIONAL)
endif()
install(DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>/
        DESTINATION . FILES_MATCHING PATTERN *.dll)

add_subdirectory(tests)
