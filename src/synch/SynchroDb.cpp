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
        m_synch_tree = SynchTreeMessageManager::create(m_network, m_file_storage, ptr());
        m_synch_chunk = ExchangeChunkMessageManager::create(m_network, m_file_storage, ptr());
        m_network->registerListener(*UnnencryptedMessageType::TIMER_SECOND, this->ptr());
    }

    void SynchroDb::receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) {
        if (message_id == *UnnencryptedMessageType::TIMER_SECOND) {
            //log(std::to_string(m_network->getComputerId()) + "$ RECEIVE TIMER_SECOND from " + sender->getPeerId());
            //get current DateTime
            DateTime current_time = message.rewind().getLong();
            assert(std::abs(current_time - m_network->getCurrentTime()) < 100); // less than 100ms of latency (should even be less than a milisecond...)
            //update
            this->update(current_time);
        }
    }

    void SynchroDb::update(DateTime current) {
        // emit an invalidated message each 5 seconds for object that i have modified.
        std::unordered_map<FsID, Invalidation> copy_invalidated;
        {std::lock_guard lock{ m_newly_invalidated_by_mutex };
        if (!m_newly_invalidated_2_modifier_notifier.empty()
            && m_last_invalidation_sent < current
            // only send an invalidation message each 5 seconds.
            && current - m_last_invalidation_sent > 5000) {
            copy_invalidated = m_newly_invalidated_2_modifier_notifier;
            m_newly_invalidated_2_modifier_notifier.clear();
            m_last_invalidation_sent = current; // not useful, but safer.
        }}
        if (!copy_invalidated.empty()) {
            // even if it should be harmless, it's safer to do 'complex' things outside of the mutex.
            m_synch_tree->emitModificationsNotification(copy_invalidated);
        }
    }

    void SynchroDb::addInvalidatedElements(const std::vector<FsID>& invadidated, ComputerId sender, ComputerId modifier, DateTime last_modif_time) {
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
            state.id = modifier;
            state.last_modification_date = last_modif_time;
        }}
        // populate m_invalidated_2_modifier_notifier
        std::vector<FsID> inval_by_toadd;
        Invalidation invalidation{ modifier, sender };
        {std::lock_guard lock{ m_invalidated_mutex };
        for (FsID id : invadidated) {
            if (auto& it = m_invalidated_2_modifier_notifier.find(id); it != m_invalidated_2_modifier_notifier.end()) {
                //i already know. I'll change the from, for good measure.
                it->second = invalidation;
            } else {
                m_invalidated_2_modifier_notifier[id] = invalidation;
                inval_by_toadd.push_back(id); //notifier, to be sure we don't emit to him again.
            }
        }}
        //also populate m_newly_invalidated_2_modifier_notifier if the entry is new for us.
        {std::lock_guard lock{ m_newly_invalidated_by_mutex };
        for (FsID id : inval_by_toadd) {
            m_newly_invalidated_2_modifier_notifier[id] = invalidation;
        }}
    }

    ComputerId SynchroDb::isInvalidated(FsID to_test) {
        std::lock_guard lock{ m_invalidated_mutex };
        auto it = m_invalidated_2_modifier_notifier.find(to_test);
        return it == m_invalidated_2_modifier_notifier.end() ? 0 : it->second.modifier;
    }


    void SynchroDb::updateFetchTime(ComputerId cid) {
        if (auto it = m_current_states.find(cid); it != m_current_states.end()) {
            it->second.last_fetch_date = m_network->getCurrentTime();
        } else {
            assert(cid != 0);
            SynchState& my_state = m_current_states[cid];
            my_state.id = cid;
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

    void SynchroDb::retreiveChunks(FsFilePtr file, size_t min_size, std::function<void(bool)> callback_caller) {
        if (file) {
            std::shared_ptr<std::pair<std::mutex, std::unordered_set<FsID>>> missing_chunks = std::make_shared< std::pair<std::mutex, std::unordered_set<FsID>>>();
            {std::lock_guard lock{ m_file_storage_mutex };
            for (const FsID& chunk_id : file->getCurrent()) {
                if (!m_file_storage->hasLocally(chunk_id)) {
                    missing_chunks->second.insert(chunk_id);
                }
            }}
            if (!file->getCurrent().empty()) {
                auto callback_wait_all = [missing_chunks, callback_caller](ExchangeChunkMessageManager::FsChunkTempPtr answer) {
                    if (answer) {
                        bool is_empty;
                        {std::lock_guard lock{ missing_chunks->first };
                        assert(!missing_chunks->second.empty());
                        missing_chunks->second.erase(answer->getId());
                        is_empty = missing_chunks->second.empty();
                        }
                        if (is_empty) {
                            callback_caller(true);
                        }
                    } else {
                        assert(false);
                        callback_caller(false);
                    }
                };

                {std::lock_guard lock{ missing_chunks->first };
                for (const FsID& chunk_id : missing_chunks->second) {
                    m_synch_chunk->fetchChunk(chunk_id, callback_wait_all);
                }}
            }
        }else{
            callback_caller(false);
        }
    }

    void SynchroDb::notifyObjectChanged(FsID id) {
        //group them and only send them after some seconds.
        std::lock_guard lock{ m_newly_invalidated_by_mutex };
        if (m_newly_invalidated_2_modifier_notifier.empty()) {
            m_last_invalidation_sent = m_network->getCurrentTime();
        }
        if (m_invalidated_by_me.modifier == 0 || m_invalidated_by_me.modifier == NO_COMPUTER_ID) {
            m_invalidated_by_me = { m_network->getComputerId(),m_network->getComputerId() };
        }
        m_newly_invalidated_2_modifier_notifier[id] = m_invalidated_by_me;
    }

    bool SynchroDb::mergeCommit(ComputerId from, const FsElt& to_merge, const std::unordered_map<FsID, const FsElt*>& extra_db) {
        std::lock_guard lock{ m_file_storage_mutex };
        /// When we fetch data, we receive FsElt with only the last commits that should be useful for us. (it shouldn't send data much older than what we have).
        /// The object only have the most basic implementation (ids of commits with ids of content, with dates).
        /// This method use this information to modify our own implementation of the FsStorage to insert the data we diddn't known yet.
        /// Note: we may only get the state of the last commit (for each object) since the last fetch. Each server have an incomplete knowledge of the past commit, as these
        ///  can be destroyed at will to avoid clutter. The important thing is that a commit id is unique and the state of the FsElt for his commit is the same evrywhere.
        SynchState no_source;
        SynchState& my_state = from == 0 ? no_source : m_current_states[from];
        if (!my_state.id) {
            //create it
            my_state.id = to_merge.getOwner();
        }
        FsID last_id = my_state.last_commit;
        assert(my_state.last_commit_date >= 0);
        assert(my_state.last_commit_received_date >= 0);
        if (FsElt::isChunk(to_merge.getId())) {
            //chunk is immutable. its commit is its id.
            if (last_id < to_merge.getId()) {
                //update
                my_state.last_commit = to_merge.getId();
                my_state.last_commit_date = to_merge.getDate();
                my_state.last_commit_received_date = m_network->getCurrentTime();
                assert(my_state.last_commit_date > 0);
                assert(my_state.last_commit_received_date > 0);

                //it's not needed to send chunks. the file commit already has their ids.
                assert(false);
            }
        } else if (FsElt::isFile(to_merge.getId())) {
            const FsFile* file = static_cast<const FsFile*>(&to_merge);
            assert(file->getCommitsSize() > 0);
            const FsObjectCommit& commit = file->getCommit(file->getCommitsSize() - 1);
            //update
            my_state.last_commit = commit.id;
            my_state.last_commit_date = commit.date;
            my_state.last_commit_received_date = m_network->getCurrentTime();
            assert(my_state.last_commit_date > 0);
            assert(my_state.last_commit_received_date > 0);
            //ask fs to update the file
            return m_file_storage->mergeFileCommit(*file, extra_db);
        } else if (FsElt::isDirectory(to_merge.getId())) {
            const FsDirectory* dir = static_cast<const FsDirectory*>(&to_merge);
            //assert(dir->getCommitsSize() > 0); //can be 0 if it's an empty directory
            if (dir->getCommitsSize() > 0) {
                const FsObjectCommit& commit = dir->getCommit(dir->getCommitsSize() - 1);
                //update
                my_state.last_commit = commit.id;
                my_state.last_commit_date = commit.date;
                my_state.last_commit_received_date = m_network->getCurrentTime();
                assert(my_state.last_commit_date > 0);
                assert(my_state.last_commit_received_date > 0);
            } else {
                my_state.last_commit = dir->getId();
                my_state.last_commit_date = dir->getDate();
                my_state.last_commit_received_date = m_network->getCurrentTime();
                assert(my_state.last_commit_date > 0);
                assert(my_state.last_commit_received_date > 0);
            }
            //ask fs to update the file
            return m_file_storage->mergeDirectoryCommit(*dir, extra_db);
        }
        return false;
    }
    
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
            m_current_states[elt] = { elt, commit, commit_date, received_date, fetch_date };
        }

    }

}

