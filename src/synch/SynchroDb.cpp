#include "SynchroDb.hpp"
#include "SynchTreeMessageManager.hpp"
#include "ExchangeChunkMessageManager.hpp"

#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"

#include <fstream>

namespace supercloud {


    void SynchroDb::init(std::shared_ptr<FsStorage> file_storage, std::shared_ptr<ClusterManager> network) {
        m_network = network;
        m_file_storage = file_storage;
        m_chunk_sizer = std::shared_ptr<BasicChunkSizer>{ new BasicChunkSizer{} };
    }


    void SynchroDb::launch() {
        assert(!m_synch_tree);
        assert(!m_synch_chunk);
        m_synch_tree = SynchTreeMessageManager::create(m_network, m_file_storage, ptr());
        m_synch_chunk = ExchangeChunkMessageManager::create(m_network, m_file_storage, ptr());
        m_network->registerListener(*UnnencryptedMessageType::TIMER_SECOND, this->ptr());
        m_network->registerListener(*UnnencryptedMessageType::TIMER_MINUTE, this->ptr());
        assert(m_synch_tree);
        assert(m_synch_chunk);
    }

    void SynchroDb::receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) {
        if (message_id == *UnnencryptedMessageType::TIMER_SECOND) {
            //log(std::to_string(m_network->getComputerId()) + "$ RECEIVE TIMER_SECOND from " + sender->getPeerId());
            //get current DateTime
            DateTime current_time = message.rewind().getLong();
            //assert(std::abs(current_time - m_network->getCurrentTime()) < 100); // less than 100ms of latency (should even be less than a milisecond...)
            //update : emit invalidation message
            this->update(current_time);
        } else if (message_id == *UnnencryptedMessageType::TIMER_MINUTE) {
            //get current DateTime
            DateTime current_time = message.rewind().getLong();
            // remove old invalidation. they should already have been updated.
            {std::lock_guard lock{ m_invalidated_mutex };
                DateTime current_time = m_network->getCurrentTime();
                std::vector<uint64_t> old_request;
                for (auto& id2_inv : m_invalidated) {
                    if (toDateTime(id2_inv.second.date_notified) + 2 * 60 * 1000 < current_time) {
                        old_request.push_back(id2_inv.first);
                    }
                }
                for (uint64_t id : old_request) {
                    m_invalidated.erase(id);
                }
            }

            // push modification made by me to my neighbors if they don't fetch it already.
            this->push_modifications(current_time);

            //TODO: fetch for cid that have modifications. 
            
        }
    }

    void SynchroDb::push_modifications(DateTime current) {
        std::map<PeerPtr, SynchTreeMessageManager::TreeRequest> to_send;
        {
            std::lock_guard lock{ m_current_states_mutex };
            PeerList peers_neighbors;

            // ---- update neighbors ----
            PeerList peers_to_check = m_network->getPeersCopy();
            foreach(it_peer, peers_to_check) {
                Peer& peer = *it_peer->get();
                if (!peer.isConnected() || !peer.isAlive()) {
                    it_peer.erase();
                }
            }
            // remove is_neighbor flag for disconnected peers
            for (auto& it : m_current_states) {
                if (it.second.is_neighbor) {
                    bool found = false;
                    foreach(it_peer, peers_to_check) {
                        Peer& peer = *it_peer->get();
                        if (it.second.cid == peer.getComputerId()) {
                            found = true;
                            peers_neighbors.push_back(*it_peer);
                            it_peer.erase();
                        }
                    }
                    it.second.is_neighbor = found;
                }
            }
            // set is_neighbor flag for connected peers
            foreach(it_peer, peers_to_check) {
                Peer& peer = *it_peer->get();
                if (auto it = m_current_states.find(peer.getComputerId()); it != m_current_states.end()) {
                    it->second.is_neighbor = true;
                } else {
                    // create new synch state
                    SynchState& new_state = m_current_states[peer.getComputerId()];
                    new_state.cid = peer.getComputerId();
                    new_state.is_neighbor = true;
                }
                peers_neighbors.push_back(*it_peer);
            }
            // ---- END update neighbors ----


            // ---- emit push to neighbors ----
            DateTime current_time = m_network->getCurrentTime();
            for (PeerPtr& peer : peers_neighbors) {
                assert(m_current_states.find(peer->getComputerId()) != m_current_states.end());
                SynchState& state = m_current_states[peer->getComputerId()];
                assert(state.is_neighbor);
                if (!state.unpushed_elts.empty()) {
                    //create request
                    SynchTreeMessageManager::TreeRequest &fake_request = to_send[peer];
                    fake_request.depth = -1;
                    // date for limitting the nodes in the push: use the first date of unpushed things, (or last_fetch_date if more recent and first_unpushed_date is strangely old)
                    fake_request.last_fetch_time = state.first_unpushed_date - 100;
                    if (fake_request.last_fetch_time < current_time - 600000 && state.last_fetch_date > state.first_unpushed_date) {
                        fake_request.last_fetch_time = state.last_fetch_date;
                    }
                    fake_request.roots.insert(fake_request.roots.begin(), state.unpushed_elts.begin(), state.unpushed_elts.end());
                    //update synch (note that it should be cleared by 'answerTreeRequest' anyway)
                    state.unpushed_elts.clear();
                }
            }
        }//end of lock

        //emit message out of lock, for safety (only do what's strictly necessary inside a lock).
        for (auto& peer_2_request : to_send) {
            //create & send message
            peer_2_request.first->writeMessage(*SynchMessagetype::SEND_TREE,
                m_synch_tree->writeTreeAnswerMessage(
                    m_synch_tree->answerTreeRequest(peer_2_request.first, peer_2_request.second)));
        }

    }

    void SynchroDb::update(DateTime current) {
        // emit an invalidated message each 5 seconds for object that i have modified.
        std::unordered_map<FsID, Invalidation> copy_invalidated;
        {std::lock_guard lock{ m_newly_invalidated_by_mutex };
        if (!m_newly_invalidated.empty()
            && m_invalidation_wait_since_to_send < current) {
            copy_invalidated = m_newly_invalidated;
            m_newly_invalidated.clear();
            // only send an invalidation message each 5 seconds.
            m_invalidation_wait_since_to_send = current + 5000;
        }}
        if (!copy_invalidated.empty()) {
            m_invalidation_last_sent = current;
            // even if it should be harmless, it's safer to do 'complex' things outside of the mutex.
            m_synch_tree->emitModificationsNotification(copy_invalidated);
        }
    }

    void SynchroDb::addInvalidatedElements(const std::vector<FsID>& invalidated, const std::vector<FsCommitID>& commits, ComputerId sender, ComputerId modifier, DateTime last_modif_time) {
        //check i'm not the one who sent it first
        if (modifier == m_network->getComputerId()) return;
        assert(sender != m_network->getComputerId());
        //update state (or return if alredy up-to-date)
        {std::lock_guard lock{ m_current_states_mutex };
        if (auto it = m_current_states.find(modifier); it != m_current_states.end()) {
            if (it->second.last_fetch_date > last_modif_time) {
                //i already updated.
                return;
            }
            if (it->second.last_modification_date < last_modif_time) {
                it->second.last_modification_date = last_modif_time;
            }
        } else {
            //create one for him
            auto& state = m_current_states[modifier];
            state.cid = modifier;
            state.last_modification_date = last_modif_time;
        }}

        // remove invalidation with commits we already known
        std::vector<size_t> good_idx;
        {
            std::lock_guard lock{ this->fsSynchronize() };
            assert(invalidated.size() == commits.size());
            for (size_t idx = 0; idx < invalidated.size(); ++idx) {
                FsID elt_id = invalidated[idx];
                FsCommitID inval_commit = commits[idx];
                FsObjectPtr obj = m_file_storage->loadObject(elt_id);
                //if not inside, then you may want keep it in the avalidation array... but it's not very useful.
                if (obj) {
                    //auto [last_commit, last_time] = obj->getLastModification();
                    if (obj->getCommits().empty()) {
                        good_idx.push_back(idx);
                    } else {
                        //check time of the commit
                        bool inval_not_needed = last_modif_time < obj->getCommits().back().time;
                        if (!inval_not_needed) {
                            //check if the commit isn't inside.
                            for (auto& check_commit : obj->getCommits()) {
                                if (check_commit.id == inval_commit) {
                                    inval_not_needed = true;
                                    break;
                                }
                            }
                        }
                        if (!inval_not_needed) {
                            good_idx.push_back(idx);
                        }
                    }
                }
            }
        }

        // populate m_invalidated_2_modifier_notifier
        std::vector<FsID> inval_by_toadd;
        Invalidation invalidation{ sender, toDate(m_network->getCurrentTime()), toDate(last_modif_time), 0/*commmit to fill*/};
        {std::lock_guard lock{ m_invalidated_mutex };
        assert(invalidated.size() == commits.size());
        for (size_t idx : good_idx) {
            FsID id = invalidated[idx];
            FsCommitID commit = commits[idx];
            if (auto& it = m_invalidated.find(id); it != m_invalidated.end()) {
                //i already know. I'll change the from, for good measure.
                it->second = invalidation;
                it->second.commit = commit;
            } else {
                Invalidation& stored_invalidation = m_invalidated[id];
                stored_invalidation = invalidation;
                stored_invalidation.commit = commit;
                inval_by_toadd.push_back(id); // to be sure we don't emit to sender again. //TODO test
            }
        }}

        //also populate m_newly_invalidated_2_modifier_notifier if the entry is new for us.
        {std::lock_guard lock{ m_newly_invalidated_by_mutex };
        for (FsID id : inval_by_toadd) {
            m_newly_invalidated[id] = m_invalidated[id];
        }}
    }

    void SynchroDb::removeInvalidatedElements(const std::vector<FsID>& not_invadidated_anymore) {
        std::lock_guard lock{ m_invalidated_mutex };
        for (const FsID& id : not_invadidated_anymore) {
            m_invalidated.erase(id);
        }
    }

    bool SynchroDb::hasInvalidation() {
        return !m_invalidated.empty();
    }
