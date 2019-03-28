cmake_minimum_required(VERSION 3.7)

# Create a list containing all variables passed as command line arguments
# to this script so they can be forwarded to ExternalProject_Add()
# This must be at the beginning of this script.
# Source: https://cmake.org/pipermail/cmake/2018-January/067002.html
get_cmake_property(vars CACHE_VARIABLES)
foreach(var ${vars})
  get_property(currentHelpString CACHE "${var}" PROPERTY HELPSTRING)
    if("${currentHelpString}" MATCHES "No help, variable specified on the command line." OR "${currentHelpString}" STREQUAL "")
        if(NOT "${var}" MATCHES "CMAKE_MAKE_PROGRAM")
          list(APPEND CL_ARGS "-D${var}:STRING=${${var}}")
        endif()
    endif()
endforeach()

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

add_library(argon2 STATIC ${ARGON2_SOURCES})
if(MSVC)
	# TODO: Find a better way to prevent the argon2 functions from being exported with MVSC
	#set_target_properties(argon2 PROPERTIES COMPILE_FLAGS -Ddllexport)
	# The MSVC equivalient of -isystem is /external:I
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
	# Suppress all warnings for external headers
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:external /external:W0")
	#target_compile_options(${PROJECT_NAME} PRIVATE /experimental:external /external:W0)
endif()
target_include_directories(argon2 PUBLIC ${ARGON2_INCLUDE_DIR})

find_package(OpenSSL REQUIRED)

find_package(SQLite3 REQUIRED)

set(ODB_COMPONENTS libodb sqlite)
find_package(odb REQUIRED COMPONENTS ${ODB_COMPONENTS})
set(${PROJECT_NAME}_ODB_HEADERS "${CMAKE_SOURCE_DIR}/Database/User.h" "${CMAKE_SOURCE_DIR}/Database/VmConfig.h" "${CMAKE_SOURCE_DIR}/Database/ServerConfig.h")

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

add_compile_definitions(BOOST_ALL_NO_LIB)
find_package(Boost 1.69 REQUIRED COMPONENTS system filesystem program_options date_time thread)
add_definitions("-DBOOST_VERSION_STR=${Boost_VERSION}")
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

link_directories(${Boost_LIBRARY_DIR})

add_definitions("-DPROJECT_VERSION=${PROJECT_VERSION}")
add_definitions(-DHAS_STD_BYTE -DBOOST_ASIO_NO_DEPRECATED -DBOOST_ASIO_HAS_MOVE -DBOOST_ASIO_HAS_STD_ATOMIC -DBOOST_ASIO_HAS_VARIADIC_TEMPLATES -DBOOST_ASIO_HAS_STD_CHRONO)
if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0501 -D_HAS_AUTO_PTR_ETC)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DLIBODB_STATIC_LIB /DLIBODB_SQLITE_STATIC_LIB")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /DLIBODB_STATIC_LIB /DLIBODB_SQLITE_STATIC_LIB")
endif()

set(CMAKE_CXX_STANDARD 17)
add_executable(${PROJECT_NAME} Main.cpp Database/Database.cpp ${CAPNP_SRCS} ${CAPNP_HDRS} ${ODB_HEADERS})

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /bigobj)
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/MANIFEST:NO")
	# Enable Visual Studio's C++ Core Guidelines Checker
	set_target_properties(${PROJECT_NAME} PROPERTIES
		VS_GLOBAL_EnableCppCoreCheck true
		VS_GLOBAL_CodeAnalysisRuleSet CppCoreCheckRules.ruleset
		VS_GLOBAL_RunCodeAnalysis true)
endif()

find_package(PNG REQUIRED)

find_package(unofficial-cairo REQUIRED)

