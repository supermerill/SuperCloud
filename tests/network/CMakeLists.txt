get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

configure_file(../serv1_keys.ini serv1_keys.ini COPYONLY)
configure_file(../serv2_keys.ini serv2_keys.ini COPYONLY)
configure_file(../serv3_keys.ini serv3_keys.ini COPYONLY)
configure_file(../serv4_keys.ini serv4_keys.ini COPYONLY)

add_executable(${_TEST_NAME}_tests
    test_connection.cpp
    test_connection_fake_network.cpp
    test_connection_messages.cpp
    ../WaitConnection.hpp
    ../FakeNetwork.hpp
    ../FakeNetwork.cpp
    )

    
target_link_libraries(${_TEST_NAME}_tests 
    test_common
    libsupercloud 
)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