#ifdef _DEBUG
    bool SynchroDb::isInvalidated(FsID to_test) {
        std::lock_guard lock{ m_invalidated_mutex };
        return m_invalidated.find(to_test) != m_invalidated.end();
    }
#endif
    Invalidation SynchroDb::getInvalidation(FsID to_test) {
        std::lock_guard lock{ m_invalidated_mutex };
        auto it = m_invalidated.find(to_test);
        return it == m_invalidated.end() ? Invalidation{0,0,0,0} : it->second;
    }
    ComputerId SynchroDb::notifierInvalidation(FsID to_test) {
        std::lock_guard lock{ m_invalidated_mutex };
        auto it = m_invalidated.find(to_test);
        return it == m_invalidated.end() ? 0 : it->second.notifier;
    }


    void SynchroDb::updateFetchTime(ComputerId cid) {
        if (auto it = m_current_states.find(cid); it != m_current_states.end()) {
            it->second.last_fetch_date = m_network->getCurrentTime();
        } else {
            assert(cid != 0);
            SynchState& my_state = m_current_states[cid];
            my_state.cid = cid;
            my_state.last_fetch_date = m_network->getCurrentTime();
        }
    }

    void SynchroDb::askForFsSynch(FsID elt_to_get, std::function<void(FsEltPtr answer)> callback) {
        if (FsElt::isObject(elt_to_get)) {
            m_synch_tree->fetchTree(elt_to_get, [this, elt_to_get, callback](SynchTreeMessageManager::TreeAnswerPtr answer) {
                    std::lock_guard lock{ m_file_storage_mutex };
                    callback(m_file_storage->load(elt_to_get));
                });
        } else if (FsElt::isChunk(elt_to_get)) {
            m_synch_chunk->fetchChunk(elt_to_get, [this, elt_to_get, callback](ExchangeChunkMessageManager::FsChunkTempPtr result) {
                    //add it to local store
                    callback(result);
                });
        } else {
            callback({});
        }
    }

    void SynchroDb::retreiveChunks(FsFilePtr file, std::vector<FsID> chunks_id_to_retreive, std::function<void(const std::vector<FsChunkPtr>&)> callback_caller) {
        if (file) {
            std::shared_ptr<std::pair<std::recursive_mutex, std::unordered_set<FsID>>> missing_chunks = std::make_shared< std::pair<std::recursive_mutex, std::unordered_set<FsID>>>();
            std::shared_ptr<std::vector<FsChunkPtr>> needed_chunks = std::make_shared<std::vector<FsChunkPtr>>();
            {std::lock_guard lock{ m_file_storage_mutex };
            for (const FsID& chunk_id : chunks_id_to_retreive) {
                if (!m_file_storage->hasLocally(chunk_id)) {
                    missing_chunks->second.insert(chunk_id);
                } else {
                    needed_chunks->push_back(m_file_storage->loadChunk(chunk_id));
                }
            }}
            if (!file->getCurrent().empty()) {
                auto callback_wait_all = [missing_chunks, needed_chunks, callback_caller](ExchangeChunkMessageManager::FsChunkTempPtr answer) {
                    if (answer) {
                        bool is_empty;
                        {std::lock_guard lock{ missing_chunks->first };
                        assert(!missing_chunks->second.empty());
                        needed_chunks->push_back(answer);
                        missing_chunks->second.erase(answer->getId());
                        is_empty = missing_chunks->second.empty();
                        }
                        if (is_empty) {
                            callback_caller(*needed_chunks);
                        }
                    } else {
                        assert(false);
                        callback_caller({});
                    }
                };

                {std::lock_guard lock{ missing_chunks->first };
                for (const FsID& chunk_id : missing_chunks->second) {
                    m_synch_chunk->fetchChunk(chunk_id, callback_wait_all);
                }}
            }
        }else{
            callback_caller({});
        }
    }

    void SynchroDb::notifyObjectChanged(FsID id) {
        //group them and only send them after some seconds.
        {std::lock_guard lock{ m_newly_invalidated_by_mutex };
            if (m_newly_invalidated.empty()) {
                // ask to wait at least a second before emitting it? (often, many changes are packed toguether)
                m_invalidation_wait_since_to_send = std::max(m_network->getCurrentTime() + 1000, m_invalidation_last_sent + 5000);
            }
            m_newly_invalidated[id] = { m_network->getComputerId(), 0, 0, FsElt::createId(FsType::NONE, 0, m_network->getComputerId()) };
        }
        {
            std::lock_guard lock{ m_current_states_mutex };
            for (auto& it : m_current_states) {
                if (it.second.unpushed_elts.empty()) {
                    it.second.first_unpushed_date = m_network->getCurrentTime();
                }
                if (it.second.is_neighbor) {
                    it.second.unpushed_elts.insert(id);
                }
            }
        }
    }

    bool SynchroDb::mergeCommit(ComputerId from, const std::vector<const FsElt*>& elts_to_merge, ComputerId modifier) {
        //it's not needed to send chunks. the file commit already has their ids.
        std::vector<const FsObject*> objs_to_merge;
        for (const FsElt* elt : elts_to_merge) {
            if (FsElt::isObject(elt->getId())) {
                objs_to_merge.push_back(static_cast<const FsObject*>(elt));
            }
        }

        if (objs_to_merge.empty()) return false;
        
        //update the invalidation map
        {std::lock_guard lock_invalidation{ m_invalidated_mutex };
        for (const FsObject* elt : objs_to_merge) {
            if (auto it = m_invalidated.find(elt->getId()); it != m_invalidated.end()) {
                ComputerId modifier = FsElt::getComputerId(it->second.commit);
                if (it->second.notifier == from || modifier == from || modifier == modifier) {
                    m_invalidated.erase(elt->getId());
                }
            }
        }}


        std::lock_guard lock{ m_file_storage_mutex };
        /// When we fetch data, we receive FsElt with only the last commits that should be useful for us. (it shouldn't send data much older than what we have).
        /// The object only have the most basic implementation (ids of commits with ids of content, with dates).
        /// This method use this information to modify our own implementation of the FsStorage to insert the data we diddn't known yet.
        /// Note: we may only get the state of the last commit (for each object) since the last fetch. Each server have an incomplete knowledge of the past commit, as these
        ///  can be destroyed at will to avoid clutter. The important thing is that a commit id is unique and the state of the FsElt for his commit is the same evrywhere.
        SynchState no_source;
        SynchState& my_state = from == 0 ? no_source : m_current_states[from];
        if (!my_state.cid) {
            //create it
            my_state.cid = from;
        }
        //get last commit
        DateTime last_commit_time = 0;
        FsID last_commit = 0;
        for (const FsObject* elt : objs_to_merge) {
            if (!elt->getCommits().empty()) {
                if (elt->getCommits().back().time > last_commit_time) {
                    last_commit_time = elt->getCommits().back().time;
                    last_commit = elt->getCommits().back().id;
                }
            } else {
                if (elt->getCreationTime() > last_commit_time) {
                    last_commit_time = elt->getCreationTime();
                    last_commit = elt->getId();
                }
            }
        }
        //update
        my_state.last_commit = last_commit;
        my_state.last_commit_date = last_commit_time;
        my_state.last_commit_received_date = m_network->getCurrentTime();
        assert(my_state.last_commit_date > 0);
        assert(my_state.last_commit_received_date > 0);

        //merge
        m_file_storage->mergeObjectsCommit(objs_to_merge);
        return true;
    }

    //bool SynchroDb::mergeCommit_deprecated(ComputerId from, const FsElt& to_merge, const std::unordered_map<FsID, const FsElt*>& extra_db, ComputerId modifier) {
    //    //update the invalidation map
    //    {std::lock_guard lock_invalidation{ m_invalidated_mutex };
    //        if (auto it = m_invalidated.find(to_merge.getId()); it != m_invalidated.end()) {
    //            ComputerId modifier = FsElt::getComputerId(it->second.commit);
    //            if (it->second.notifier == from || modifier == from || modifier == modifier) {
    //                m_invalidated.erase(to_merge.getId());
    //            }
    //        }
    //    }
    //    std::lock_guard lock{ m_file_storage_mutex };
    //    /// When we fetch data, we receive FsElt with only the last commits that should be useful for us. (it shouldn't send data much older than what we have).
    //    /// The object only have the most basic implementation (ids of commits with ids of content, with dates).
    //    /// This method use this information to modify our own implementation of the FsStorage to insert the data we diddn't known yet.
    //    /// Note: we may only get the state of the last commit (for each object) since the last fetch. Each server have an incomplete knowledge of the past commit, as these
    //    ///  can be destroyed at will to avoid clutter. The important thing is that a commit id is unique and the state of the FsElt for his commit is the same evrywhere.
    //    SynchState no_source;
    //    SynchState& my_state = from == 0 ? no_source : m_current_states[from];
    //    if (!my_state.cid) {
    //        //create it
    //        my_state.cid = to_merge.getOwner();
    //    }
    //    FsID last_id = my_state.last_commit;
    //    assert(my_state.last_commit_date >= 0);
    //    assert(my_state.last_commit_received_date >= 0);
    //    if (FsElt::isChunk(to_merge.getId())) {
    //        //chunk is immutable. its commit is its id.
    //        if (last_id < to_merge.getId()) {
    //            //update
    //            my_state.last_commit = to_merge.getId();
    //            my_state.last_commit_date = to_merge.getCreationTime();
    //            my_state.last_commit_received_date = m_network->getCurrentTime();
    //            assert(my_state.last_commit_date > 0);
    //            assert(my_state.last_commit_received_date > 0);

    //            //it's not needed to send chunks. the file commit already has their ids.
    //            assert(false);
    //        }
    //    } else if (FsElt::isFile(to_merge.getId())) {
    //        const FsFile* file = static_cast<const FsFile*>(&to_merge);
    //        assert(file->getCommits().size() > 0);
    //        const FsObjectCommit& commit = file->getCommits().back();
    //        //update
    //        my_state.last_commit = commit.id;
    //        my_state.last_commit_date = commit.date;
    //        my_state.last_commit_received_date = m_network->getCurrentTime();
    //        assert(my_state.last_commit_date > 0);
    //        assert(my_state.last_commit_received_date > 0);
    //        //ask fs to update the file
    //        return m_file_storage->mergeFileCommit(*file, extra_db);
    //    } else if (FsElt::isDirectory(to_merge.getId())) {
    //        const FsDirectory* dir = static_cast<const FsDirectory*>(&to_merge);
    //        //assert(dir->getCommitsSize() > 0); //can be 0 if it's an empty directory
    //        if (dir->getCommits().size() > 0) {
    //            const FsObjectCommit& commit = dir->getCommits().back();
    //            //update
    //            my_state.last_commit = commit.id;
    //            my_state.last_commit_date = commit.date;
    //            my_state.last_commit_received_date = m_network->getCurrentTime();
    //            assert(my_state.last_commit_date > 0);
    //            assert(my_state.last_commit_received_date > 0);
    //        } else {
    //            my_state.last_commit = dir->getId();
    //            my_state.last_commit_date = dir->getCreationTime();
    //            my_state.last_commit_received_date = m_network->getCurrentTime();
    //            assert(my_state.last_commit_date > 0);
    //            assert(my_state.last_commit_received_date > 0);
    //        }
    //        //ask fs to update the file
    //        return m_file_storage->mergeDirectoryCommit(*dir, extra_db);
    //    }
    //    return false;
    //}
    
    void SynchroDb::save(std::filesystem::path& file) {
        ByteBuff buffer;
        buffer.putSize(this->m_current_states.size());
        for (auto& cid_2_sycstate : m_current_states) {
            buffer.serializeComputerId(cid_2_sycstate.first);
            buffer.putULong(cid_2_sycstate.second.last_commit);
            buffer.putLong(cid_2_sycstate.second.last_commit_date);
            buffer.putLong(cid_2_sycstate.second.last_commit_received_date);
            buffer.putLong(cid_2_sycstate.second.last_fetch_date);
        }
        buffer.flip();
        auto myfile = std::fstream(file, std::ios::out | std::ios::binary);
        myfile.write((char*)buffer.raw_array() + buffer.position(), buffer.limit());
        myfile.close();
    }

    void SynchroDb::load(std::filesystem::path& file) {
        std::ifstream infile(file, std::ios_base::binary);
        std::vector<char> vector_buffer{ std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>() };
        infile.close();
        ByteBuff buffer{ (uint8_t*)vector_buffer.data(), vector_buffer.size() };

        m_current_states.clear();
        const size_t nb_elts = buffer.getSize();
        for (size_t idx = 0; idx < nb_elts; ++idx) {
            ComputerId elt = buffer.deserializeComputerId();
            FsID commit = buffer.getULong();
            DateTime commit_date = buffer.getLong();
            DateTime received_date = buffer.getLong();
            DateTime fetch_date = buffer.getLong();
            m_current_states[elt] = { elt, false, commit, commit_date, received_date, fetch_date, 0, {} };
        }

    }

}

