#include  "StandardManager.hpp"

#include "network/ClusterManager.hpp"

namespace supercloud {

	StandardManager::StandardManager() {
		chunkRequester = std::make_shared<ExchangeChunk>{ *this };
		flowManager = std::make_shared<FlowAndPing>{ *this };
		updateManager = std::make_shared<RequestCheckForModification>{ *this };
	}

	void StandardManager::initializeNewCluster() {
		//check if we havn't any id
		if(getComputerId() < 0){
			
			//choose id
			net->initializeNewCluster();
			
		}
		
	}

	/**
	 * Launch the manager,<br>
	 * It load the filesystem<br>
	 * The it listen to port for incoming connections.
	 * @param dataPath path of the filesystem on the local drive
	 * @param port port to listen to.
	 */
	void StandardManager::initBdNet(const std::string& dataPath, int port) {
		try{
			File mainDir = new File(".");
			remi.distributedFS.fs.Parameters paramsMana = new remi.distributedFS.fs.Parameters(mainDir.getAbsolutePath()+"/standardManager.properties");
			
			rootFolder = dataPath;
			
			//check if folder exist
			if(!new File(rootFolder).exists()){
				new File(rootFolder).mkdirs();
			}

			Logs.logManager.info("== begin init bd");
			FsTableLocalFactory storageFactory = new FsTableLocal.FsTableLocalFactory();
			storageFactory.rootRep = dataPath+"/data";
			storageFactory.filename = dataPath+"/"+"localdb.data";
			storageFactory.manager = this;
			if(paramsMana.getstd::stringOrDef("StorageType", "Default").contains("FsChunkOneFile")){
				storageFactory.factory = new FsChunkOneFile.StorageFactory();
			}else{
				storageFactory.factory = new ObjectFactory.StandardFactory(); //ie FSChunkFromFile.StorageFactory();
			}
			storage = storageFactory.create();
//			storage = new FsTableLocal(dataPath, dataPath+"/"+"localdb.data", this, true);
			storage.cleanUnusedSectors(true);
			

			
			//algo propagate
			if(paramsMana.getstd::stringOrDef("AlgoPropagate", "Default").equals("Grab")) {
				Logs.logManager.info("PropagateChangeAndGrabData");
				algoPropagate = new PropagateChangeAndGrabData(this);
			}else {
				//"Default"
				Logs.logManager.info("PropagateChange");
				algoPropagate = new PropagateChange(this);
			}
			
	
			if(port>0){
				Logs.logManager.info("== create net");
				net = new PhysicalServer(this, false, dataPath);
				Logs.logManager.info("== init net");
				net.init(port);
				algoPropagate.register(this.net);
				chunkRequester.register(this.net);
//				flowManager.register(this.net);
//				flowManager.startAutoRefresh();__________�-__________________����_________
				updateManager.register(net);
				updateManager.startRefreshThread(1000);
			}
			

			//TODO: serialize & gui
//			cleaner = new Cleaner(this, 1024*1024*256, 1024*1024*1024, 1000*60);
			Logs.logManager.info("== create cleaner");
			cleanerM = new CleanerManager(this);
			cleanerM.start();
		}catch (Exception e) {
			e.printStackTrace();
			close();
		}
	}


	void StandardManager::initOs(const std::string& dataPath, const std::string& letter) {
		try{
	
			//wait initialization before giving hand to men
			try {
				while(getComputerId()<0){
					System.err.println("Warn : waiting for connection");
					Thread.sleep(1000);
				}
			} catch (InterruptedException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
			
			if(letter != null){
				os = new JnrfuseImpl(this);
				os.init(letter);
				driveletter = letter;
			}else driveletter = " ";
			
			Logs.logManager.info("end of init");
			
			try {
				Thread.sleep(4000);
			} catch (InterruptedException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
			net.launchUpdater();
		}catch (Exception e) {
			e.printStackTrace();
			close();
		}
	}

	void StandardManager::close() {
		net->close();
		os->close();
	}

	void StandardManager::propagateChange(FsObject& obj) {
		obj.accept(algoPropagate);
	}

	FsDirectory StandardManager::getRoot() {
		return storage->getRoot();
	}

	uint16_t StandardManager::getComputerId() {
		//TODO
		if(!net) return uint16_t(-1);
//		return os.getContext().pid.longValue();
		return net->getComputerId();
	}

	uint64_t StandardManager::getUserId() {
		if(os) return 0;
		return os->getContext().uid.longValue();
	}

	uint64_t StandardManager::getGroupId() {
		if (os) return 0;
		return os->getContext().gid.longValue();
	}

	const std::string& StandardManager::getDrivePath() {
		return driveletter;
	}

	void StandardManager::requestDirUpdate(const std::string& path) {
		algoPropagate->requestDirPath(path,-1);
	}

	FsChunk StandardManager::requestChunk(FsFileFromFile& file, FsChunk& chunk, ShortList& serverIdPresent) {
		Logs.logManager.info("REQUEST CHUNK "+chunk.getId());
		//request chunk to all servers
		int nbReq = chunkRequester.requestchunk(serverIdPresent, file, chunk);
		//register to the chunk requester
//		boolean ok = false;
		FsChunk chunkReceived = null;
		try{
//			while(!ok){
			chunkReceived = chunkRequester.waitReceiveChunk(chunk.getId(), file.getId(), file.getModifyDate(), nbReq);
//				if(chunk.)
				//TODO : check if it's our chunk
				if(chunkReceived != null){
//					ok = true;
				}else{
					Logs.logManager.info("Can't find chunk(1) "+file.getPath()+" id:"+chunk.getId());
				}
//			}
		}catch(RuntimeException ex){
			ex.printStackTrace();
			Logs.logManager.info("Can't find chunk(2) "+file.getPath()+" id:"+chunk.getId());
		}
		return chunkReceived;
	}


}
