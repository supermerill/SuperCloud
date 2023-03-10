get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

configure_file(../../src/network/IdentityManagerCrypto.cpp IdentityManagerCrypto.cpp COPYONLY)

include_directories(".")

add_executable(${_TEST_NAME}_tests
    test_cryptopp.cpp
    IdentityManager.hpp
    IdentityManagerCrypto.cpp
    ../../src/utils/ByteBuff.hpp
    ../../src/utils/ByteBuff.cpp
#    ../../src/utils/Utils.hpp
#    ../../src/utils/Utils.cpp
    )

target_link_libraries(${_TEST_NAME}_tests 
    test_common
    cryptopp::cryptopp
)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
