enable_testing()

add_executable(recaptcha Recaptcha.cpp)
target_include_directories(recaptcha PUBLIC ${PROJECT_SOURCE_DIR} ${OPENSSL_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/submodules/beast/include ${Boost_INCLUDE_DIRS})
target_link_libraries(recaptcha ${Boost_LIBRARIES} OpenSSL::SSL)
add_test(recaptcha recaptcha)

add_executable(totp Totp.cpp)
target_include_directories(totp PUBLIC ${PROJECT_SOURCE_DIR} ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/submodules/GSL/include)
target_link_libraries(totp OpenSSL::Crypto)
add_test(totp totp)

add_executable(guac-test Guacamole.cpp)
target_include_directories(guac-test PUBLIC ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR} ${Cairo_INCLUDE_DIR} ${GUACAMOLE_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/submodules/GSL/include)
target_link_libraries(guac-test ${Boost_LIBRARIES} CapnProto::capnp ${Cairo_LIBRARY} guacamole)
add_test(guac-test guac-test)
add_dependencies(guac-test guacamole)

add_executable(turn-test TurnTest.cpp)
target_include_directories(turn-test PUBLIC ${PROJECT_SOURCE_DIR} ${Boost_INCLUDE_DIRS})
target_link_libraries(turn-test ${Boost_LIBRARIES})
add_test(turn-test turn-test)