include(cmake/Findodb-compiler.cmake)
set(ODB_COMPILE_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
odb_compile(
    ${PROJECT_NAME}
    FILES
      ${${PROJECT_NAME}_ODB_HEADERS}
    DB
      sqlite
	INCLUDE
	  ${Boost_INCLUDE_DIRS}
    GENERATE_QUERY
    GENERATE_SESSION
    GENERATE_SCHEMA
	STANDARD c++11
)
# The odb_compile command does not support the --include-prefix argument
# so this is used to allow the generated ODB files to find the schema
# headers inside of the Database subdirectory
target_include_directories(${PROJECT_NAME} PUBLIC Database)

set(LIBVNCSERVER_CMAKE_CACHE_ARGS ${CL_ARGS})
list(FILTER LIBVNCSERVER_CMAKE_CACHE_ARGS EXCLUDE REGEX "CMAKE_INSTALL_PREFIX")
set(LIBVNCSERVER_INSTALL_DIR ${CMAKE_BINARY_DIR}/libvncserver)
include(ExternalProject)
set(LIBVNCSERVER_LIBRARIES ${LIBVNCSERVER_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}vncclient${CMAKE_STATIC_LIBRARY_SUFFIX})
set(LIBVNCSERVER_INCLUDE_DIRS ${LIBVNCSERVER_INSTALL_DIR}/include)
if (WIN32)
  set(LIBVNCSERVER_CMAKE_EXE_LINKER_FLAGS -DCMAKE_EXE_LINKER_FLAGS:STRING=crypt32.lib)
endif()
ExternalProject_Add(libvncserver SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/libvncserver"
  CMAKE_CACHE_ARGS ${LIBVNCSERVER_CMAKE_CACHE_ARGS}
		-DCMAKE_INSTALL_PREFIX:STRING=${LIBVNCSERVER_INSTALL_DIR}
		${LIBVNCSERVER_CMAKE_EXE_LINKER_FLAGS}
	BUILD_BYPRODUCTS ${LIBVNCSERVER_LIBRARIES}
	)
add_dependencies(${PROJECT_NAME} libvncserver)

set(FREERDP_CMAKE_CACHE_ARGS ${CL_ARGS})
list(FILTER FREERDP_CMAKE_CACHE_ARGS EXCLUDE REGEX "CMAKE_INSTALL_PREFIX")
set(FREERDP_INSTALL_DIR ${CMAKE_BINARY_DIR}/FreeRDP)
set(FREERDP_LIBRARIES
	${FREERDP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}freerdp2${CMAKE_STATIC_LIBRARY_SUFFIX}
	${FREERDP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}freerdp-client2${CMAKE_STATIC_LIBRARY_SUFFIX}
	${FREERDP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}winpr2${CMAKE_STATIC_LIBRARY_SUFFIX}
	ntdsapi ws2_32 rpcrt4 dbghelp)
ExternalProject_Add(FreeRDP SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/FreeRDP"
	DEPENDS OpenSSL::SSL
	CMAKE_CACHE_ARGS ${FREERDP_CMAKE_CACHE_ARGS}
		-DCMAKE_INSTALL_PREFIX:STRING=${FREERDP_INSTALL_DIR}
    -DMSVC_RUNTIME:STRING=$<IF:$<CONFIG:RELEASE>,static,dynamic>
    -DBUILD_SHARED_LIBS:STRING=$<IF:$<CONFIG:RELEASE>,OFF,ON>
	BUILD_BYPRODUCTS ${FREERDP_LIBRARIES}
	)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FREERDP_INSTALL_DIR}/lib/" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
  )
add_dependencies(${PROJECT_NAME} FreeRDP)

set(FREERDP_INCLUDE_DIRS
	${FREERDP_INSTALL_DIR}/include/freerdp2
	${FREERDP_INSTALL_DIR}/include/winpr2)

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

list(REMOVE_ITEM GUACAMOLE_SOURCES ${GUACAMOLE_ROOT_DIR}/src/libguac/id.c ${GUACAMOLE_ROOT_DIR}/src/libguac/encode-webp.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/rdp_fs.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/audio_input.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/rdp_print_job.c ${GUACAMOLE_ROOT_DIR}/src/protocols/vnc/sftp.c)
add_library(guacamole ${GUACAMOLE_SOURCES})
target_link_libraries(guacamole PRIVATE CapnProto::capnp unofficial::cairo::cairo unofficial::cairo::cairo-gobject collab-vm-common PNG::PNG ${JPEG_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto ${FREERDP_LIBRARIES} ${LIBVNCSERVER_LIBRARIES})
add_dependencies(guacamole FreeRDP)
add_dependencies(guacamole libvncserver)
set(GUACAMOLE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/common" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/libguac" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/libguac/guacamole" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src" ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp ${COLLAB_VM_COMMON_BINARY_DIR})
target_compile_definitions(guacamole PRIVATE -DENABLE_WINPR)
target_include_directories(guacamole PUBLIC ${GUACAMOLE_INCLUDE_DIRS} ${FREERDP_INCLUDE_DIRS} ${LIBVNCSERVER_INCLUDE_DIRS} ${JPEG_INCLUDE_DIR} ${GUACAMOLE_ROOT_DIR})
if (MSVC)
	find_path(PTHREADS_INCLUDE_DIR pthread)
	if(PTHREADS_INCLUDE_DIR-NOTFOUND)
		message(FATAL_ERROR "Could not find pthreads include directory")
	endif()
	find_library(PTHREADS_LIBRARIES NAMES pthreads pthreadsVC2)
	if(PTHREADS_LIBRARIES-NOTFOUND)
		message(FATAL_ERROR "Could not find pthreads library")
	endif()
	target_link_libraries(guacamole PRIVATE ${PTHREADS_LIBRARIES})
	target_include_directories(guacamole PUBLIC PTHREADS_INCLUDE_DIR ${GUACAMOLE_ROOT_DIR}/src/win-compat)
endif()
add_dependencies(guacamole FreeRDP)

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
	${FREERDP_INCLUDE_DIRS}
	${LIBVNCSERVER_INCLUDE_DIRS}
	${COLLAB_VM_COMMON} ${OPENSSL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/submodules/GSL/include ${Boost_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIR} ${ARGON2_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE
	argon2 ${Boost_LIBRARIES} unofficial::cairo::cairo unofficial::cairo::cairo-gobject CapnProto::capnp collab-vm-common
	guacamole ${LIBVNCSERVER_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL
	odb::libodb odb::libodb-sqlite sqlite3)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION . OPTIONAL)
install(DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>/
        DESTINATION . FILES_MATCHING PATTERN *.dll)

add_subdirectory(tests)
