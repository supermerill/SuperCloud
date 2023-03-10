cmake_minimum_required(VERSION 3.13)
project(libsupercloud)

# boost nowide
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
    network/IdentityManagerCrypto.cpp
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
	fs/base/FsElt.cpp
	fs/base/FsElt.hpp
	fs/base/FsObject.hpp
	fs/base/FsChunk.hpp
	fs/base/FsFile.hpp
	fs/base/FsDirectory.hpp
	fs/base/FsStorage.cpp
	fs/base/FsStorage.hpp
	fs/inmemory/FsDirInMemory.hpp
	fs/inmemory/FsDirInMemory.cpp
	fs/inmemory/FsFileInMemory.hpp
	fs/inmemory/FsFileInMemory.cpp
	fs/inmemory/FsChunkInMemory.hpp
	fs/inmemory/FsChunkInMemory.cpp
	fs/inmemory/FsStorageInMemory.hpp
	fs/inmemory/FsStorageInMemory.cpp
#	fs/local/FsDirLocal.hpp
#	fs/local/FsDirLocal.cpp
#	fs/local/FsFileLocal.hpp
#	fs/local/FsFileLocal.cpp
#	fs/local/FsChunkLocal.hpp
#	fs/local/FsChunkLocal.cpp
#	fs/local/FsStorageLocal.hpp
#	fs/local/FsStorageLocal.cpp
	synch/AbstractFSMessageManager.hpp
	synch/ExchangeChunkMessageManager.cpp
	synch/ExchangeChunkMessageManager.hpp
	synch/SynchTreeMessageManager.cpp
	synch/SynchTreeMessageManager.hpp
	synch/RequestCheckForModification.hpp
	synch/SynchroDb.cpp
	synch/SynchroDb.hpp
	os/FuseImpl.hpp
	os/FuseInterface.hpp
)

#encoding_check(libsupercloud)
#set(cryptopp_ROOT ${CRYPTOPP_CMAKE_INSTALL_ROOT})
#find_package(cryptopp REQUIRED static PATHS ${CRYPTOPP_CMAKE_INSTALL_ROOT})



# target_compile_definitions(libsupercloud PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_include_directories(libsupercloud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libsupercloud PUBLIC ${EXPAT_INCLUDE_DIRS})
target_link_libraries(libsupercloud PUBLIC
    # miniz
    boost_libs
    nowide
    cryptopp::cryptopp
    )

if (APPLE)
    target_compile_options(libsupercloud PUBLIC "-fno-aligned-allocation")
endif ()
