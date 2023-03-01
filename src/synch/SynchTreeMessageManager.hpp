#pragma once

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsObject.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"
#include "network/Peer.hpp"

#include <map>
#include <unordered_set>

namespace supercloud {

	class SynchTreeMessageManager : public AbstractMessageManager, public std::enable_shared_from_this<SynchTreeMessageManager> {

	public:

		struct TreeAnswerElt {
			FsID elt_id;
			uint16_t elt_depth;
			size_t elt_size;
			FsID last_commit_id;
			DateTime last_commit_time;
			bool operator==(const TreeAnswerElt& other) const {
				return elt_id == other.elt_id && elt_depth == other.elt_depth && elt_size == other.elt_size
					&& last_commit_id == other.last_commit_id && last_commit_time == other.last_commit_time;
			}
		};
		struct TreeAnswerEltChange : TreeAnswerElt {
			std::vector<FsID> state;
			bool operator==(const TreeAnswerEltChange& other) const {
				return TreeAnswerElt::operator==(other) && state == other.state;
			}
		};
		struct TreeAnswerEltDeleted : TreeAnswerElt {
			FsID renamed_to; //can be 0 if just deleted
			bool operator==(const TreeAnswerEltDeleted& other) const {
				return TreeAnswerElt::operator==(other) && renamed_to == other.renamed_to;
			}
		};
		//a fake object, to have all information about the last state of the object (with id & date of the last commit, but no data inside)
		class FsObjectTreeAnswer : public FsObject {
			uint16_t m_depth;
			size_t m_size;
		public:
			FsObjectTreeAnswer(FsID id, uint16_t depth, size_t size, DateTime date, std::string name, CUGA puga, FsID parent, uint32_t group, std::vector<FsID> state) :
				FsObject(id, date, name, puga, parent), m_depth(depth), m_size(size) {
				m_current_state = state;
			}
			FsObjectTreeAnswer(FsID id, uint16_t depth, size_t size, std::vector<FsID> state) :
				FsObject(id, 0, "", 0, 0), m_depth(depth), m_size(size) {
				m_current_state = state;
			}
			FsObjectTreeAnswer& setCommit(FsID id_commit, DateTime date) {
				m_commits.emplace_back();
				m_commits.back().id = id_commit;
				m_commits.back().date = date;
				return *this;
			}
			FsObjectTreeAnswer& setDeleted(FsID renamed_to, DateTime date) {
				m_date_deleted = date;
				m_renamed_to = renamed_to;
				return *this;
			}
			//default -set
			FsObjectTreeAnswer(const FsObjectTreeAnswer& o) // can copy
				:FsObject(o.m_id, o.m_creation_date, o.m_name, o.m_puga, o.m_parent), m_depth(o.m_depth) {
				m_current_state = std::move(o.m_current_state);
			}
			FsObjectTreeAnswer(FsObjectTreeAnswer&& o) // can move
				:FsObject(o.m_id, o.m_creation_date, o.m_name, o.m_puga, o.m_parent), m_depth(o.m_depth) {
				m_current_state = std::move(o.m_current_state);
			}
			uint16_t getDepth() const override {
				return m_depth;
			}
			size_t size() const override {
				return m_size;
			}
			bool operator==(const FsObjectTreeAnswer& other) const {
				return m_depth == other.m_depth && m_size == other.m_size && m_id == other.m_id
					&& m_creation_date == other.m_creation_date && m_puga == other.m_puga && m_name == other.m_name
					&& m_group_id == other.m_group_id && m_parent == other.m_parent && m_current_state == other.m_current_state
					&& m_commits == other.m_commits && m_date_deleted == other.m_date_deleted && m_renamed_to == other.m_renamed_to
					&& m_renamed_from == other.m_renamed_from;
			}
		};

		struct TreeRequest {
			//what we want as information (the FsID we don't know about)
			std::vector<FsID> roots;
			//how many information
			size_t depth; // 0=only these one, 1 = also their childs, etc... uint16_t(-1) (ie the max as it's unsigned) for evrything, but i guess 16k depth is enough
			//last commit we have for this element (if it's the same you have, then you can return a null answer)
			FsID last_commit_received; // note: currently not used to crate the answer TODO: do something with it or erase?
			// last time we fetched you (please give all information that you received/modified since that moment)
			DateTime last_fetch_time;
		};
		typedef std::shared_ptr<FsObjectTreeAnswer> FsObjectTreeAnswerPtr;
		struct TreeAnswer {
			ComputerId from;
			// time of the fetch
			DateTime answer_time;
			// all the element in the tree requested that have been modified / created.
			std::vector<TreeAnswerEltChange> modified;
			std::vector<FsObjectTreeAnswerPtr> created; //ptr because it's immutable and vector can't store immutable things...
			// 1 here are the elements that are renamed to something else
			// 2 the deleted element may not be listed. You have to infer them (all childs of a modified dir that are not in its current state anymore are deleted)
			// for renamed element, check the renamed_from from the created elements.
			// 1 we need the delete date & commit id
			// if a directory is deleted, all the content is also deleted (using the same commit id is okay-ish, but it should be marked as "unknown deletion id")
			std::vector<TreeAnswerEltDeleted> deleted;
		};

	protected:

		std::shared_ptr<FsStorage> m_filesystem;
		std::shared_ptr<SynchroDb> m_syncro;
		std::shared_ptr<ClusterManager> m_cluster_manager;
		
		std::mutex m_mutex_incomplete_requests;
		std::map<ComputerId, TreeAnswer> m_incomplete_requests;

		void register_listener();

