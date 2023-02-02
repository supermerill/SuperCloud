#pragma once

#include <string>
#include <iostream>

namespace supercloud {

class FileSystemManager {

public:
	FileSystemManager() {}
	////used by fs
	//virtual FsDirectory getRoot() = 0;

	//virtual void propagateChange(FsObject& fic) = 0;
	//
	//
	////not used
	////StorageManager getDb();
	////ClusterManager getNet();

	//virtual uint16_t getComputerId() = 0;
	//virtual uint64_t getUserId() = 0;
	//virtual uint64_t getGroupId() = 0;
	//
	void requestDirUpdate(const std::string& path) {
		std::cout << "FileSystemManager: requestDirUpdate on" << path << "\n";
	}
	//virtual FsChunk requestChunk(FsFileFromFile& file, FsChunk& chunk, ShortList& serverIdPresent) = 0;
	//
	//
	
	std::string getDrivePath() {
		return "C:/";
	}
	//virtual std::string getRootFolder() = 0;

};

} // namespace supercloud
