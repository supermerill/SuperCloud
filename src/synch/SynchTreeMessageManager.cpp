#include "SynchTreeMessageManager.hpp"

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsObject.hpp"
#include "fs/base/FsDirectory.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"
#include "network/Peer.hpp"

#include <map>
#include <unordered_set>

namespace supercloud {


	void SynchTreeMessageManager::register_listener() {
		m_cluster_manager->registerListener(*SynchMessagetype::GET_STATE, this->ptr()); //TODO
		m_cluster_manager->registerListener(*SynchMessagetype::SEND_STATE, this->ptr()); //TODO
		m_cluster_manager->registerListener(*SynchMessagetype::GET_TREE, this->ptr());
		m_cluster_manager->registerListener(*SynchMessagetype::SEND_TREE, this->ptr());
		m_cluster_manager->registerListener(*SynchMessagetype::GET_HOST_PROPERTIES, this->ptr()); //TODO
		m_cluster_manager->registerListener(*SynchMessagetype::SEND_HOST_PROPERTIES, this->ptr()); //TODO
		// fetch each minute
		m_cluster_manager->registerListener(*UnnencryptedMessageType::TIMER_MINUTE, this->ptr()); //TODO
	}

	void SynchTreeMessageManager::receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) {
		if (sender->getComputerId() == 0 || sender->getComputerId() == NO_COMPUTER_ID) {
			//error: not a estabished peer
			error(std::string("Error, peer ") + std::to_string(sender->getPeerId() % 100) + " ask us a chunk and he doens't have a computerid !");
			return;
		}
		if (message_id == *SynchMessagetype::GET_TREE) {
			log(std::to_string(m_cluster_manager->getComputerId()) + "$ RECEIVE GET_CHUNK_AVAILABILITY from " + sender->getPeerId());
			TreeAnswer answer = answerTreeRequest(sender, readTreeRequestMessage(message));
			sender->writeMessage(*SynchMessagetype::SEND_TREE, writeTreeAnswerMessage(answer));
		} else if (message_id == *SynchMessagetype::SEND_TREE) {
			log(std::to_string(m_cluster_manager->getComputerId()) + "$ RECEIVE SEND_CHUNK_AVAILABILITY from " + sender->getPeerId());
			//update our availability
			useTreeRequestAnswer(sender, readTreeAnswerMessage(message));
		} else if (message_id == *SynchMessagetype::GET_HOST_PROPERTIES) {
			log(std::to_string(m_cluster_manager->getComputerId()) + "$ RECEIVE GET_HOST_PROPERTIES from " + sender->getPeerId());
			//TODO
		} else if (message_id == *SynchMessagetype::SEND_HOST_PROPERTIES) {
			log(std::to_string(m_cluster_manager->getComputerId()) + "$ RECEIVE SEND_HOST_PROPERTIES from " + sender->getPeerId());
			//TODO
		}
	}

	ByteBuff SynchTreeMessageManager::writeTreeRequestMessage(const TreeRequest& request) {
		ByteBuff buff;
		buff.putSize(request.depth);
		buff.putULong(request.last_commit_received);
		buff.putLong(request.last_fetch_time);
		buff.putSize(request.roots.size());
		for (const FsID& id : request.roots) {
			buff.putULong(id);
		}
		return buff;
	}

	SynchTreeMessageManager::TreeRequest SynchTreeMessageManager::readTreeRequestMessage(const ByteBuff& buffer) {
		TreeRequest request;
		request.depth = buffer.getSize();
		request.last_commit_received = buffer.getULong();
		request.last_fetch_time = buffer.getLong();
		size_t nb_roots = buffer.getSize();
		for (size_t i = 0; i < nb_roots; ++i) {
			request.roots.push_back(buffer.getULong());
		}
		return request;
	}

	ByteBuff SynchTreeMessageManager::writeTreeAnswerMessage(const TreeAnswer& answer) {
		ByteBuff buffer;
		buffer.serializeComputerId(answer.from);
		buffer.putLong(answer.answer_time);
		buffer.putSize(answer.created.size());
		for (const FsObjectTreeAnswerPtr& added : answer.created) {
			assert(added);
			buffer.putULong(added->getId());
			buffer.putSize(added->getDepth());
			buffer.putSize(added->size());
			buffer.putLong(added->getDate());
			buffer.putUTF8(added->getName());
			buffer.putUShort(added->getCUGA());
			buffer.putULong(added->getParent());
			buffer.putUInt(added->getGroupId());
			if (added->getCommitsSize() > 0) {
				buffer.putULong(added->getCommit(added->getCommitsSize() - 1).id);
				buffer.putLong(added->getCommit(added->getCommitsSize() - 1).date);
			} else {
				buffer.putULong(0);
				buffer.putLong(0);
			}
			buffer.putSize(added->getCurrent().size());
			for (const FsID& id : added->getCurrent()) {
				buffer.putULong(id);
			}
		}
		buffer.putSize(answer.modified.size());
		for (const TreeAnswerEltChange& changed : answer.modified) {
			buffer.putULong(changed.elt_id);
			buffer.putSize(changed.elt_depth);
			buffer.putSize(changed.elt_size);
			buffer.putULong(changed.last_commit_id);
			buffer.putLong(changed.last_commit_time);
			buffer.putSize(changed.state.size());
			for (const FsID& id : changed.state) {
				buffer.putULong(id);
			}
		}
		buffer.putSize(answer.deleted.size());
		for (const TreeAnswerEltDeleted& deleted : answer.deleted) {
			buffer.putULong(deleted.elt_id);
			buffer.putSize(deleted.elt_depth);
			buffer.putSize(deleted.elt_size);
			buffer.putULong(deleted.last_commit_id);
			buffer.putLong(deleted.last_commit_time);
			buffer.putULong(deleted.renamed_to);
		}
		return buffer;
	}
	SynchTreeMessageManager::TreeAnswer SynchTreeMessageManager::readTreeAnswerMessage(const ByteBuff& buffer) {
		TreeAnswer answer;
		answer.from = buffer.deserializeComputerId();
		answer.answer_time = buffer.getLong();
		size_t created_size = buffer.getSize();
		for (size_t i = 0; i < created_size; ++i) {
			FsID added_Id = buffer.getULong();
			uint16_t added_Depth = uint16_t(buffer.getSize());
			size_t added_size = buffer.getSize();
			DateTime added_Date = buffer.getLong();
			std::string added_Name = buffer.getUTF8();
			CUGA added_CUGA = buffer.getUShort();
			FsID added_Parent = buffer.getULong();
			uint32_t added_GroupId = buffer.getUInt();
			FsID commit_id = buffer.getULong();
			DateTime commit_date = buffer.getLong();
			size_t added_Current_size = buffer.getSize();
			std::vector<FsID> current;
			for (size_t curr_idx = 0; curr_idx < added_Current_size; ++curr_idx) {
				current.push_back(buffer.getULong());
			}
			answer.created.push_back(FsObjectTreeAnswerPtr{ new FsObjectTreeAnswer{ added_Id , added_Depth , added_size, added_Date , added_Name , added_CUGA , added_Parent , added_GroupId, current } });
			if (commit_id != 0) {
				answer.created.back()->setCommit(commit_id, commit_date);
			}
		}
		size_t modified_size = buffer.getSize();
		for (size_t i = 0; i < modified_size; ++i) {
			answer.modified.emplace_back();
			answer.modified.back().elt_id = buffer.getULong();
			answer.modified.back().elt_depth = uint16_t(buffer.getSize());
			answer.modified.back().elt_size = buffer.getSize();
			answer.modified.back().last_commit_id = buffer.getULong();
			answer.modified.back().last_commit_time = buffer.getLong();
			size_t state_size = buffer.getSize();
			std::vector<FsID>& current = answer.modified.back().state;
			for (size_t curr_idx = 0; curr_idx < state_size; ++curr_idx) {
				current.push_back(buffer.getULong());
			}
		}
		size_t deleted_size = buffer.getSize();
		for (size_t i = 0; i < deleted_size; ++i) {
			answer.deleted.emplace_back();
			answer.deleted.back().elt_id = buffer.getULong();
			answer.deleted.back().elt_depth = uint16_t(buffer.getSize());
			answer.deleted.back().elt_size = buffer.getSize();
			answer.deleted.back().last_commit_id = buffer.getULong();
			answer.deleted.back().last_commit_time = buffer.getLong();
			answer.deleted.back().renamed_to = buffer.getULong();
		}
		return answer;
	}

	void SynchTreeMessageManager::fillTreeAnswer(TreeAnswer& answer, FsID elt_id, size_t depth, DateTime since) {
		FsEltPtr elt = m_filesystem->load(elt_id);
		if (elt) {
			//ignore chunks
			if (FsElt::isObject(elt_id)) {
				FsObjectPtr object = FsElt::toObject(elt);
				//is it created, modified or deleted?
				size_t commit_size = object->getCommitsSize();
				if (object->getDate() > since || (commit_size > 0 && object->getCommit(0).date > since) ) {
					// the object has been created after the last fetch, or we received the first (creation) commit after the last fetch
					//created
					answer.created.push_back(FsObjectTreeAnswerPtr{ new FsObjectTreeAnswer{ elt_id , object->getDepth() , object->size(), object->getDate() , object->getName() ,
						object->getCUGA() , object->getParent() , object->getGroupId(), object->getCurrent() } });
					if (object->getCommitsSize() > 0) {
						answer.created.back()->setCommit(object->getCommit(object->getCommitsSize() - 1).id, object->getCommit(object->getCommitsSize() - 1).date);
					}
				} else if (object->getDeletedDate() != 0 && (object->getDeletedDate() > since || (commit_size > 0 && object->getCommit(commit_size-1).date > since))) {
					// the object has been deleted, and :
					//	. it's a deletion that occured since the last fetch
					//  . we were notified of it since the last fetch
					//deleted
					answer.deleted.emplace_back();
					answer.deleted.back().elt_id = elt_id;
					answer.deleted.back().elt_depth = object->getDepth(); //? it is deleted... meh, whatever.
					answer.deleted.back().elt_size = object->size();
					answer.deleted.back().last_commit_id = object->getCommitsSize() == 0 ? 0 : object->getCommit(object->getCommitsSize()-1).id;
					answer.deleted.back().last_commit_time = object->getCommitsSize() == 0 ? 0 : object->getCommit(object->getCommitsSize() - 1).date;
					answer.deleted.back().renamed_to = object->getRenamedTo();
				} else if(commit_size > 0 && object->getCommit(commit_size - 1).date > since) {
					// the object has been modified since the last fetch
					//modified
					answer.modified.emplace_back();
					answer.modified.back().elt_id = elt_id;
					answer.modified.back().elt_depth = object->getDepth();
					answer.modified.back().elt_size = object->size();
					answer.modified.back().last_commit_id = object->getCommitsSize() == 0 ? 0 : object->getCommit(object->getCommitsSize() - 1).id;
					answer.modified.back().last_commit_time = object->getCommitsSize() == 0 ? 0 : object->getCommit(object->getCommitsSize() - 1).date;
					answer.modified.back().state = object->getCurrent();
				}
				//if directory, and depth not 0, and there is a modification insde since he last fetch, go deeper
				if (FsElt::isDirectory(elt_id) && depth > 0) {
					FsDirPtr dir = FsElt::toDirectory(elt);
					auto [commit_id, commit_time] = dir->getLastModification();
					if (commit_time > since) {
						for (const FsID& id : object->getCurrent()) {
							fillTreeAnswer(answer, id, depth - 1, since);
						}
					}
				}
			}
		} else {
			error(std::string("error, a peer requested a tree from unknown id?"));
			// Assume deleted? just ignore?
		}
	}

	SynchTreeMessageManager::TreeAnswer SynchTreeMessageManager::answerTreeRequest(const PeerPtr sender, TreeRequest&& request) {
		TreeAnswer answer;
		answer.from = m_cluster_manager->getComputerId();
		answer.answer_time = m_cluster_manager->getCurrentTime();
		//get the ids
		for (const FsID root_id : request.roots) {
			fillTreeAnswer(answer, root_id, request.depth, request.last_fetch_time);
		}
		return answer;
	}

	void SynchTreeMessageManager::useTreeRequestAnswer(const PeerPtr sender, TreeAnswer&& answer) {
		std::lock_guard lock{ m_mutex_incomplete_requests };
		//fusion the result with currently waiting answers?
		if (auto it = m_incomplete_requests.find(sender->getComputerId()); it != m_incomplete_requests.end()) {
			//add it into answer
			answer.modified.insert(answer.modified.end(), it->second.modified.begin(), it->second.modified.end());
			answer.created.insert(answer.created.end(), it->second.created.begin(), it->second.created.end());
			answer.deleted.insert(answer.deleted.end(), it->second.deleted.begin(), it->second.deleted.end());
			//remove it from the map
			m_incomplete_requests.erase(it);
		}

		//remove change/create if they are deleted (shouldn't be transmitted, but it's better to check)
		//TODO test
		std::unordered_set<FsID> create_change;
		for (const FsObjectTreeAnswerPtr& obj : answer.created) {
			assert(obj);
			create_change.insert(obj->getId());
		}
		for (const TreeAnswerEltChange& obj : answer.modified) {
			create_change.insert(obj.elt_id);
		}
		for (const TreeAnswerEltDeleted& del_obj : answer.deleted) {
			if (create_change.find(del_obj.elt_id) != create_change.end()) {
				foreach(it_obj, answer.created) {
					if (del_obj.elt_id == (*it_obj)->getId()) {
						it_obj.erase();
					}
				}
				foreach(it_obj, answer.modified) {
					if (del_obj.elt_id == it_obj->elt_id) {
						it_obj.erase();
					}
				}
				create_change.erase(del_obj.elt_id);
			}
		}

		//for each id, check that we have already each "changed" object inside our fs. (but for chunks, chunks can be missed)
		// TODO: test
		//maybe it's better to just get all information for all of them, just in case?
		std::unordered_set<FsID> unkown_ids;
		for (const FsObjectTreeAnswerPtr& obj : answer.created) {
			assert(obj);
			if (FsElt::isDirectory(obj->getId())) {
				for (const FsID& id : obj->getCurrent()) {
					unkown_ids.insert(id);
				}
			}
		}
		for (const TreeAnswerEltChange& obj : answer.modified) {
			if (FsElt::isDirectory(obj.elt_id)) {
				for (const FsID& id : obj.state) {
					unkown_ids.insert(id);
				}
			}
		}
		for (const FsObjectTreeAnswerPtr& obj : answer.created) {
			assert(obj);
			unkown_ids.erase(obj->getId());
		}
		if (unkown_ids.size() > 0) {
			//for each id in the current state, check if it's inside our fs.
				//if not, send another request about it
			TreeRequest request;
			// for each of them, send a request to get it. (date=0 to be sure it answer with a create answer)
			//SynchState last_synch = m_syncro->getSynchState(sender->getComputerId());
			//request.last_commit_received = last_synch.last_commit_received_date;
			//request.last_fetch_time = last_synch.last_fetch_date;
			request.last_commit_received = 0;
			request.last_fetch_time = 0;
			request.depth = 0;
			for (const TreeAnswerEltChange& obj : answer.modified) {
				if (unkown_ids.erase(obj.elt_id) && !m_filesystem->hasLocally(obj.elt_id)) {
					request.roots.push_back(obj.elt_id);
				}
			}
			for (const FsID& id : unkown_ids) {
				if (!m_filesystem->hasLocally(id)) {
					request.roots.push_back(id);
				}
			}

			m_incomplete_requests[sender->getComputerId()] = answer;
			sender->writeMessage(*SynchMessagetype::GET_TREE, this->writeTreeRequestMessage(request));
		} else {
			//the changes are "complete"
			// have to ordered the commit items, by depth in the filesystem.
			std::vector<FsObjectTreeAnswer> stub_storage;
			std::map<uint16_t, const FsObjectTreeAnswer*> ordered_stubs;
			std::unordered_map<FsID, const FsElt*> db_stubs;
			std::lock_guard lock{ m_filesystem->synchronize() };
			for (FsObjectTreeAnswerPtr& obj : answer.created) {
				assert(obj);
				ordered_stubs[obj->getDepth()] = obj.get();
				db_stubs[obj->getId()] = obj.get();
			}
			for (const TreeAnswerEltChange& obj : answer.modified) {
				stub_storage.emplace_back(obj.elt_id, obj.elt_depth, obj.elt_size, obj.state);
				stub_storage.back().setCommit(obj.last_commit_id, obj.last_commit_time);
				ordered_stubs[obj.elt_depth] = &stub_storage.back();
				db_stubs[obj.elt_id] = &stub_storage.back();
			}
			for (const TreeAnswerEltDeleted& obj : answer.deleted) {
				stub_storage.emplace_back(obj.elt_id, obj.elt_depth, obj.elt_size, std::vector<FsID>{});
				stub_storage.back().setCommit(obj.last_commit_id, obj.last_commit_time)
					.setDeleted(obj.renamed_to, obj.last_commit_time);
				ordered_stubs[obj.elt_depth] = &stub_storage.back();
				db_stubs[obj.elt_id] = &stub_storage.back();
			}
			//update our fs with these
			for (const auto& depth2stub : ordered_stubs) {
				m_syncro->mergeCommit(*depth2stub.second, db_stubs);
			}
#ifndef NDEBUG
			// assert that the resulted filesystem is safe
			m_filesystem->checkFilesystem();
#endif
		}
	}
}
