cmake_minimum_required(VERSION 3.7)

if(MSVC)
set(HUNTER_ENABLED_DEFAULT OFF)
else()
set(HUNTER_ENABLED_DEFAULT ON)
endif()
option(HUNTER_ENABLED "Enable Hunter package manager" HUNTER_ENABLED_DEFAULT)

include("cmake/HunterGate.cmake")

HunterGate(
	URL "https://github.com/ruslo/hunter/archive/v0.19.57.tar.gz"
	SHA1 "f83ea762f0609c48a583d0e877a6358f6fbfe0d7"
	FILEPATH "${CMAKE_CURRENT_LIST_DIR}/${CMAKE_CONFIG}"
)

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
set_target_properties(argon2 PROPERTIES COMPILE_FLAGS -Ddllexport)
endif()
target_include_directories(argon2 PUBLIC ${ARGON2_INCLUDE_DIR})

hunter_add_package(OpenSSL)
find_package(OpenSSL REQUIRED)

hunter_add_package(SQLite3)
set(SQLite3_DIR "${SQLITE3_VCPKG}/share/sqlite3")
find_package(SQLite3 REQUIRED CONFIG)

if(HUNTER_ENABLED)
	hunter_add_package(odb-compiler)
	hunter_add_package(odb-sqlite)
	set(ODB_COMPONENTS sqlite)
else()
	set(ODB_COMPONENTS libodb sqlite)
endif()
find_package(odb REQUIRED CONFIG COMPONENTS ${ODB_COMPONENTS})
set(${PROJECT_NAME}_ODB_HEADERS "${CMAKE_SOURCE_DIR}/Database/User.h" "${CMAKE_SOURCE_DIR}/Database/VmConfig.h" "${CMAKE_SOURCE_DIR}/Database/ServerConfig.h")

hunter_add_package(CapnProto)
set(BUILD_TESTING OFF)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MTd")
add_subdirectory(submodules/capnproto/c++)
add_definitions("-DCAPNP_VERSION_STR=${CapnProto_VERSION}")

# NOTE: The Cap'n Proto compiler doesn't generate files to the correct directory
#set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS submodules/collab-vm-shared/CollabVm.capnp)

set(BOOST_COMPONENTS system filesystem program_options date_time)
if (HUNTER_ENABLED)
	string(REPLACE ";" ";Boost::" Boost_LIBRARIES ";${BOOST_COMPONENTS}")
	hunter_add_package(Boost COMPONENTS ${BOOST_COMPONENTS})
	find_package(Boost 1.66 REQUIRED CONFIG COMPONENTS ${BOOST_COMPONENTS})
else()
	find_package(Boost 1.66 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
endif()
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
	#set(CMAKE_LINK_FLAGS "${CMAKE_LINK_FLAGS} /MANIFEST:NO")
	target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /bigobj)
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/MANIFEST:NO")
	# Enable Visual Studio's C++ Core Guidelines Checker
	set_target_properties(${PROJECT_NAME} PROPERTIES
		VS_GLOBAL_EnableCppCoreCheck true
		VS_GLOBAL_CodeAnalysisRuleSet CppCoreCheckRules.ruleset
		VS_GLOBAL_RunCodeAnalysis true)
endif()

find_package(PNG REQUIRED)

find_library(CAIRO_LIBRARY cairo REQUIRED)
function(copy_cairo_dlls TARGET)
	if(MSVC AND ${CMAKE_BUILD_TYPE} MATCHES "Debug|RelWithDebInfo")
		get_filename_component(CAIRO_LIBRARY_DIR ${CAIRO_LIBRARY} DIRECTORY)
		set(CAIRO_BINARY_DIR "${CAIRO_LIBRARY_DIR}/../bin")
		foreach(DEPENDENT_CAIRO_DLL IN ITEMS cairo expat libpng16 libbz2 libiconv libcharset freetype pixman-1 fontconfig zlib1)
			add_custom_command(TARGET ${TARGET} PRE_LINK
					COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CAIRO_BINARY_DIR}/${DEPENDENT_CAIRO_DLL}.dll" "${CMAKE_CURRENT_BINARY_DIR}"
				)
		endforeach()
	endif()
