#pragma once

#include <string>
#include <memory>

#include "utils/Parameters.hpp"
#include "FileSystemManager.hpp"

namespace supercloud {

class StorageManager;
class ClusterManager;
class JnrfuseImpl;
class PropagateChange;
class ExchangeChunk;
class FlowAndPing;
class RequestCheckForModification;
class CleanerManager;

//TODO: allow to be a gateway : wantRead -> me -> hasZeData (it doesn't work right now)
class StandardManager : FileSystemManager {
	
protected:
	std::shared_ptr<StorageManager> storage;

	std::shared_ptr<ClusterManager> net;

	std::shared_ptr<JnrfuseImpl> os;
	
	std::shared_ptr<Parameters> myParameters;

	std::shared_ptr<PropagateChange> algoPropagate;
	//to initialize in constructor
	std::shared_ptr<ExchangeChunk> chunkRequester;
	std::shared_ptr<FlowAndPing> flowManager;
	std::shared_ptr<RequestCheckForModification> updateManager;
	
	std::string driveletter;
	std::string rootFolder = ".";
	
	std::shared_ptr<CleanerManager> cleanerM;
	
public:
	StandardManager();


	/**
	* !! Deprecated !! 
	 * Initialize a new network cluster.
	 * Do it if you want to start a cluster, when you can't join any other live instances.
	 */
	void initializeNewCluster();

	/**
	 * Launch the manager,<br>
	 * It load the filesystem<br>
	 * The it listen to port for incoming connections.
	 * @param dataPath path of the filesystem on the local drive
	 * @param port port to listen to.
	 */
	void initBdNet(const std::string& dataPath, int port);
	void initOs(const std::string& dataPath, std::string letter);

	void close();

	void propagateChange(FsObject& obj) override;

	FsDirectory getRoot() override;

	StorageManager& getDb() {
		return *storage;
	}

	ClusterManager& getNet() {
		return *net;
	}

	uint16_t getComputerId() override;
	uint64_t getUserId() override;
	uint64_t getGroupId() override;
	
	void requestDirUpdate(const std::string& path) override;

	FsChunk requestChunk(FsFileFromFile& file, FsChunk& chunk, ShortList& serverIdPresent) override;

	std::string getDrivePath() override {
		return driveletter;
	}
	std::string getRootFolder() override {
		return rootFolder;
	}


}

} // namespace supercloud
