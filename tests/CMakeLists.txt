# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

#set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
#file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)

add_library(Catch2 INTERFACE)
list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/Catch2)
target_include_directories(Catch2 INTERFACE ${CMAKE_CURRENT_LIST_DIR})
add_library(Catch2::Catch2 ALIAS Catch2)
include(Catch)

set(CATCH_EXTRA_ARGS "" CACHE STRING "Extra arguments for catch2 test suites.")


add_library(test_common INTERFACE)
target_compile_definitions(test_common INTERFACE CATCH_CONFIG_FAST_COMPILE)
target_link_libraries(test_common INTERFACE Catch2::Catch2)

# add_library(test_common_data ${COMMON_DATA_UTILS})
# target_include_directories(test_common_data PUBLIC ${TEST_DATA_DIR}/include)
# target_link_libraries(test_common_data PRIVATE test_common libslic3r)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(network)
add_subdirectory(others)