		void fillTreeAnswer(TreeAnswer& answer, FsID elt_id, size_t depth, DateTime since);
	public:
		//factory
		[[nodiscard]] static std::shared_ptr<SynchTreeMessageManager> create(std::shared_ptr<ClusterManager> physical_server) {
			std::shared_ptr<SynchTreeMessageManager> pointer = std::shared_ptr<SynchTreeMessageManager>{ new SynchTreeMessageManager(/*TODO*/) };
			pointer->m_cluster_manager = physical_server;
			pointer->register_listener();
			return pointer;
		}

		std::shared_ptr<SynchTreeMessageManager> ptr() {
			return shared_from_this();
		}


		void receiveMessage(PeerPtr peer, uint8_t messageId, ByteBuff message) override;




		ByteBuff writeTreeRequestMessage(const TreeRequest& request);
		TreeRequest readTreeRequestMessage(ByteBuff& buffer);

		ByteBuff writeTreeAnswerMessage(const TreeAnswer& request);
		TreeAnswer readTreeAnswerMessage(ByteBuff& buffer);

		TreeAnswer answerTreeRequest(const PeerPtr sender, TreeRequest&& request);

		void useTreeRequestAnswer(const PeerPtr sender, TreeAnswer&& answer);
	};
	//	void putHeader(ByteBuff message, FsEltPtr obj) {
	//		message.putLong(obj->getDate());

	//		message.putLong(obj.getParentId());
	//		message.putLong(obj.getUserId());
	//		message.putLong(obj.getGroupId());
	//		message.putShort(obj.getPUGA());

	//		message.putLong(obj.getCreationDate());
	//		message.putLong(obj.getDeleteDate());

	//		message.putUTF8(obj.getName());

	//		message.putLong(obj.getCreatorUID());
	//		message.putLong(obj.getModifyUID());
	//		message.putLong(obj.getDeleteUID());
	//	}

	//	//	public boolean isDeleted(ByteBuff message){
	//	//		//check if parentId is -1
	//	//		int pos = message.position();
	//	//		long myId = message.getLong();
	//	//		long modifyDate = message.getLong();
	//	//		long parentId = message.getLong();
	//	//		
	//	//		message.position(pos);
	//	//		
	//	//		return parentId < 0;
	//	//	}

	//	public void readHeader(ByteBuff message, FsObject obj) {
	//		obj.setModifyDate(message.getLong());

	//		obj.setParentId(message.getLong());
	//		obj.setUserId(message.getLong());
	//		obj.setGroupId(message.getLong());
	//		obj.setPUGA(message.getShort());

	//		obj.setCreationDate(message.getLong());
	//		obj.setDeleteDate(message.getLong());

	//		obj.setName(message.getUTF8());

	//		obj.setCreatorUID(message.getLong());
	//		obj.setModifyUID(message.getLong());
	//		obj.setDeleteUID(message.getLong());
	//	}

