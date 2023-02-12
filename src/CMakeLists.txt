cmake_minimum_required(VERSION 3.13)
project(libsupercloud)

add_subdirectory(boost)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()


add_library(libsupercloud STATIC
    network/BoostAsioNetwork.cpp
    network/BoostAsioNetwork.hpp
    network/ClusterManager.hpp
    network/ConnectionMessageManagerInterface.hpp
    network/ConnectionMessageManager.cpp
    network/ConnectionMessageManager.hpp
    network/ClusterAdminMessageManager.cpp
    network/ClusterAdminMessageManager.hpp
    network/IdentityManager.cpp
    network/IdentityManager.hpp
    network/NetworkAdapter.hpp
    network/Peer.cpp
    network/Peer.hpp
    network/PhysicalServer.cpp
    network/PhysicalServer.hpp
    utils/ByteBuff.cpp
    utils/ByteBuff.hpp
    utils/Utils.cpp
    utils/Utils.hpp
    utils/Parameters.cpp
    utils/Parameters.hpp
	fs/base/FsElt.hpp
	fs/base/FsObject.hpp
	fs/base/FsChunk.hpp
	fs/base/FsFile.hpp
	fs/base/FsDirectory.hpp
	fs/base/FsStorage.hpp
	fs/inmemory/FsChunkInMemory.hpp
	fs/inmemory/FsChunkInMemory.cpp
	fs/inmemory/FsStorageInMemory.hpp
	fs/inmemory/FsStorageInMemory.cpp
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