endfunction()

if(HUNTER_ENABLED AND MSVC AND ${CMAKE_BUILD_TYPE} MATCHES "Debug|RelWithDebInfo")
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		# TODO: Get lib paths path correctly
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SQLITE3_VCPKG}/debug/bin/sqlite3.dll" "${${PROJECT_NAME}_BINARY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ODB_VCPKG}/debug/bin/libodb.dll" "${${PROJECT_NAME}_BINARY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ODB_VCPKG}/debug/bin/libodb-sqlite.dll" "${${PROJECT_NAME}_BINARY_DIR}"
	)
endif()

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

set(external_cxx_flags -DCMAKE_CXX_FLAGS_DEBUG=/MDd -DCMAKE_C_FLAGS_RELEASE=/MT -DCMAKE_CXX_FLAGS_RELEASE=/MT)

set(LIBVNCSERVER_INSTALL_DIR ${CMAKE_BINARY_DIR}/libvncserver)
include(ExternalProject)
set(LIBVNCSERVER_LIBRARIES ${LIBVNCSERVER_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}vncclient${CMAKE_STATIC_LIBRARY_SUFFIX})
ExternalProject_Add(libvncserver SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/libvncserver"
	CMAKE_ARGS
		"-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
		"-DCMAKE_CONFIGURATION_TYPES=${CMAKE_BUILD_TYPE}"
		-DCMAKE_INSTALL_PREFIX=${LIBVNCSERVER_INSTALL_DIR} ${external_cxx_flags}
	BUILD_BYPRODUCTS ${LIBVNCSERVER_LIBRARIES}
	)
add_dependencies(${PROJECT_NAME} libvncserver)

set(FREERDP_INSTALL_DIR ${CMAKE_BINARY_DIR}/FreeRDP)
set(FREERDP_LIBRARIES
	${FREERDP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}freerdp2${CMAKE_STATIC_LIBRARY_SUFFIX}
	${FREERDP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}freerdp-client2${CMAKE_STATIC_LIBRARY_SUFFIX}
	${FREERDP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}winpr2${CMAKE_STATIC_LIBRARY_SUFFIX}
	ntdsapi ws2_32 rpcrt4 dbghelp)
ExternalProject_Add(FreeRDP SOURCE_DIR "${CMAKE_SOURCE_DIR}/submodules/FreeRDP"
	DEPENDS OpenSSL::SSL
	CMAKE_ARGS
		"-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
		"-DCMAKE_CONFIGURATION_TYPES=${CMAKE_BUILD_TYPE}"
		#"-DCMAKE_C_FLAGS=/D_WIN32_WINNT=NTDDI_VISTA /DNTDDI_VERSION=WIN32_WINNT_VISTA"
		"-DCMAKE_C_FLAGS=/D_WIN32_WINNT=0x0600 /DNTDDI_VERSION=0x06000000"
		-DCMAKE_INSTALL_PREFIX=${FREERDP_INSTALL_DIR} ${external_cxx_flags} -DMSVC_RUNTIME=$<IF:$<CONFIG:RELEASE>,static,dynamic> -DBUILD_SHARED_LIBS=$<IF:$<CONFIG:RELEASE>,OFF,ON>
	# CMake's FindOpenSSL doesn't set OPENSSL_ROOT_DIR but FreeRDP's FindOpenSSL requires it
		-DOPENSSL_ROOT_DIR=${OPENSSL_INCLUDE_DIR}/..
	BUILD_BYPRODUCTS ${FREERDP_LIBRARIES}
	)
function(copy_freerdp_dlls TARGET)
	if(MSVC AND ${CMAKE_BUILD_TYPE} MATCHES "Debug|RelWithDebInfo")
		add_custom_command(TARGET ${TARGET} PRE_LINK
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FREERDP_INSTALL_DIR}/lib/freerdp2.dll" "${CMAKE_CURRENT_BINARY_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FREERDP_INSTALL_DIR}/lib/freerdp-client2.dll" "${CMAKE_CURRENT_BINARY_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FREERDP_INSTALL_DIR}/lib/winpr2.dll" "${CMAKE_CURRENT_BINARY_DIR}"
			)
	endif()
