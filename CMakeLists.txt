cmake_minimum_required(VERSION 3.13)
project(supercloud)

# Enable C++17 language standard.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- boost ---
set(Boost_USE_STATIC_LIBS ON)
set(MINIMUM_BOOST_VERSION "1.66.0")
set(_boost_components "system;filesystem;thread;log;locale;regex;chrono;atomic;date_time;iostreams")
find_package(Boost ${MINIMUM_BOOST_VERSION} REQUIRED COMPONENTS ${_boost_components})
# boost compile only in release & debug. We have to force the release version for RELWITHDEBINFO compilation
if (MSVC)
  set_target_properties(${Boost_LIBRARIES} PROPERTIES MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE)
endif()
add_library(boost_libs INTERFACE)
add_library(boost_headeronly INTERFACE)
if (APPLE)
    # BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X: https://svn.boost.org/trac/boost/ticket/5339
    target_compile_definitions(boost_headeronly INTERFACE BOOST_ASIO_DISABLE_KQUEUE)
endif()

if(TARGET Boost::system)
    message(STATUS "Boost::boost exists")
    target_link_libraries(boost_headeronly INTERFACE Boost::boost)

    # Only from cmake 3.12
    # list(TRANSFORM _boost_components PREPEND Boost:: OUTPUT_VARIABLE _boost_targets)
    set(_boost_targets "")
    foreach(comp ${_boost_components})
        list(APPEND _boost_targets "Boost::${comp}")
    endforeach()

    target_link_libraries(boost_libs INTERFACE
        boost_headeronly # includes the custom compile definitions as well
        ${_boost_targets}
        )
    # slic3r_remap_configs("${_boost_targets}" RelWithDebInfo Release)
else()
    target_include_directories(boost_headeronly INTERFACE ${Boost_INCLUDE_DIRS})
    target_link_libraries(boost_libs INTERFACE boost_headeronly ${Boost_LIBRARIES})
endif()
# --- end boost ---

# Where all the bundled libraries reside?
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LIBDIR_BIN ${CMAKE_CURRENT_BINARY_DIR}/src)
# For the bundled boost libraries (boost::nowide)
include_directories(${LIBDIR})

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(exe)
	
