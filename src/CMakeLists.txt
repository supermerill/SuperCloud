cmake_minimum_required(VERSION 3.13)
project(libsupercloud)

add_subdirectory(boost)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()


add_library(libsupercloud STATIC
    network/ClusterManager.hpp
    network/ConnectionMessageManagerInterface.hpp
	network/ConnectionMessageManager.hpp
	network/ConnectionMessageManager.cpp
	network/ClusterAdminMessageManager.hpp
	network/ClusterAdminMessageManager.cpp
    network/Peer.cpp
    network/Peer.hpp
	network/PhysicalServer.cpp
	network/PhysicalServer.hpp
	network/IdentityManager.cpp
	network/IdentityManager.hpp
    utils/ByteBuff.cpp
    utils/ByteBuff.hpp
    utils/Utils.cpp
    utils/Utils.hpp
    utils/Parameters.cpp
    utils/Parameters.hpp
#    fs/FileSystemManager.hpp
#    fs/StandardManager.hpp
#    fs/StandardManager.cpp
    fs/stubFileSystemManager.hpp
)

#encoding_check(libsupercloud)

# target_compile_definitions(libsupercloud PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_include_directories(libsupercloud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libsupercloud PUBLIC ${EXPAT_INCLUDE_DIRS})
target_link_libraries(libsupercloud
    # miniz
    boost_libs
    nowide
    )

if (APPLE)
    target_compile_options(libsupercloud PUBLIC "-fno-aligned-allocation")
endif ()