endfunction()
add_dependencies(${PROJECT_NAME} FreeRDP)
copy_freerdp_dlls(${PROJECT_NAME})
copy_cairo_dlls(${PROJECT_NAME})

set(FREERDP_INCLUDE_DIRS
	${FREERDP_INSTALL_DIR}/include/freerdp2
	${FREERDP_INSTALL_DIR}/include/winpr2)

find_package(JPEG REQUIRED)

set(GUACAMOLE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/submodules/guacamole-server)
file(GLOB GUACAMOLE_SOURCES ${GUACAMOLE_ROOT_DIR}/src/common/*.c ${GUACAMOLE_ROOT_DIR}/src/libguac/timestamp.cpp ${GUACAMOLE_ROOT_DIR}/src/libguac/*.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/*.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/_generated_keymaps.c ${GUACAMOLE_ROOT_DIR}/src/libguac/protocol.cpp ${GUACAMOLE_ROOT_DIR}/src/libguac/user-handlers.cpp)
list(REMOVE_ITEM GUACAMOLE_SOURCES ${GUACAMOLE_ROOT_DIR}/src/common/recording.c ${GUACAMOLE_ROOT_DIR}/src/libguac/id.c ${GUACAMOLE_ROOT_DIR}/src/libguac/encode-webp.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/rdp_fs.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/audio_input.c ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp/rdp_print_job.c)
capnp_generate_cpp(GUAC_CAPNP_SRCS GUAC_CAPNP_HDRS submodules/collab-vm-shared/Guacamole.capnp)
add_library(guacamole ${GUACAMOLE_SOURCES} ${GUAC_CAPNP_SRCS})
target_link_libraries(guacamole CapnProto::capnp ${CAIRO_LIBRARY} PNG::PNG ${JPEG_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
add_dependencies(guacamole FreeRDP)
set(GUACAMOLE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/common" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/libguac" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src/libguac/guacamole" "${CMAKE_SOURCE_DIR}/submodules/guacamole-server/src" ${GUACAMOLE_ROOT_DIR}/src/protocols/rdp)
target_compile_definitions(guacamole PRIVATE -DENABLE_WINPR)
target_include_directories(guacamole PUBLIC ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/submodules/collab-vm-shared ${GUACAMOLE_INCLUDE_DIRS} ${FREERDP_INCLUDE_DIRS} ${JPEG_INCLUDE_DIR} ${GUACAMOLE_ROOT_DIR})
if (MSVC)
	find_path(PTHREADS_INCLUDE_DIR pthread)
	if(PTHREADS_INCLUDE_DIR-NOTFOUND)
		message(FATAL_ERROR "Could not find pthreads include directory")
	endif()
	find_library(PTHREADS_LIBRARIES NAMES pthreads pthreadsVC2)
	if(PTHREADS_LIBRARIES-NOTFOUND)
		message(FATAL_ERROR "Could not find pthreads library")
	endif()
	target_link_libraries(guacamole ${PTHREADS_LIBRARIES})
	target_include_directories(guacamole PUBLIC PTHREADS_INCLUDE_DIR ${GUACAMOLE_ROOT_DIR}/src/win-compat)
endif()
add_dependencies(guacamole FreeRDP)
#add_subdirectory(submodules/guacamole-server/src/protocols/rdp/guac_rdpsnd)
#add_channel_client_subsystem(${MODULE_PREFIX} ${CHANNEL_NAME} "oss" "")

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
	${FREERDP_INCLUDE_DIRS}
	${LIBVNCSERVER_INSTALL_DIR}/include
	${CMAKE_CURRENT_BINARY_DIR}/submodules/collab-vm-shared submodules/collab-vm-shared ${OPENSSL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/submodules/GSL/include ${Boost_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIR} ${ARGON2_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}
	argon2 ${Boost_LIBRARIES} ${CAIRO_LIBRARY} CapnProto::capnp 
	${FREERDP_LIBRARIES} guacamole ${LIBVNCSERVER_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL
	odb::libodb odb::libodb-sqlite sqlite3)

add_subdirectory(tests)
