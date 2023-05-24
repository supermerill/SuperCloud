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
		assert(m_network->test_getListener(*SynchMessagetype::GET_TREE).size() == 0);
		m_network->registerListener(*SynchMessagetype::GET_STATE, this->ptr()); //TODO
		m_network->registerListener(*SynchMessagetype::SEND_STATE, this->ptr()); //TODO
		m_network->registerListener(*SynchMessagetype::GET_TREE, this->ptr());
		m_network->registerListener(*SynchMessagetype::SEND_TREE, this->ptr());
		m_network->registerListener(*SynchMessagetype::GET_HOST_PROPERTIES, this->ptr()); //TODO
		m_network->registerListener(*SynchMessagetype::SEND_HOST_PROPERTIES, this->ptr()); //TODO
		m_network->registerListener(*SynchMessagetype::SEND_INVALIDATE_ELT, this->ptr());
		// abandon request that aren't answered since 1-2 seconds
		m_network->registerListener(*UnnencryptedMessageType::TIMER_SECOND, this->ptr());
		// cleaning
		m_network->registerListener(*UnnencryptedMessageType::TIMER_MINUTE, this->ptr()); //TODO
	}

	void SynchTreeMessageManager::receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) {
		if (sender->getComputerId() == 0 || sender->getComputerId() == NO_COMPUTER_ID) {
			//error: not a estabished peer
			error(std::string("Error, peer ") + std::to_string(sender->getPeerId() % 100) + " ask us a chunk and he doens't have a computerid !");
			return;
		}
		if (message_id == *SynchMessagetype::GET_TREE) {
			log(std::to_string(m_network->getComputerId()) + "("+(m_network->getPeerId()%100)+") RECEIVE GET_TREE from " + (sender->getPeerId()%100));
			//FIXME: check m_syncro->isInvalidated, and ask for update before answering if it's the case.
			checkAndAnswerTreeRequest(sender, readTreeRequestMessage(message));
		} else if (message_id == *SynchMessagetype::SEND_TREE) {
			log(std::to_string(m_network->getComputerId()) + "(" + (m_network->getPeerId() % 100) + ") RECEIVE SEND_TREE from " + (sender->getPeerId()%100));
			//update our availability
			useTreeRequestAnswer(sender, readTreeAnswerMessage(message));
		} else if (message_id == *SynchMessagetype::GET_HOST_PROPERTIES) {
			log(std::to_string(m_network->getComputerId()) + "(" + (m_network->getPeerId() % 100) + ") RECEIVE GET_HOST_PROPERTIES from " + (sender->getPeerId()%100));
			//TODO
		} else if (message_id == *SynchMessagetype::SEND_HOST_PROPERTIES) {
			log(std::to_string(m_network->getComputerId()) + "(" + (m_network->getPeerId() % 100) + ") RECEIVE SEND_HOST_PROPERTIES from " + (sender->getPeerId()%100));
			//TODO
		} else if (message_id == *SynchMessagetype::SEND_INVALIDATE_ELT) {
			log(std::to_string(m_network->getComputerId()) + "(" + (m_network->getPeerId() % 100) + ") RECEIVE SEND_INVALIDATE_ELT from " + (sender->getPeerId()%100));
			std::vector<InvalidateElementsMessage> decoded_msgs = readInvalidateEltsMessage(message);
			for (const InvalidateElementsMessage& decoded_msg : decoded_msgs) {
				useInvalidateEltsAnswer(sender, decoded_msg);
			}
		} else if (message_id == *UnnencryptedMessageType::TIMER_SECOND) {
			//check if some request are pending and have to be abandoned.
			{std::lock_guard lock{ m_mutex_incomplete_requests };
				DateTime current_time = m_network->getCurrentTime();
				std::vector<uint64_t> old_request;
				for (auto& id2_req : m_incomplete_requests) {
					if (id2_req.second.start + MAX_MILISECONDS_FOR_MSG_ABANDON < current_time) {
						// abandon it : set it as finished and call the callbacks.
						id2_req.second.finished = true;
						id2_req.second.finished_since = current_time;
						log(std::string("answer incomplete tree update ") + (m_network->getPeerId() % 100));
						foreach(callback_it, id2_req.second.callbacks) {
							callback_it.get()(&id2_req.second.our_answer);
							callback_it.erase();
						}
					}
				}
			}
		} else if (message_id == *UnnencryptedMessageType::TIMER_MINUTE) {
			log(std::to_string(m_network->getComputerId()) + "(" + (m_network->getPeerId() % 100) + ") RECEIVE TIMER_MINUTE from " + (sender->getPeerId()%100));
			//remove very old (>2min) & completed requests from m_incomplete_requests
			{std::lock_guard lock{ m_mutex_incomplete_requests };
				std::vector<uint64_t> old_request;
				for (auto& id2_req : m_incomplete_requests) {
					if (id2_req.second.finished) {
						old_request.push_back(id2_req.first);
					}
				}
				for (uint64_t id : old_request) {
					m_incomplete_requests.erase(id);
				}
			}
			//TODO more update thingy
		}
	}


	void SynchTreeMessageManager::emitModificationsNotification(std::unordered_map<FsID, Invalidation> modifiedid_2_invalidation) {
		//create an InvalidateElementsMessage per modifier. not mandatory but it's maybe useful for the 'last_invalidation_time'
		//if you can remove it, remove this shuffle and only create one InvalidateElementsMessage
		struct FsIDs_Invalidations { std::vector<FsID> ids; std::vector<FsCommitID> commits; };
		std::unordered_map<ComputerId, FsIDs_Invalidations> modifier_2_modified;
		for (const auto& entry : modifiedid_2_invalidation) {
			FsIDs_Invalidations& id2inva = modifier_2_modified[FsElt::getComputerId(entry.second.commit)];
			id2inva.ids.push_back(entry.first);
			id2inva.commits.push_back(entry.second.commit);
		}
		DateTime current_time = m_network->getCurrentTime();
		for (PeerPtr peer : m_network->getPeersCopy()) {
			ComputerId send_to = peer->getComputerId();
			//Create InvalidateElementsMessage
			std::vector<InvalidateElementsMessage> msgs;
			for (const auto& entry : modifier_2_modified) {
				msgs.emplace_back();
				msgs.back().modified = entry.second.ids;
				msgs.back().commits = entry.second.commits;
				msgs.back().last_invalidation_time = current_time;
				if (SynchState state = m_syncro->getSynchState(entry.first); state) {
					msgs.back().last_invalidation_time = state.last_modification_date;
				}
				foreach(idptr, msgs.back().modified) {
					assert(entry.first == FsElt::getComputerId(modifiedid_2_invalidation[*idptr].commit));
					if (modifiedid_2_invalidation[*idptr].notifier == send_to) idptr.erase();
					else if (entry.first == send_to) idptr.erase();
				}
				if (msgs.back().modified.empty()) {
					msgs.erase(msgs.end() - 1);
				}
			}
			//create buffer
			ByteBuff buffer = writeInvalidateEltsMessage(msgs);
#ifdef _DEBUG
			auto cids = this->m_syncro->getComputerIds();
			cids.insert(m_network->getComputerId());
			for (auto& msg : msgs) {
				for (FsID commit : msg.commits) {
					assert(cids.find(FsElt::getComputerId(commit)) != cids.end());
				}
			}
#endif
			//emit to all connected peers
			peer->writeMessage(*SynchMessagetype::SEND_INVALIDATE_ELT, buffer.rewind());
		}
	}
	ByteBuff SynchTreeMessageManager::writeInvalidateEltsMessage(const std::vector<InvalidateElementsMessage>& answers) {
		ByteBuff buffer;
		buffer.putSize(answers.size());
		for (const InvalidateElementsMessage& elt : answers) {
			buffer.putLong(elt.last_invalidation_time);
			buffer.putSize(elt.modified.size());
			assert(elt.modified.size() == elt.commits.size());
			for (size_t idx = 0; idx < elt.modified.size(); idx++) {
				buffer.putULong(elt.modified[idx]);
				buffer.putULong(elt.commits[idx]);
				assert(elt.commits[idx] != 0);
			}
		}
		return buffer.flip();
	}
	std::vector<SynchTreeMessageManager::InvalidateElementsMessage> SynchTreeMessageManager::readInvalidateEltsMessage(const ByteBuff& buffer) {
		std::vector<InvalidateElementsMessage> msgs;
		size_t nb_msg = buffer.getSize();
		for (size_t i_msg = 0; i_msg < nb_msg; ++i_msg) {
			InvalidateElementsMessage& msg = msgs.emplace_back();
			msg.last_invalidation_time = buffer.getLong();
			size_t nb_roots = buffer.getSize();
			for (size_t i_id = 0; i_id < nb_roots; ++i_id) {
				msg.modified.push_back(buffer.getULong());
				msg.commits.push_back(buffer.getULong());
			}
		}
		return msgs;

	}
	void SynchTreeMessageManager::useInvalidateEltsAnswer(const PeerPtr sender, const InvalidateElementsMessage& answer) {
		assert(answer.modified.size() == answer.commits.size());
		if (!answer.commits.empty()) {
			m_syncro->addInvalidatedElements(answer.modified, answer.commits, sender->getComputerId(), FsElt::getComputerId(answer.commits.front()) , answer.last_invalidation_time);
		}
	}


	SynchTreeMessageManager::TreeAnswerRequest& SynchTreeMessageManager::registerRequest(TreeRequest& unregistered_request, ComputerId cid) {
		assert(unregistered_request.request_id == 0);
		//write the request
		unregistered_request.request_id = FsElt::createId(FsType::NONE, m_request_id_generator.fetch_add(1), cid);
		TreeAnswerRequest& saved_request = m_incomplete_requests[unregistered_request.request_id];
		saved_request.request_to = cid;
		saved_request.request_id = unregistered_request.request_id;
		saved_request.our_answer.from = cid;
		saved_request.our_answer.request_id = unregistered_request.request_id;
		saved_request.start = m_network->getCurrentTime();
		return saved_request;
	}

	std::future<SynchTreeMessageManager::TreeAnswerPtr> SynchTreeMessageManager::fetchTree(FsID root, ComputerId cid) {
		std::shared_ptr<std::promise<TreeAnswerPtr>> notify_tree_fetched{ new std::promise<TreeAnswerPtr> {} };
		std::future<TreeAnswerPtr> future = notify_tree_fetched->get_future();

		fetchTree(root, [notify_tree_fetched](TreeAnswerPtr answer) {notify_tree_fetched->set_value(answer); }, cid);

		return future;
	}

	void SynchTreeMessageManager::fetchTree(FsID root, const std::function<void(TreeAnswerPtr)>& callback_caller, ComputerId enforced_cid) {
		//choose which peers/cid is useful to fetch
		//TODO (stop-gap: broadcast)
		PeerList peers = m_network->getPeersCopy();
		std::unordered_set<ComputerId> already_seen;
		foreach(it, peers) {
			if (!(*it)->isConnected() || already_seen.find((*it)->getComputerId()) != already_seen.end()) {
				it.erase();
			} else if (enforced_cid != NO_COMPUTER_ID && (*it)->getComputerId() != enforced_cid) {
				it.erase();
			} else {
				already_seen.insert((*it)->getComputerId());
			}
		}
		if (!peers.empty()) {
			std::lock_guard lock{ m_mutex_incomplete_requests };
			std::shared_ptr<TreeAnswer> full_answer = std::make_shared<TreeAnswer>();
			std::shared_ptr<std::unordered_set<ComputerId>> all_peers = std::make_shared<std::unordered_set<ComputerId>>();
			for (PeerPtr& peer : peers) {
				all_peers->insert(peer->getComputerId());
			}
			auto callback_wait_all = [this, root, full_answer, all_peers, callback_caller](TreeAnswer* answer) {
				if (!all_peers->empty()) {
					all_peers->erase(answer->from);
					if (full_answer->answer_time < answer->answer_time) {
						full_answer->answer_time = std::max(full_answer->answer_time, answer->answer_time);
						full_answer->modified.insert(full_answer->modified.end(), answer->modified.begin(), answer->modified.end());
						full_answer->created.insert(full_answer->created.end(), answer->created.begin(), answer->created.end());
						full_answer->deleted.insert(full_answer->deleted.end(), answer->deleted.begin(), answer->deleted.end());
					}
					if (all_peers->empty()) {
						//update invalidation if we couldn't get the request (TODO: maybe find a better spot for that? this one is convenient as it ensure we have hexausted our options)
						// it's alredy merged, so it should be validated anyway. so just ensure it is not invalidated anymore.
						this->ensureNotInvalidated(root);
						callback_caller(full_answer);
					}
				}
			};
			for (PeerPtr& peer : peers) {
				//create request
				TreeRequest request;
				request.roots.push_back(root);
				request.depth = uint16_t(-1);
				SynchState synch_state = m_syncro->getSynchState(peer->getComputerId());
				if (synch_state && synch_state.last_commit_date > 0) {
					request.last_commit_received = synch_state.last_commit;
					request.last_fetch_time = synch_state.last_commit_received_date;
					assert(synch_state.last_commit_date > 0);
					assert(synch_state.last_commit_received_date > 0);
				} else {
					//no data on this cid: get all
					request.last_commit_received = 0;
					request.last_fetch_time = 0;
				}
				//create request 'waiter'
				TreeAnswerRequest& saved_request = registerRequest(request, peer->getComputerId());
				saved_request.roots.push_back(root);
				saved_request.callbacks.push_back(callback_wait_all);
				//send request
				ByteBuff buff = writeTreeRequestMessage(request);
				log(std::string("TREE REQUEST (fetch tree): ") + (m_network->getPeerId() % 100) + " ask " + (peer->getPeerId() % 100) + " for a request (fetch tree)");
				peer->writeMessage(*SynchMessagetype::GET_TREE, buff);// writeTreeRequestMessage(request));
			}
		}
	}

	void SynchTreeMessageManager::ensureNotInvalidated(const FsID root) {
		this->m_syncro->removeInvalidatedElements({ root });
	}

	ByteBuff SynchTreeMessageManager::writeTreeRequestMessage(const TreeRequest& request) {
#ifdef _DEBUG
		std::string st;
		for (const FsID& id : request.roots) {
			st += ", " + std::to_string(id%1000);
		}
		log(std::to_string(m_network->getPeerId()%100) + std::string(" Request docs {") + st + "}");
		log(std::to_string(m_network->getPeerId() % 100) + std::string(" Request since") + request.last_fetch_time);
		if (request.last_fetch_time == 0) {
			int i = 0;
		}
#endif
		assert(request.request_id);
		ByteBuff buff;
		buff.putULong(request.request_id);
		buff.putSize(request.depth);
		buff.putULong(request.last_commit_received);
		buff.putLong(request.last_fetch_time);
		buff.putSize(request.roots.size());
		for (const FsID& id : request.roots) {
			buff.putULong(id);
		}
		return buff.flip();
	}

	SynchTreeMessageManager::TreeRequest SynchTreeMessageManager::readTreeRequestMessage(const ByteBuff& buffer) {
		TreeRequest request;
		request.request_id = buffer.getULong();
		request.depth = buffer.getSize();
		request.last_commit_received = buffer.getULong();
		request.last_fetch_time = buffer.getLong();
		size_t nb_roots = buffer.getSize();
		for (size_t i = 0; i < nb_roots; ++i) {
			request.roots.push_back(buffer.getULong());
		}

#ifdef _DEBUG
		std::string st;
		for (const FsID& id : request.roots) {
			st += ", " + std::to_string(id % 1000);
		}
		log(std::to_string(m_network->getPeerId() % 100) + std::string(" Receive Request for docs {") + st + "}");
#endif
		return request;
	}

	ByteBuff SynchTreeMessageManager::writeTreeAnswerMessage(const TreeAnswer& answer) {
#ifdef _DEBUG
		std::string stC;
		for (auto& obj : answer.created) {
			stC += ", " + std::to_string(obj->getId() % 1000);
		}
		std::string stM;
		for (auto& obj : answer.modified) {
			stM += ", " + std::to_string(obj.elt_id % 1000);
		}
		std::string stD;
		for (auto& obj : answer.deleted) {
			stD += ", " + std::to_string(obj.elt_id % 1000);
		}
		log(std::to_string(m_network->getPeerId() % 100) + std::string(" Send objects {") + stC + "}, {" + stM + "}, {" + stD + "}");

		//not possible to create & delete at the same time
		//or to create & modify: just create 
		std::unordered_set<FsID> already_inside;
		for (auto& obj : answer.created) {
			assert(already_inside.find(obj->getId()) == already_inside.end());
			already_inside.insert(obj->getId());
		}
		for (auto& obj : answer.modified) {
			assert(already_inside.find(obj.elt_id) == already_inside.end());
			already_inside.insert(obj.elt_id);
		}
		for (auto& obj : answer.deleted) {
			assert(already_inside.find(obj.elt_id) == already_inside.end());
			already_inside.insert(obj.elt_id);
		}
		for (auto& obj_id : answer.unchanged) {
			assert(already_inside.find(obj_id) == already_inside.end());
		}

#endif
		ByteBuff buffer;
		buffer.putULong(answer.request_id);
		buffer.serializeComputerId(answer.from);
		buffer.putLong(answer.answer_time);
		buffer.putSize(answer.created.size());
		for (const FsObjectTreeAnswerPtr& added : answer.created) {
			assert(added);
			buffer.putULong(added->getId());
			buffer.putSize(added->getDepth());
			buffer.putLong(added->getCreationTime());
			buffer.putUTF8(added->getName());
			buffer.putUShort(added->getCUGA());
			buffer.putUInt(added->getGroupId());
			buffer.putULong(added->getParent());
			// it's possible to have a commit in an empty obj: it get data and then it's cleared (and intermediate commits may have been erased).
			// it's not possible to have data withotu a commit
			assert(added->getCurrent().empty() || !added->getCommits().empty());
			assert(added->sizes().size() == added->getCurrent().size());
			if (!added->getCommits().empty()) {
				buffer.putULong(added->getCommits().back().id);
				buffer.putLong(added->getCommits().back().time);
			} else {
				buffer.putULong(0);
				buffer.putLong(0);
			}
			const size_t curr_size = added->getCurrent().size();
			buffer.putSize(curr_size);
			assert(added->sizes().size() == curr_size);
			for (size_t i = 0; i < curr_size; ++i) {
				buffer.putULong(added->getCurrent()[i]);
				buffer.putSize(added->sizes()[i]);
			}
		}
		buffer.putSize(answer.modified.size());
		for (const TreeAnswerEltChange& changed : answer.modified) {
			buffer.putULong(changed.elt_id);
			buffer.putSize(changed.elt_depth);
			buffer.putSize(changed.elt_size);
			buffer.putULong(changed.last_commit_id);
			buffer.putLong(changed.last_commit_time);
			const size_t curr_size = changed.state.size();
			buffer.putSize(curr_size);
			assert(changed.sizes.size() == curr_size);
			for (size_t i = 0; i < curr_size; ++i) {
				buffer.putULong(changed.state[i]);
				buffer.putSize(changed.sizes[i]);
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
		buffer.putSize(answer.unchanged.size());
		for (FsID elt_id : answer.unchanged) {
			buffer.putULong(elt_id);
		}
		return buffer.flip();
	}
	SynchTreeMessageManager::TreeAnswer SynchTreeMessageManager::readTreeAnswerMessage(const ByteBuff& buffer) {
		TreeAnswer answer;
		answer.request_id = buffer.getULong();
		answer.from = buffer.deserializeComputerId();
		answer.answer_time = buffer.getLong();
		const size_t created_size = buffer.getSize();
		for (size_t i = 0; i < created_size; ++i) {
			FsID added_Id = buffer.getULong();
			uint16_t added_Depth = uint16_t(buffer.getSize());
			DateTime added_Date = buffer.getLong();
			std::string added_Name = buffer.getUTF8();
			CUGA added_CUGA = buffer.getUShort();
			uint32_t added_GroupId = buffer.getUInt();
			FsID added_Parent = buffer.getULong();
			FsID commit_id = buffer.getULong();
			DateTime commit_date = buffer.getLong();
			size_t added_Current_size = buffer.getSize();
			std::vector<FsID> current;
			std::vector<size_t> current_size;
			for (size_t curr_idx = 0; curr_idx < added_Current_size; ++curr_idx) {
				current.push_back(buffer.getULong());
				current_size.push_back(buffer.getSize());
			}
			answer.created.push_back(FsObjectTreeAnswerPtr{ new FsObjectTreeAnswer{ 
				added_Id , added_Depth, added_Date , added_Name , added_CUGA , added_GroupId, added_Parent} });
			if (commit_id) {
				answer.created.back()->setCommit(current, commit_id, commit_date, current_size);
			} else {
				assert(current.empty());
				assert(current_size.empty());
			}
		}
		const size_t modified_size = buffer.getSize();
		for (size_t i = 0; i < modified_size; ++i) {
			answer.modified.emplace_back();
			answer.modified.back().elt_id = buffer.getULong();
			answer.modified.back().elt_depth = uint16_t(buffer.getSize());
			answer.modified.back().elt_size = buffer.getSize();
			answer.modified.back().last_commit_id = buffer.getULong();
			answer.modified.back().last_commit_time = buffer.getLong();
			size_t state_size = buffer.getSize();
			std::vector<FsID>& current = answer.modified.back().state;
			std::vector<size_t>& current_sizes = answer.modified.back().sizes;
			for (size_t curr_idx = 0; curr_idx < state_size; ++curr_idx) {
				current.push_back(buffer.getULong());
				current_sizes.push_back(buffer.getSize());
			}
		}
		const size_t deleted_size = buffer.getSize();
		for (size_t i = 0; i < deleted_size; ++i) {
			answer.deleted.emplace_back();
			answer.deleted.back().elt_id = buffer.getULong();
			answer.deleted.back().elt_depth = uint16_t(buffer.getSize());
			answer.deleted.back().elt_size = buffer.getSize();
			answer.deleted.back().last_commit_id = buffer.getULong();
			answer.deleted.back().last_commit_time = buffer.getLong();
			answer.deleted.back().renamed_to = buffer.getULong();
		}
		const size_t unchanged_size = buffer.getSize();
		for (size_t i = 0; i < unchanged_size; ++i) {
			answer.unchanged.push_back(buffer.getULong());
		}
#ifdef _DEBUG
		std::string stC;
		for (auto& obj : answer.created) {
			stC += ", " + std::to_string(obj->getId() % 1000);
		}
		std::string stM;
		for (auto& obj : answer.modified) {
			stM += ", " + std::to_string(obj.elt_id % 1000);
		}
		std::string stD;
		for (auto& obj : answer.deleted) {
			stD += ", " + std::to_string(obj.elt_id % 1000);
		}
		log(std::to_string(m_network->getPeerId() % 100) + std::string(" Received objects {") + stC + "}, {" + stM + "}, {" + stD + "}");
#endif
		return answer;
	}

	void SynchTreeMessageManager::fillTreeAnswer(ComputerId cid_requester, TreeAnswer& answer, FsID elt_id, size_t depth, DateTime since, std::unordered_set<FsID>& already_seen) {
		FsEltPtr elt = m_filesystem->load(elt_id);
		log(std::string("answer request from") + since);
		if (elt) {
			// update synch state
			m_syncro->notifyPushElement(cid_requester, elt_id);
			already_seen.insert(elt_id);
			//ignore chunks
			if (FsElt::isObject(elt_id)) {
				FsObjectPtr object = FsElt::toObject(elt);
				//is it created, modified or deleted?
				size_t commit_size = object->getCommits().size();
				log(std::string("obj ")+object->getId()+(" was created at ") + object->getCreationTime() + " ?"+(object->getCreationTime() > since)
					+" and first commit: "+(commit_size? std::to_string(object->getCommits().back().id)+" ?"+ (object->getCommits().back().time > since) :std::string("N/A")));
				bool unchanged = false;
				if (object->getCreationTime() > since) {
					// the object has been created after the last fetch
					//created
					answer.created.push_back(FsObjectTreeAnswerPtr{ new FsObjectTreeAnswer{ elt_id , object->getDepth() , object->getCreationTime() , object->getName() ,
						object->getCUGA() , object->getGroupId(), object->getParent()} });
					assert(answer.created.back()->getCurrent().size() == answer.created.back()->sizes().size());
					if (commit_size > 0) {
						answer.created.back()->setCommit(object->getCurrent(), object->getCommits().back().id, object->getCommits().back().time, object->sizes());
					} else {
						assert(object->getCurrent().empty());
						assert(object->sizes().empty());
					}
				} else if (object->getDeletedDate() != 0 && (object->getDeletedDate() > since || (commit_size > 0 && object->getCommits().back().time > since))) {
					// the object has been deleted, and :
					//	. it's a deletion that occured since the last fetch
					//  . we were notified of it since the last fetch
					//deleted
					answer.deleted.emplace_back();
					answer.deleted.back().elt_id = elt_id;
					answer.deleted.back().elt_depth = object->getDepth(); //? it is deleted... meh, whatever.
					answer.deleted.back().elt_size = object->size();
					answer.deleted.back().last_commit_id = commit_size == 0 ? 0 : object->getCommits().back().id;
					answer.deleted.back().last_commit_time = commit_size == 0 ? 0 : object->getCommits().back().time;
					answer.deleted.back().renamed_to = object->getRenamedTo();
				} else if(commit_size > 0 && object->getCommits().back().time > since) {
					// the object has been modified since the last fetch
					//modified
					answer.modified.emplace_back();
					answer.modified.back().elt_id = elt_id;
					answer.modified.back().elt_depth = object->getDepth();
					answer.modified.back().elt_size = object->size();
					answer.modified.back().last_commit_id = commit_size == 0 ? 0 : object->getCommits().back().id;
					answer.modified.back().last_commit_time = commit_size == 0 ? 0 : object->getCommits().back().time;
					answer.modified.back().state = object->getCurrent();
					answer.modified.back().sizes = object->sizes();
					assert(answer.modified.back().state.size() == answer.modified.back().sizes.size());
				} else {
					answer.unchanged.push_back(elt_id);
					unchanged = true;
				}
				//if directory, and depth not 0, and there is a modification inside since the last fetch, go deeper
				if (FsElt::isDirectory(elt_id) && depth > 0 && !unchanged) {
					FsDirPtr dir = FsElt::toDirectory(elt);
					auto [commit_id, commit_time] = dir->getLastModification();
					if (commit_time > since) {
						for (const FsID& id : object->getCurrent()) {
							fillTreeAnswer(cid_requester, answer, id, depth - 1, since, already_seen);
						}
					}
				}
			}
		} else {
			error(std::string("error, a peer requested a tree from unknown id?"));
			// Assume deleted? just ignore?
		}
	}

	bool SynchTreeMessageManager::checkAndAnswerTreeRequest(const PeerPtr sender, TreeRequest& request) {
		std::shared_ptr<TreeRequest> original_treerequest;
		std::shared_ptr<bool> already_emmited;
		bool has_invalidation = false;
		// check that each elt is up to date here.
		// emit fetch for each elt that isn't up to date.
		for (FsID elt_id : request.roots) {
			Invalidation invalidation = m_syncro->getInvalidation(elt_id);
			if (invalidation.commit != 0) {
				ComputerId cid_modifier = FsElt::getComputerId(invalidation.commit);
				// then ask for it

				if (!original_treerequest) {
					original_treerequest.reset(new TreeRequest(request));
					already_emmited = std::make_shared<bool>(false);
				}

				// get peer
				ComputerId cid_notifier = m_syncro->notifierInvalidation(elt_id);
				if (cid_notifier == 0) continue; //...

				//get peer
				PeerPtr peer;
				PeerId pid = m_network->getPeerIdFromCompId(cid_modifier);
				peer = m_network->getPeerPtr(pid);
				if (!peer || !peer->isConnected()) {
					PeerId pid = m_network->getPeerIdFromCompId(cid_notifier);
					peer = m_network->getPeerPtr(pid);
					if (!peer || !peer->isConnected()) {
						// remove our invalidation: it's not accessible anymore
						m_syncro->removeInvalidatedElements({ elt_id });
						continue;
					}
				}

				// this is an invalidation that we should be able to resolve.
				// we set this switch to true as we won't answer the request right now but after one/some requests.
				has_invalidation = true;

				//create request
				TreeRequest request;
				request.roots.push_back(elt_id);
				request.depth = uint16_t(-1);
				SynchState synch_state = m_syncro->getSynchState(peer->getComputerId());
				if (synch_state && synch_state.last_commit_date > 0) {
					request.last_commit_received = synch_state.last_commit;
					request.last_fetch_time = synch_state.last_commit_received_date;
					assert(synch_state.last_commit_date > 0);
					assert(synch_state.last_commit_received_date > 0);
				} else {
					//no data on this cid: get all
					request.last_commit_received = 0;
					request.last_fetch_time = 0;
				}
				//create request 'waiter'
				TreeAnswerRequest& saved_request = registerRequest(request, peer->getComputerId());
				saved_request.roots.push_back(elt_id);
				saved_request.callbacks.push_back([this, sender, original_treerequest, already_emmited](TreeAnswer* answer) { 
					if (!*already_emmited) {
						*already_emmited.get() = checkAndAnswerTreeRequest(sender, *original_treerequest.get());
					}
					});
				//send request
				ByteBuff buff = writeTreeRequestMessage(request);
				log(std::string("TREE REQUEST (checkAndAnswerTreeRequest): ") + (m_network->getPeerId() % 100) + " ask " + (peer->getPeerId() % 100)+" for a request (from answer tree request)");
				peer->writeMessage(*SynchMessagetype::GET_TREE, buff);// writeTreeRequestMessage(request));
			}
		}
		if (has_invalidation) {
			// fetch are in progress, the callback will be used when finished.
			return false;
		}else{
			// we have evrything, emit the answer.
			TreeAnswer answer = answerTreeRequest(sender, request);
			log(std::string("TREE ANSWER: answer from ") + (m_network->getPeerId() % 100) + " to " + (sender->getPeerId() % 100));
			sender->writeMessage(*SynchMessagetype::SEND_TREE, writeTreeAnswerMessage(answer));
			return true;
		}
	}

	SynchTreeMessageManager::TreeAnswer SynchTreeMessageManager::answerTreeRequest(const PeerPtr requester, TreeRequest& request) {
		TreeAnswer answer;
		answer.request_id = request.request_id;
		answer.from = m_network->getComputerId();
		answer.answer_time = m_network->getCurrentTime();
		//get the ids
		std::unordered_set<FsID> already_seen;
		for (const FsID root_id : request.roots) {
			fillTreeAnswer(requester->getComputerId(), answer, root_id, request.depth, request.last_fetch_time, already_seen);
		}
		return answer;
	}

	void SynchTreeMessageManager::useTreeRequestAnswer(const PeerPtr sender, TreeAnswer&& partial_answer) {
		bool need_to_send_invalidation = false;
		std::map<FsID, FsID> item2commit; // to check if something has changed
		{std::lock_guard lock{ m_mutex_incomplete_requests };
		//fusion the result with currently waiting answers?
		TreeAnswerRequest* our_answer_request_storage;
		if (auto it = m_incomplete_requests.find(partial_answer.request_id); it != m_incomplete_requests.end()) {
			assert(partial_answer.request_id);
			log(std::string("TREE ANSWER: partial answer found in ") + (m_network->getPeerId() % 100) + " from " + (sender->getPeerId() % 100));
			//add it into the general answer
			assert(it->second.request_id == partial_answer.request_id);
			assert(it->second.our_answer.request_id == partial_answer.request_id);
			it->second.our_answer.modified.insert(it->second.our_answer.modified.end(), partial_answer.modified.begin(), partial_answer.modified.end());
			it->second.our_answer.created.insert(it->second.our_answer.created.end(), partial_answer.created.begin(), partial_answer.created.end());
			it->second.our_answer.deleted.insert(it->second.our_answer.deleted.end(), partial_answer.deleted.begin(), partial_answer.deleted.end());
			it->second.our_answer.answer_time = partial_answer.answer_time;
			our_answer_request_storage = &it->second;
			assert(our_answer_request_storage->our_answer.request_id != 0);
		} else {
			// create a new answer.... should already be created, no? => not if we don't ask anything and the peer just want to push a change to us directly.
			assert(partial_answer.request_id == 0);
			log(std::to_string(m_network->getPeerId() % 100) + " receive a push from " + (sender->getPeerId() % 100));
			//registerAnswer();
			partial_answer.request_id = FsElt::createId(FsType::NONE, m_request_id_generator.fetch_add(1), m_network->getComputerId());
			TreeAnswerRequest& req = m_incomplete_requests[partial_answer.request_id];
			req.request_id = partial_answer.request_id;
			req.request_to = sender->getComputerId();
			req.our_answer.from = sender->getComputerId();
			req.our_answer.request_id = partial_answer.request_id;
			req.our_answer = partial_answer;
			req.start = m_network->getCurrentTime();
			our_answer_request_storage = &req;
			//as it's a push, send invalidation to other peers.
			need_to_send_invalidation = true;
		}
		TreeAnswer& answer = our_answer_request_storage->our_answer;

		DateTime time = m_network->getCurrentTime();
		assert(our_answer_request_storage->start <= m_network->getCurrentTime());
		assert(our_answer_request_storage->start > 0);
		assert(our_answer_request_storage->start > m_network->getCurrentTime() - 4 * 60 * 1000);

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
#ifdef _DEBUG
		assert(m_incomplete_requests.find(answer.request_id) != m_incomplete_requests.end());
		std::unordered_set<FsID> create_change_del_unchanged = create_change;
		for (const TreeAnswerEltDeleted& del_obj : answer.deleted) {
			create_change_del_unchanged.insert(del_obj.elt_id);
		}
		for (FsID elt_id : answer.unchanged) {
			create_change_del_unchanged.insert(elt_id);
		}
		for (FsID requested_id : our_answer_request_storage->roots) {
			//assert(create_change_del.find(requested_id) != create_change_del.end());
			if (create_change_del_unchanged.find(requested_id) == create_change_del_unchanged.end()) {
				log("error: can't get an answer for tree request\n");
			}
		}
#endif
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
			request.request_id = our_answer_request_storage->request_id;
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

			our_answer_request_storage->finished = false;

			log(std::string("TREE REQUEST (useTreeRequestAnswer): ") + (m_network->getPeerId() % 100) + " ask " + (sender->getPeerId() % 100) + " for a request (still unknown ids)");
			sender->writeMessage(*SynchMessagetype::GET_TREE, this->writeTreeRequestMessage(request));
		} else {
			//the changes are "complete"
			// have to ordered the commit items, by depth in the filesystem.
			std::vector<FsObjectTreeAnswer> stub_storage;
			std::map<uint16_t, const FsObjectTreeAnswer*> ordered_stubs;
			std::vector<const FsElt*> stubs_to_merge;
			std::unordered_map<FsID, const FsElt*> db_stubs;
			std::lock_guard lock{ m_filesystem->synchronize() };
			for (FsObjectTreeAnswerPtr& obj : answer.created) {
				assert(obj);
				ordered_stubs[obj->getDepth()] = obj.get();
				db_stubs[obj->getId()] = obj.get();
				stubs_to_merge.push_back(obj.get());
			}
			for (const TreeAnswerEltChange& obj : answer.modified) {
				stub_storage.emplace_back(obj.elt_id, obj.elt_depth);
				stub_storage.back().setCommit(obj.state, obj.last_commit_id, obj.last_commit_time, obj.sizes);
			}
			for (const TreeAnswerEltDeleted& obj : answer.deleted) {
				stub_storage.emplace_back(obj.elt_id, obj.elt_depth);
				stub_storage.back().setCommit({}, obj.last_commit_id, obj.last_commit_time, {})
					.setDeleted(obj.renamed_to, obj.last_commit_time);
			}
			//now stub_storage is read-only, we can use pointers.
			for (const FsObjectTreeAnswer& obj : stub_storage) {
				ordered_stubs[obj.getDepth()] = &obj;
				db_stubs[obj.getId()] = &obj;
				stubs_to_merge.push_back(&obj);
			}

			//update our fs with these
			//for (const auto& depth2stub : ordered_stubs) {
			//	m_syncro->mergeCommit(sender->getComputerId(), *depth2stub.second, db_stubs, answer.from);
			//}
			//update our fs with these
			//log(std::string("fsbefore:") + m_filesystem->getDatabase().size());
			m_syncro->mergeCommit(sender->getComputerId(), stubs_to_merge, answer.from);
			//log(std::string("fsafter:") + m_filesystem->getDatabase().size());
			//note: maybe some changes are too old for my fs and so discarded.

			// unchanged element are now validated again!
			//Do i need ot check for the right peer? If i mess up the one i ask for, it's my fault, i guess.
			m_syncro->removeInvalidatedElements(answer.unchanged);

			// gather current merged commits
			if (need_to_send_invalidation) {
				for (const FsObjectTreeAnswerPtr& obj : answer.created) {
					if (this->m_filesystem->hasLocally(obj->getId())) {
						FsObjectPtr local_obj = this->m_filesystem->loadObject(obj->getId());
						if (local_obj && !local_obj->getCommits().empty()) {
							item2commit[obj->getId()] = local_obj->getCommits().back().id;
						}
					}
				}
				for (const TreeAnswerEltChange& obj : answer.modified) {
					if (this->m_filesystem->hasLocally(obj.elt_id)) {
						FsObjectPtr local_obj = this->m_filesystem->loadObject(obj.elt_id);
						if (local_obj && !local_obj->getCommits().empty()) {
							item2commit[obj.elt_id] = local_obj->getCommits().back().id;
						}
					}
				}
				for (const TreeAnswerEltDeleted& obj : answer.deleted) {
					if (this->m_filesystem->hasLocally(obj.elt_id)) {
						FsObjectPtr local_obj = this->m_filesystem->loadObject(obj.elt_id);
						if (local_obj && !local_obj->getCommits().empty()) {
							item2commit[obj.elt_id] = local_obj->getCommits().back().id;
						}
					}
				}
			}


			our_answer_request_storage->finished = true;
			our_answer_request_storage->finished_since = m_network->getCurrentTime();
			//call the callbacks
			foreach(callback_it, our_answer_request_storage->callbacks) {
				callback_it.get()(&our_answer_request_storage->our_answer);
				callback_it.erase();
			}
#ifndef NDEBUG
			// assert that the resulted filesystem is safe
			m_filesystem->checkFilesystem();
#endif
		}}
		// emit notifications for updated (maybe) items
		if (need_to_send_invalidation && !item2commit.empty()) {
			InvalidateElementsMessage invalidation;
			// TODO: is this the good time? check how it's used.
			invalidation.last_invalidation_time = m_network->getCurrentTime();
			for (auto& elt : item2commit) {
				invalidation.modified.push_back(elt.first);
				invalidation.commits.push_back(elt.second);
			}
			//for each peer
			PeerList peers = m_network->getPeersCopy();
			std::unordered_set<ComputerId> already_seen;
			foreach(it, peers) {
				ComputerId cid = (*it)->getComputerId();
				if (!(*it)->isConnected() || already_seen.find(cid) != already_seen.end()) {
					it.erase();
				} else if (cid == m_network->getComputerId() || cid == sender->getComputerId()) {
					// but us and the caller
					it.erase();
				} else {
					already_seen.insert(cid);
				}
			}
			ByteBuff buffer = this->writeInvalidateEltsMessage({ invalidation });
			for (PeerPtr& peer : peers) {
				peer->writeMessage(*SynchMessagetype::SEND_INVALIDATE_ELT, buffer.rewind());
			}
		}
	}
}