	//	public boolean copyHeader(FsObject from, FsObject to) {
	//		Logs.logManager.info(this.manager.getComputerId() % 100 + " getHeader ");
	//		if (from.getModifyDate() > to.getModifyDate()) {
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " sended dir is changed! nice! : " + from.getModifyDate() + " > " + to.getModifyDate());
	//			to.setModifyDate(from.getModifyDate());
	//			//			//more recent, update!

	//			to.setParentId(from.getParentId());
	//			to.setUserId(from.getUserId());
	//			to.setGroupId(from.getGroupId());
	//			to.setPUGA(from.getPUGA());

	//			to.setCreationDate(from.getCreationDate());
	//			to.setDeleteDate(from.getDeleteDate());

	//			to.setName(from.getName());

	//			to.setCreatorUID(from.getCreatorUID());
	//			to.setModifyUID(from.getModifyUID());
	//			to.setDeleteUID(from.getDeleteUID());

	//			return true;
	//		} else {
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " sended dir is OOOOld: " + from.getModifyDate() + " < " + to.getModifyDate());
	//			return false;

	//		}
	//	}

	//	private ByteBuff createNotFindPathMessage(String path) {
	//		Logs.logManager.info(this.manager.getComputerId() % 100 + " WRITE SEND NO PATH OBJ " + path);
	//		ByteBuff message = new ByteBuff();
	//		message.put((byte)0);
	//		//send path
	//		message.putUTF8(path);
	//		return message.flip();
	//	}

	//	public void requestDirPath(String path, long dirId) {
	//		Logs.logManager.info(this.manager.getComputerId() % 100 + " WRITE GET DIR " + path + " for all");
	//		ByteBuff buff = new ByteBuff();
	//		buff.putUTF8(path);
	//		buff.putLong(dirId);
	//		buff.flip();
	//		manager.getNet().writeBroadcastMessage(GET_DIR, buff);
	//	}

	//	public void requestFilePath(String path, long fileId) {
	//		Logs.logManager.info(this.manager.getComputerId() % 100 + " WRITE GET FILE MESSAGE : " + path + " : " + fileId);
	//		ByteBuff buff = new ByteBuff();
	//		buff.putUTF8(path);
	//		buff.putLong(fileId);
	//		buff.flip();
	//		manager.getNet().writeBroadcastMessage(GET_FILE_DESCR, buff);
	//	}

	//	public void requestDirPath(long serverId, String path, long dirId) {
	//		Logs.logManager.info(this.manager.getComputerId() % 100 + " WRITE GET DIR MESSAGE : " + path + " : " + dirId + " for " + serverId);
	//		ByteBuff buff = new ByteBuff();
	//		buff.putUTF8(path);
	//		buff.putLong(dirId);
	//		buff.flip();
	//		manager.getNet().writeMessage(serverId, GET_DIR, buff);
	//	}

	//	public void requestFilePath(long serverId, String path, long fileId) {
	//		Logs.logManager.info(this.manager.getComputerId() % 100 + " WRITE GET FILE MESSAGE : " + path + " : " + fileId + " for " + serverId);
	//		ByteBuff buff = new ByteBuff();
	//		buff.putUTF8(path);
	//		buff.putLong(fileId);
	//		buff.flip();
	//		manager.getNet().writeMessage(serverId, GET_FILE_DESCR, buff);
	//	}

	//	//TODO: sendobject, and receive for the 2 of them.
	//	@Deprecated
	//		public void requestObject(long serverId, long objId) {
	//		//		Logs.logManager.info(this.manager.getComputerId()%100+" WRITE GET DIR "+path+" for "+senderId);
	//		ByteBuff buff = new ByteBuff();
	//		buff.putLong(objId);
	//		buff.flip();
	//		manager.getNet().writeMessage(serverId, GET_OBJECT, buff);
	//	}

	//	public ByteBuff createFileDescrMessage(FsFile fic) {
	//		String path = fic.getPath();
	//		//create dir data update.
	////		Logs.logManager.info(manager.getNet().getId()%100+" WRITE SEND FILE "+path+" for "+senderId);
	//		ByteBuff message = new ByteBuff();
	//		message.put((byte)1);
	//		//send path
	//		message.putUTF8(path);
	//		//id
	//		message.putLong(fic.getId());
	//		//send metadata
	//		putHeader(message, fic);
	//		//send nbChunks
	//		message.putTrailInt(fic.getChunks().size());
	//		//send my chunks timestamps (0 if i didn't have this one
	//		final int sizeChunk = fic.getChunks().size();
	//		for (int i = 0; i < sizeChunk; i++) {
	//			FsChunk chunk = fic.getChunks().get(i);
	//			if (chunk != null) {
	//				message.putLong(chunk.getId());
	//				if (chunk.isPresent()) {
	//					message.putLong(chunk.getModifyDate());
	//				} else {
	//					message.putLong(-1);
	//				}
	//				message.putInt(chunk.currentSize());
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + "emit for file " + fic.getPath() + " his " + i + "� Chunk " + chunk.getId() + ", size=" + chunk.currentSize() + " @" + (chunk.isPresent() ? chunk.getModifyDate() : -1));
	//			} else {
	//				System.err.println(this.manager.getComputerId() % 100 + " SEND FILE ERROR: null chunk!! (pos " + i + ")");
	//				message.putLong(-1);
	//				message.putLong(-1);
	//				message.putInt(-1);
	//			}
	//		}
	//		message.flip();
	//		return message;
	//	}

	//	protected void getAChunk(FsChunk newCHunk) {
	//		//		Logs.logManager.info(this.manager.getComputerId()%100+" CHUNKCG !! aeff !! new chunk : "+newCHunk.getId());
	//	}

	//	protected void getAFileFrom(long senderId, ByteBuff message) {

	//		if (message.get() == 1) {
	//			String path = message.getUTF8();
	//			long receivedId = message.getLong();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG RECEIVE FILE " + path + " " + receivedId + " from " + senderId);
	//			FsObjectDummy dummy = new FsObjectDummy(receivedId, path);
	//			readHeader(message, dummy);
	//			if (receivedId < 0) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG  Id = " + receivedId + " << USELESSS!!!!!!");
	//				return;
	//			}


	//			//get the dir object or create it if not present
	//			FsFile fic = getOrCreateOrRequestObject(senderId, path, receivedId, dummy, fileFunc);
	//			if (fic == null) {
	//				return;
	//			}

	//			//check if moved
	//			byte shouldContinue = checkObjectMoved(senderId, fic, path, dummy, fileFunc);
	//			if (shouldContinue < 0) {
	//				return;
	//			}

	//			//copy header  / properties
	//			long tempOldLong = fic.getModifyDate();
	//			if (copyHeader(dummy, fic) || shouldContinue > 0) {
	//				fic.flush();
	//			}


	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG old mod date (fic) : " + tempOldLong + ", new one : " + fic.getModifyDate());

	//			//check chunks
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG check fic inside " + fic.getPath());
	//			int nbChunks = message.getTrailInt();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG " + fic.getPath() + " has " + nbChunks + " chunks");
	//			List<FsChunk> chunksToSet = new ArrayList<>();
	//			final short peerServerId = manager.getNet().getComputerId(senderId);
	//			for (int i = 0; i < nbChunks; i++) {
	//				long chunkId = message.getLong();
	//				long chunkDate = message.getLong();
	//				int chunkSize = message.getInt();
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + "thius file has his " + i + "� Chunk " + chunkId + ", size=" + chunkSize + " @" + chunkDate);
	//				if (chunkId != -1) {
	//					FsChunk chunk = FsFile.getChunk(fic, chunkId);
	//					boolean used = true;
	//					if (chunk == null) {
	//						//search in allChunks
	//						chunk = FsFile.getFromAllChunk(fic, chunkId);
	//						used = false;
	//					}
	//					if (chunk != null) {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + "mine version: " + i + "� Chunk " + chunk.getId() + ", size=" + chunk.currentSize() + " @" + (chunk.isPresent() ? chunk.getModifyDate() : -1));
	//					}
	//					if (chunk == null) {
	//						//if can't retrieve by id, maybe new?
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG oh, a new chunk");
	//						//						requestDirPath(senderId, dir.getPath()+"/"+dirName, chunkId);
	//						chunk = fic.createNewChunk(chunkId);
	//						//						chunk.setLastModificationTimestamp()
	//						if (!chunk.serverIdPresent().contains(peerServerId)) chunk.serverIdPresent().add(peerServerId);
	//						chunk.setCurrentSize(chunkSize);
	//						chunk.flush();
	//					} else if (chunk.getId() != chunkId) {
	//						System.err.println(this.manager.getComputerId() % 100 + " FILECG oh, a chunk BAD ID : impossibkle!!!!");
	//						// bad id: request the two of them.
	////						requestDirPath(senderId, dir.getPath()+"/"+dirName, childDir.getId()); //this one should get us the two files, one from path, one from id
	//					} else {
	//						boolean modified = false;
	//						//check lastChangeDate & modifyDate
	//						if (chunk.getModifyDate() < chunkDate && !used) {
	//							Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG oh, a chunk was re-used : " + chunk.getId());
	//							//this dir has something modified (inside or deep inside)
	//							//request a recursive check. NOT FOR CHUNK
	////							requestDirPath(senderId, childDir.getPath(), childDir.getId());
	//						}
	//						if (chunkDate < 0) {
	//							//not present, be sure to not have the peerServerId in our server id in the chunk
	//							chunk.serverIdPresent().rem(peerServerId);
	//							modified = true;
	//						} else {
	//							if (!chunk.serverIdPresent().contains(peerServerId)) {
	//								chunk.serverIdPresent().add(peerServerId);
	//								modified = true;
	//							}
	//							if (chunk.getModifyDate() < chunkDate) {
	//								Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG oh, a chunk was updated : " + chunk.getId());
	//								chunk.setPresent(false);
	//								chunk.setCurrentSize(chunkSize);
	//								modified = true;
	//								Logs.logManager.info(this.manager.getComputerId() % 100 + "mine new version: " + i + "� Chunk " + chunk.getId() + ", size=" + chunk.currentSize() + " @" + (chunk.isPresent() ? chunk.getModifyDate() : -1));
	//							}
	//						}
	//						if (modified) {
	//							chunk.flush();
	//						}
	//						//						direxist.add(childDir);
	//					}
	//					chunksToSet.add(chunk);
	//				}
	//			}
	//			//does it means something list<chunk>.equals?
	//			if (!chunksToSet.equals(fic.getChunks())) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG : chunks are not the sames : ");
	//				System.out.print(" their=");
	//				for (FsChunk ch : chunksToSet) { System.out.print(", " + ch.getId()); }
	//				Logs.logManager.info(" ; ");
	//				System.out.print(" our=");
	//				for (FsChunk ch : fic.getChunks()) { System.out.print(", " + ch.getId()); }
	//				Logs.logManager.info("");
	//				fic.setChunks(chunksToSet);
	//				fic.flush();
	//				for (int i = 0; i < fic.getChunks().size(); i++) {
	//					FsChunk chunk = fic.getChunks().get(i);
	//					getAChunk(chunk);
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " FILECG : chunk " + i + " : " + chunk.getId() + ", size=" + chunk.currentSize() + " @" + (chunk.isPresent() ? chunk.getModifyDate() : -1));
	//				}
	//			}

	//		} else {
	//			String mypath = message.getUTF8();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " READ SEND FILE : NO file " + mypath + " from " + senderId);
	//			System.err.println("I Requested an not existant file : " + mypath);
	//			// request his par<ent dir, to see if it's not deleted.
	//			FsDirectory dirParent = getPathParentDir(manager.getRoot(), mypath);
	//			//don't do that, it create a dead loop parent->child->parent->child->.....
	//			//the parent should be updated in an other manner, like while a refresh, by seeing that a rep has been modified (lastchange)
	////			if(dirParent != null) requestDirPath(senderId, dirParent.getPath(), dirParent.getId());
	//		}

	//	}


	//	public ByteBuff createDirectoryMessage(FsDirectory dir) {
	//		Logs.logManager.info("WRITE SEND DIR MESSAGE : " + dir.getPath() + " : " + dir.getModifyDate());

	//		String path = dir.getPath();
	//		if (path.equals("") && dir.getParent() == dir) {
	//			path = "/";
	//		}

	//		//		Logs.logManager.info(this.manager.getComputerId()%100+" WRITE SEND DIR "+path+" for "+senderId);
	//		ByteBuff message = new ByteBuff();
	//		message.put((byte)1);
	//		message.putUTF8(path);
	//		message.putLong(dir.getId());
	//		message.putLong(dir.getLastChangeDate());
	//		message.putLong(dir.getLastChangeUID());
	//		putHeader(message, dir);
	//		List<FsDirectory> arrayDir = new ArrayList<>(dir.getDirs());
	//		message.putTrailInt(arrayDir.size());
	//		Logs.logManager.info("WRITE DIR : " + arrayDir.size());
	//		for (FsDirectory dchild : arrayDir) {
	//			Logs.logManager.info(" -D-> : " + dchild.getName());
	//			message.putUTF8(dchild.getName());
	//			message.putLong(dchild.getId());
	//			message.putLong(dchild.getModifyDate());
	//			message.putLong(dchild.getLastChangeDate());
	//		}
	//		List<FsFile> arrayFile = new ArrayList<>(dir.getFiles());
	//		message.putTrailInt(arrayFile.size());
	//		Logs.logManager.info("WRITE FIC : " + arrayFile.size());
	//		for (FsFile fchild : arrayFile) {
	//			Logs.logManager.info(" -F-> : " + fchild.getName());
	//			message.putUTF8(fchild.getName());
	//			message.putLong(fchild.getId());
	//			message.putLong(fchild.getModifyDate());
	//		}
	//		List<FsObject> arrayDel = new ArrayList<>(dir.getDelete());
	//		Logs.logManager.info("WRITE DEL : " + arrayDel.size());
	//		message.putTrailInt(arrayDel.size());
	//		for (FsObject ochild : arrayDel) {
	//			//			Logs.logManager.info("WRITE DEL NAME : "+ochild.getName().length()+" "+ochild.getName());
	//			Logs.logManager.info(" -R-> : " + ochild.getName());
	//			message.putUTF8(ochild.getName());
	//			message.putLong(ochild.getId());
	//			message.putLong(ochild.getDeleteDate());
	//		}
	//		message.flip();
	//		return message;
	//	}

	//	private <E extends FsObject> E containsId(List<E> lst, long id) {
	//		for (E e : lst) {
	//			if (e.getId() == id) {
	//				return e;
	//			}
	//		}
	//		return null;
	//	}


	//	public <T extends FsObject> byte checkObjectMoved(final long senderId, final T obj, final String path, final FsObject dummy, final FsObjectFunc<T> func) {
	//		//check if moved
	//		if (obj.getPath().equals(path) || path.equals("/")) {
	//			//not moved : ok
	//		} else if (obj.getModifyDate() < dummy.getModifyDate()) {
	//			//move it
	//			FsDirectory root = manager.getRoot();
	//			//TODO: refactor "get parent dir from path" , it's used multiple time : create external function.
	//			FsDirectory dirParent = getPathParentDir(root, path);
	//			if (dirParent == null) {
	//				dirParent = getPathDir(root, path.substring(0, path.lastIndexOf('/')));
	//			}
	//			if (dirParent == null) {
	//				//request parent before child.
	//				requestDirPath(senderId, path.substring(0, path.lastIndexOf('/')), -1);
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG can't find parent(2) : request parent and terminate this emssage parsing. " + path + "==" + getPathParentDir(root, path) + " -> " + path.substring(0, path.lastIndexOf('/')));
	//				return -1;
	//			}
	//			//move
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG move dir " + obj.getPath() + " from " + obj.getParent().getPath() + " to " + dirParent.getPath());
	//			if (obj.getParent() != dirParent) {
	//				//				dir.getParent().moveDir(dir, dirParent);
	//				func.move(obj, dirParent);
	//				return 1;
	//			}
	//			//rename
	//			String newName = path.substring(path.lastIndexOf('/') + 1);
	//			if (!obj.getName().equals(newName)) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG rename dir " + obj.getName() + " to " + newName);
	//				obj.setName(newName);
	//				obj.changes();
	//				return 1;
	//			}
	//		} else {
	//			//conflict : don't move it! / create a copy? -> TODO
	//			System.err.println(this.manager.getComputerId() % 100 + " OBJCG conflict : a dir has been moved by " + senderId + " but i have already modified it! my date : " + obj.getModifyDate() + ", their date : " + dummy.getModifyDate());
	//			//don't merge the rest
	//			return -1;
	//		}
	//		return 0;
	//	}

	//	public <T extends FsObject> T getOrCreateOrRequestObject(final long senderId, final String path, final long objId, final FsObject dummy, final FsObjectFunc<T> func) {
	//		FsDirectory root = manager.getRoot();
	//		T searchedObj = func.getFromPath(root, path);
	//		if (searchedObj == null) {
	//			//try to see if it has moved
	//			FsObject obj = manager.getDb().getDirect(objId);
	//			if (obj != null) {
	//				searchedObj = func.asObject(obj);
	//				if (searchedObj == null) {
	//					System.err.println(this.manager.getComputerId() % 100 + " OBJCG Finded a displaced thing! : " + obj + " which isn't a ? : " + obj.getPath() + " -> " + path);
	//					System.err.println("TODO: correct the error");
	//					return null;
	//				} else {
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG Finded a displaced/deleted dir : " + obj.getPath() + " -> " + path);
	//				}
	//			}
	//		}
	//		if (searchedObj != null && searchedObj.getId() != dummy.getId()) {
	//			System.err.println("WARN : we have a collision!!");
	//			searchedObj = null;
	//			//we should rename the most ancien one, and check if it was not deleted (it will be done by sopmehting else). => be sure than our renaming don't avoid the deletion!!
	//			//try to get the right one
	//			FsObject obj = manager.getDb().getDirect(objId);
	//			if (obj != null) {
	//				searchedObj = func.asObject(obj);
	//				if (searchedObj == null) {
	//					System.err.println(this.manager.getComputerId() % 100 + " OBJCG Finded a replaced thing! : " + obj + " which isn't a ? : " + path);
	//					System.err.println("TODO: correct the error");
	//					return null;
	//				} else {
	//					System.err.println(this.manager.getComputerId() % 100 + " WARN :  OBJCG Finded a displaced/deleted dir : " + obj.getPath() + " -> " + path + " (and a different item in the old place)");
	//				}
	//			} else {
	//				System.err.println(this.manager.getComputerId() % 100 + " WARN :  OBJCG Finded a new thing in the place of an other thing! @ " + path);
	//			}
	//		}
	//		if (searchedObj == null) {
	//			//check if it's not a delete notification
	//			if (dummy.getDeleteDate() > 0) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG notif of detion on an not-existant folder -> no-event!");
	//				return null;
	//			} else {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG can't find obj " + path);
	//				//Create new obj!
	//				FsDirectory dirParent = getPathParentDir(root, path);
	//				if (dirParent == null) {
	//					dirParent = getPathDir(root, path.substring(0, path.lastIndexOf('/')));
	//				}
	//				if (dirParent == null) {
	//					//request parent before child.
	//					requestDirPath(senderId, path.substring(0, path.lastIndexOf('/')), -1);
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " OBJCG can't find parent : request parent and terminate this emssage parsing. " + path + "==" + getPathParentDir(root, path) + " -> " + path.substring(0, path.lastIndexOf('/')));
	//					return null;
	//				}
	//				searchedObj = func.createNew(dirParent, getPathObjectName(root, path));
	//				searchedObj.setId(objId);
	//				searchedObj.flush();
	//			}
	//		} 		else {
	//			//check if deleted
	//			if (dummy.getDeleteDate() > 0) {
	//				//deletion message, check if deleted
	//				if (searchedObj.getDeleteDate() > 0) {
	//					//ok
	//				} else {
	//					//delete it, if it's newer than our modif date
	//					if (searchedObj.getModifyDate() < dummy.getDeleteDate()) {
	//						//delete
	//						func.remove(searchedObj.getParent(), searchedObj);
	//					} else {
	//						//emit a "warn"
	//						Logs.logManager.info("Warn : dir " + searchedObj.getPath() + " is deleted in a peer, but we already change his content.");
	//						//emit our version of the file
	//						func.send(senderId, searchedObj);
	//						//exit here
	//						return null;
	//					}
	//				}
	//			} else {
	//				//check if we have not deleted it
	//				if (searchedObj.getDeleteDate() > 0) {

	//					//un-delete it, if it's changes are newer than our modif date
	//					if (searchedObj.getDeleteDate() < dummy.getModifyDate()) {
	//						if (searchedObj.getParent() == searchedObj) {
	//							System.err.println("ERROR : root folder is deleted ???? : " + searchedObj.getPath() + " : " + searchedObj.getDeleteDate() + " : " + searchedObj.getModifyDate());
	//						}
	//						func.readd(searchedObj.getParent(), searchedObj);
	//						searchedObj.setDeleteDate(dummy.getDeleteDate()); //already done by the copyheader, i think, anyway.
	//					} else {
	//						//emit a "warn"
	//						Logs.logManager.info("Warn : dir " + searchedObj.getPath() + " is not deleted in a peer, but we already deleted it.");
	//						//emit our version of the file
	//						func.send(senderId, searchedObj);
	//						//exit here
	//						return null;
	//					}
	//				} else {
	//					//ok
	//				}
	//			}
	//		}
	//		return searchedObj;
	//	}


	//	public void getADirFrom(long senderId, ByteBuff message) {
	//		if (message.get() == 1) {
	//			String path = message.getUTF8();
	//			long receivedId = message.getLong();
	//			long lastChangeDate = message.getLong();
	//			long lastChangeUID = message.getLong();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG RECEIVE DIR " + path + " " + receivedId + " from " + senderId);
	//			FsObjectDummy dummy = new FsObjectDummy(receivedId, path);
	//			readHeader(message, dummy);
	//			if (receivedId < 0) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG  Id = " + receivedId + " << USELESSS!!!!!!");
	//				return;
	//			}


	//			//get the dir object or create it if not present
	//			FsDirectory dir = getOrCreateOrRequestObject(senderId, path, receivedId, dummy, dirFunc);
	//			if (dir == null) {
	//				return;
	//			}

	//			//check if moved
	//			byte shouldContinue = checkObjectMoved(senderId, dir, path, dummy, dirFunc);
	//			if (shouldContinue < 0) {
	//				return;
	//			}

	//			long tempOldLong = dir.getModifyDate();
	//			if (copyHeader(dummy, dir) || shouldContinue > 0) {
	//				shouldContinue = 1;
	//			}
	//			dir.setLastChangeDate(lastChangeDate);
	//			dir.setLastChangeUID(lastChangeUID);
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG old mod date : " + tempOldLong + ", new one : " + dir.getModifyDate());
	//			if (shouldContinue > 0) {
	//				dir.flush();
	//			}

	//			//now, explore childs to see if we need to request them.

	//			//check dirs
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG check dir inside " + dir.getPath());
	//			int nbDir = message.getTrailInt();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG " + dir.getPath() + " has " + nbDir + " dirs");
	//			Collection<FsDirectory> direxist = new ArrayList<FsDirectory>();
	//			for (int i = 0; i < nbDir; i++) {
	//				String dirName = message.getUTF8();
	//				long dirId = message.getLong();
	//				long dirDate = message.getLong();
	//				long dirLastChangeDate = message.getLong();
	//				FsDirectory childDir = getDir(dir, dirName);
	//				if (childDir == null) {
	//					//if can't retrieve by name, something may have changed, request it.
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a dir " + ((childDir == null) ? "null" : childDir.getName()) + " was name differently in the peer" + senderId % 100 + " : " + dirName);
	//					requestDirPath(senderId, dir.getPath() + "/" + dirName, dirId);
	//				} else if (childDir.getId() != dirId) {
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a dir BAD ID, same path : " + childDir.getPath());
	//					// bad id: request the two of them.
	//					requestDirPath(senderId, dir.getPath() + "/" + dirName, childDir.getId()); //this one should get us the two files, one from path, one from id
	//				} else {
	//					//check lastChangeDate & modifyDate
	//					if (childDir.getModifyDate() < dirDate || childDir.getLastChangeDate() < dirLastChangeDate) {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a dir has changed / a child has changed : " + childDir.getPath());
	//						//this dir has something modified (inside or deep inside)
	//						//request a recursive check.
	//						requestDirPath(senderId, childDir.getPath(), childDir.getId());
	//					} else {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG dir " + childDir.getPath() + " has not changed, no need to update (" + dirLastChangeDate + "<=" + childDir.getModifyDate() + ")");
	//					}
	//					direxist.add(childDir);
	//				}
	//			}


	//			int nbFile = message.getTrailInt();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG " + dir.getPath() + " has " + nbFile + " files");
	//			Collection<FsFile> fileExist = new ArrayList<>();
	//			for (int i = 0; i < nbFile; i++) {
	//				String fileName = message.getUTF8();
	//				long fileId = message.getLong();
	//				long fileDate = message.getLong();
	//				FsFile childFile = getFile(dir, fileName);
	//				if (childFile == null) {
	//					//can't retreive by name : request update for this file
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a file " + (childFile == null ? "null" : childFile.getName()) + " was name differently in the peer" + senderId % 100 + " : " + fileName);
	//					requestFilePath(senderId, dir.getPath() + "/" + fileName, fileId);
	//				} else if (childFile.getId() != fileId) {
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a file BAD ID, same path : " + childFile.getPath());
	//					// bad id: request the two of them.
	//					requestFilePath(senderId, dir.getPath() + "/" + fileName, childFile.getId()); //this one should get us the two files, one from path, one from id
	//				} else {
	//					//check lastChangeDate & modifyDate
	//					if (childFile.getModifyDate() < fileDate) {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a file has changed : " + childFile.getPath());
	//						//this dir has something modified (inside or deep inside), request a check
	//						requestFilePath(senderId, childFile.getPath(), childFile.getId());
	//					} else {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG file " + childFile.getPath() + " has not changed, no need to update (" + fileDate + "<=" + childFile.getModifyDate() + ")");
	//					}
	//					fileExist.add(childFile);
	//				}
	//			}

	//			//TODO: check if it work very well
	//			int nbDel = message.getTrailInt();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG " + dir.getPath() + " has " + nbDel + " del objects");
	//			for (int i = 0; i < nbDel; i++) {
	//				String objectName = message.getUTF8();
	//				long objectId = message.getLong();
	//				long deleteDate = message.getLong();
	//				//try to find them in dir/file exist 
	//				FsObject obj = null;
	//				boolean finded = false;
	//				//					Iterator<FsDirectory> itD = direxist.iterator();
	//				//					while(itD.hasNext() && !finded){
	//				//						obj = itD.next();
	//				//						if(obj.getName().equals(objectName) ){
	//				//							finded = true;
	//				//						}
	//				//					}
	//				//					Iterator<FsFile> itF = fileExist.iterator();
	//				//					while(itF.hasNext() && !finded){
	//				//						obj = itF.next();
	//				//						if(obj.getName().equals(objectName)){
	//				//							finded = true;
	//				//						}
	//				//					}
	//				obj = containsId(dir.getDirs(), objectId);
	//				if (obj == null) {
	//					obj = containsId(dir.getFiles(), objectId);
	//					if (obj == null) {
	//						finded = false;
	//					} else {
	//						finded = true;
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " need to delete file " + obj.getName() + " == " + objectName);
	//					}
	//				} else {
	//					finded = true;
	//					Logs.logManager.info(this.manager.getComputerId() % 100 + " need to delete dir " + obj.getName() + " == " + objectName);
	//				}

	//				if (finded) {
	//					//check date to see if we have to delete our
	//					if (obj.getModifyDate() <= deleteDate) {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " erase: " + path + (path.endsWith("/") ? "" : "/") + objectName);
	//						//							FsDirectory.FsDirectoryMethods.deleteAndFlush(obj);
	//						obj.accept(FsDirectory.FsDirectoryMethods.REMOVER);
	//						//						changes = true;
	//					}
	//				} else {
	//					obj = containsId(dir.getDelete(), objectId);
	//					if (obj == null) {

	//						// it was moved?
	//						obj = manager.getDb().getDirect(objectId);
	//						if (obj != null) {
	//							Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG oh, a dir " + obj.getPath() + " was moved AND deleted " + senderId % 100 + " : " + path + "/" + objectName);
	//							System.err.println("TODO: delete this entry");
	//						} else {
	//							Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG need ??  to add delete obj  ?? " + objectName);
	//						}
	//					} else {
	//						Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG already del obj " + obj.getName() + " == " + objectName);
	//					}
	//				}
	//			}

	//			//request updates for missing files & dirs (maybe moved?)
	//			Set<FsDirectory> missingDirs = new HashSet<>();
	//			missingDirs.addAll(dir.getDirs());
	//			missingDirs.removeAll(direxist);
	//			for (FsDirectory missingDir : missingDirs) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG request missing dir " + missingDir.getPath());
	//				requestDirPath(senderId, missingDir.getPath(), missingDir.getId());
	//			}

	//			Set<FsFile> missingFiles = new HashSet<>();
	//			missingFiles.addAll(dir.getFiles());
	//			missingFiles.removeAll(fileExist);
	//			for (FsFile missingFile : missingFiles) {
	//				Logs.logManager.info(this.manager.getComputerId() % 100 + " DIRCG request missing file " + missingFile.getPath());
	//				requestFilePath(senderId, missingFile.getPath(), missingFile.getId());
	//			}

	//		} else {
	//			String mypath = message.getUTF8();
	//			Logs.logManager.info(this.manager.getComputerId() % 100 + " READ SEND DIR : NO dir " + mypath + " from " + senderId);
	//			System.err.println("I Requested an not existant folder : " + mypath);
	//			// request his par<ent dir, to see if it's not deleted.
	//			//can be dead lock if "request missing dir" -> getparent -> "request missing dir"
	////			FsDirectory dirParent = getPathParentDir( manager.getRoot(), mypath);
	////			if(dirParent != null) requestDirPath(senderId, dirParent.getPath(), dirParent.getId());
	//		}
	//	}

	//	public void sendDir(long peerId, FsDirectory dir) {

	//		ByteBuff messageRet = createDirectoryMessage(dir);
	//		Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR for " + peerId);
	//		manager.getNet().writeMessage(peerId, SEND_DIR, messageRet);
	//	}

	//	public void sendfile(long peerId, FsFile file) {
	//		ByteBuff messageRet = createFileDescrMessage(file);
	//		manager.getNet().writeMessage(peerId, SEND_FILE_DESCR, messageRet);
	//	}

	//	@Override
	//		public void receiveMessage(long senderId, byte messageId, ByteBuff message) {
	//		if (manager.getNet().getComputerId(senderId) < 0) {
	//			//error: not a estabished peer
	//			System.err.println(manager.getNet().getComputerId() + "$ Error, peer " + senderId % 100 + " ask us a file/dir and he doens't have a computerid !" + manager.getNet().getComputerId(senderId));
	//			return;
	//		}
	//		if (messageId == SEND_FILE_DESCR) {
	//			Logs.logManager.info(this.manager.getComputerId() + "$ RECEIVE SEND FILE from " + senderId);
	//			//notTODO request chunks? -> i think it's more a db thing, to know if we want one. OR NOT
	//			getAFileFrom(senderId, message);
	//		}
	//		if (messageId == SEND_DIR) {
	//			Logs.logManager.info(this.manager.getComputerId() + "$ RECEIVE SEND DIR from " + senderId);
	//			getADirFrom(senderId, message);
	//		}
	//		if (messageId == GET_DIR) {
	//			String path = message.getUTF8();
	//			long idDir = message.getLong();
	//			Logs.logManager.info(this.manager.getComputerId() + "$ RECEIVE GET DIR " + path + "  from " + senderId);
	//			FsDirectory dirByPath = getPathDir(manager.getRoot(), path);
	//			FsDirectory dirById = dirByPath;
	//			if (dirByPath == null || dirByPath.getId() != idDir) {
	//				dirById = manager.getDb().getDirDirect(idDir);
	//			}
	//			ByteBuff messageRet = null;
	//			if (dirByPath == null && (dirById == null || dirById.asDirectory() == null)) {
	//				messageRet = createNotFindPathMessage(path);
	//				manager.getNet().writeMessage(senderId, SEND_DIR, messageRet);
	//				Logs.logManager.info(this.manager.getComputerId() + "$ NOT SEND DIR " + path + "  for " + senderId);
	//			} else if (dirByPath == null && dirById != null && dirById.asDirectory() != null) {
	//				//can find the id, send that
	//				messageRet = createDirectoryMessage(dirById.asDirectory());
	//				manager.getNet().writeMessage(senderId, SEND_DIR, messageRet);
	//				Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR " + path + "  (ID) for " + senderId);
	//			} else if (dirByPath != null && dirById == null) {
	//				//can find the path, send that
	//				messageRet = createDirectoryMessage(dirByPath);
	//				manager.getNet().writeMessage(senderId, SEND_DIR, messageRet);
	//				Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR " + path + "  (PATH) for " + senderId);
	//			} else {
	//				//check if it's the same id
	//				if (dirByPath.getId() == idDir) {
	//					///ok
	//					messageRet = createDirectoryMessage(dirByPath);
	//					manager.getNet().writeMessage(senderId, SEND_DIR, messageRet);
	//					Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR " + path + "  (impossible no conflict) for " + senderId);
	//				} else {
	//					//send what we have
	//					if (dirById.asDirectory() != null) {
	//						messageRet = createDirectoryMessage(dirById.asDirectory());
	//						manager.getNet().writeMessage(senderId, SEND_DIR, messageRet);
	//						Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR " + path + "  (conflict, id) for " + senderId);
	//					} else if (dirById.asFile() != null) { //note: this was possible when we used dirById = manager.getDb().getObjDirect(idDir);, dirById was an FSObject ==> and if it was a chunk?
	//						messageRet = createFileDescrMessage(dirById.asFile());
	//						manager.getNet().writeMessage(senderId, SEND_FILE_DESCR, messageRet);
	//						Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR->FILE " + path + "  (conflict, id) for " + senderId);
	//					}
	//					if (dirByPath != null) {
	//						messageRet = createDirectoryMessage(dirByPath);
	//						manager.getNet().writeMessage(senderId, SEND_DIR, messageRet);
	//						Logs.logManager.info(this.manager.getComputerId() + "$ WRITE SEND DIR " + path + "  (conflict, path) for " + senderId);
	//					}
	//				}
	//			}
	//		}
	//		if (messageId == GET_FILE_DESCR) {
	//			String path = message.getUTF8();
	//			Logs.logManager.info(this.manager.getComputerId() + "$ RECEIVE GET FILE " + path + " from " + senderId);
	//			FsFile file = getPathFile(manager.getRoot(), path);
	//			ByteBuff messageRet = null;
	//			if (file == null) {
	//				messageRet = createNotFindPathMessage(path);
	//			} else {
	//				messageRet = createFileDescrMessage(file);
	//			}
	//			manager.getNet().writeMessage(senderId, SEND_FILE_DESCR, messageRet);
	//		}
	//	}

	//	public void register(ClusterManager net) {
	//		net.registerListener(GET_DIR, this);
	//		net.registerListener(GET_FILE_DESCR, this);
	//		net.registerListener(SEND_DIR, this);
	//		net.registerListener(SEND_FILE_DESCR, this);
	//	}


	//	/**
	//	 * visit our chunk change, do nothing?
	//	 */
	//	@Override
	//		public void visit(FsChunk chunk) {

	//	}

	//};
}
