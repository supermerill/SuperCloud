#pragma once

#include <string>

namespace supercloud {

class FsChunk;
class FsFileFromFile;
class ShortList;
class FsObject;
class FsDirectory;
//class StorageManager;
//class ClusterManager;

class FileSystemManager {

public:
	//used by fs
	virtual FsDirectory getRoot() = 0;

	virtual void propagateChange(FsObject& fic) = 0;
	
	
	//not used
	//StorageManager getDb();
	//ClusterManager getNet();

	virtual uint16_t getComputerId() = 0;
	virtual uint64_t getUserId() = 0;
	virtual uint64_t getGroupId() = 0;
	
	virtual void requestDirUpdate(const std::string& path) = 0;
	virtual FsChunk requestChunk(FsFileFromFile& file, FsChunk& chunk, ShortList& serverIdPresent) = 0;
	
	
	
	virtual std::string getDrivePath() = 0;
	virtual std::string getRootFolder() = 0;

}

} // namespace supercloud
