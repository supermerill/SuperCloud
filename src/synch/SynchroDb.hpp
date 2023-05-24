#pragma once

#include "network/ClusterManager.hpp"
#include "fs/base/FsStorage.hpp"
#include "fs/base/FsElt.hpp"
#include "utils/Utils.hpp"
#include "FsExternalInterface.hpp"

#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace supercloud {
    class SynchTreeMessageManager;
    class ExchangeChunkMessageManager;

    class ChunkSizer {
    public:
        virtual size_t chooseChunkSize(FsFilePtr file, std::vector<size_t> current_sizes, size_t current_size, size_t new_size, size_t idx_new_chunk) = 0;
    };


    class BasicChunkSizer : public ChunkSizer {
    public:
        size_t chooseChunkSize(FsFilePtr file, std::vector<size_t> current_sizes, size_t current_size, size_t new_size, size_t idx_new_chunk) override {
            //if (new_size <= 4096) return new_size; // only one chunk for small files
            if (idx_new_chunk == 0) return 4096; // first chunk is small as it's often requested
            // accumulate existing chunks
            size_t previous_chunks_size = 0;
            size_t chunk_idx;
            for (chunk_idx = 0; chunk_idx < current_sizes.size() && chunk_idx < idx_new_chunk; ++chunk_idx) {
                if (previous_chunks_size + current_sizes[chunk_idx] > new_size) {
                    assert(false); // error: wrong idx_new_chunk
                    break;
                }
                previous_chunks_size += current_sizes[chunk_idx];
            }
            size_t max_size = std::max(new_size / 9, size_t(4096)); // try to have no more than 10 chunks, it's enough for a naive algorithm.
            if (max_size >= new_size - previous_chunks_size) {
                // if the next chunk can be large enough
                return new_size - previous_chunks_size;
            } else {
                // if the next chunk isn't large enough
                if (max_size > new_size - previous_chunks_size - 4096) {
                    // if the next chunk is almost enough large
                    if (max_size < 40960) {
                        // it's small, enlarge it
                        return new_size - previous_chunks_size;
                    } else {
                        // it's big, reduce it
                        return new_size - previous_chunks_size - 4096;
                    }
                } else {
                    // use the normal size (~10 chunks per file)
                    return max_size;
                }
            }
        }
    };

    struct SynchState {
        //this is the synch state for this computer.
        ComputerId cid = 0;
        // true if we have a direct link
        bool is_neighbor = false;
        // last commit created on the computer
        FsCommitID last_commit = 0;
        // date of the commit (cache to avoid calling the fs)
        DateTime last_commit_date = 0;
        // date of the moment we where notified of it.
        DateTime last_commit_received_date = 0;
        // date of last time we ask for a refresh of our knownledge.
        DateTime last_fetch_date = 0;
        // date of the time where it nofified the world that it does something on the fs.
        DateTime last_modification_date = 0;
        // date of the oldest unpushed elt.
        DateTime first_unpushed_date = 0;
        // list of elt that i should push to him if not fetch shortly.
        std::unordered_set<FsID> unpushed_elts;
        operator bool() const { return cid != 0 && cid != NO_COMPUTER_ID; }
    };
    struct ComputerState {
        //other informations about this computerid

        //the user id of this computer
        std::string user_name;
        uint32_t user_id;
        std::string group_id;

        // stats about the peer, to know if he is reliable
        double nb_seconds_reachable;
        double nb_seconds_connected;

    };

    //TODO: a way to say "no! it's not available"
    enum ComputerAvailabilityState : uint8_t {
        EMPTY = 0,
        HAS_CHUNK = 1 << 0, // 1: this cid has the chunk, 0: it had it but deleted since.
        FETCH_FROM_SOURCE = 1 << 1, // 1: this cid give me this information directly, 0: someone else give me the information. May be outdated.
    };
    struct ComputerAvailability {
        ComputerAvailabilityState state = ComputerAvailabilityState::EMPTY;
        Date date_state = 0; // when the state has changed to this one, only keep the most recent one.
        Date date_information = 0; // when we get the information. Can be useful to evict old information that hasn't been refreshed since a long time.
    };
    struct Availability {
        // where it's available, with the date of the last check.
        std::unordered_map<ComputerId, ComputerAvailability> available;
        //number of times it was read locally. High number means that it's useful to keep this file here.
        size_t nb_read_local = 0;
        Date date_last_local_read = 0;
        ComputerId current_best_path = NO_COMPUTER_ID; //Note: it may be useful to also store the peerid?
    };

    struct Invalidation {
        ComputerId notifier; // the computer that notified us.
        Date date_notified; // when we were notified of this invalidation.
        Date date_invalidation; // when the invalidation was created.
        FsCommitID commit; // the new commit that invalidated the elt. the cid inside is the modifier
    };

    class SynchroDb : public AbstractMessageManager, public std::enable_shared_from_this<SynchroDb> {
        friend FsExternalInterface; //TODO: remove that and use this SynchroDB class as gateway between FsExternalInterface and fsManager (for mutex exclusion, temp storage of chunks, and some cache?)
    protected: 

        bool need_to_save = false;

        /// <summary>
        /// The current state of the filesystem has taken into account these commits.
        /// </summary>
        std::unordered_map<ComputerId, SynchState> m_current_states;
        std::mutex m_current_states_mutex;

        // what is available where
        std::unordered_map<FsID, Availability> m_known_availability;
        std::mutex m_known_availability_mutex;

        /// <summary>
        /// our stats about the other peers, to know if they are reliable
        /// </summary>
        std::unordered_map<ComputerId, ComputerState> m_computer_states;
        std::mutex m_computer_states_mutex;

        static inline SynchState NO_SYNC_STATE = SynchState{};
        static inline ComputerState NO_COMPUTER_STATE = ComputerState{};
        

        /// <summary>
        /// how to size chunks when creating ones.
        /// </summary>
        std::shared_ptr<ChunkSizer> m_chunk_sizer;


        /// <summary>
        /// These elements are modified by another peer. Please fetch before using one of these.
        /// note: you don't need to serialize these, as evrythign can be invalidated when we're not connected
        /// </summary>
        std::unordered_map<FsID, Invalidation> m_invalidated;
        std::mutex m_invalidated_mutex;

        // ------ notification for invalidation -----
        std::mutex m_newly_invalidated_by_mutex;
        std::unordered_map<FsID, Invalidation> m_newly_invalidated;
        DateTime m_invalidation_last_sent;
        DateTime m_invalidation_wait_since_to_send;

        // ------ changes that I should send to my peers (before closing, or each minute) ------
        // they should already be notified that i changed it.
        std::mutex m_new_stuff_mutex;
        std::vector<FsID> m_new_stuff_by_me;

        // ------ managers -------

        /// <summary>
        /// The ditributed network we use to communicate with other peers.
        /// </summary>
        std::shared_ptr<ClusterManager> m_network;

        /// <summary>
        ///  the place where we store our local copy of the filesystem we synch.
        /// </summary>
        std::shared_ptr<FsStorage> m_file_storage;
        std::recursive_mutex m_file_storage_mutex;


        std::shared_ptr<SynchTreeMessageManager> m_synch_tree;

        std::shared_ptr<ExchangeChunkMessageManager> m_synch_chunk;

        SynchroDb() {}
    public:
        //factory
        [[nodiscard]] static std::shared_ptr<SynchroDb> create() {
            std::shared_ptr<SynchroDb> pointer = std::shared_ptr<SynchroDb>{ new SynchroDb(/*TODO*/) };
            return pointer;
        }

        std::shared_ptr<SynchroDb> ptr() {
            return shared_from_this();
        }

#ifdef _DEBUG
        static inline bool debug_barrier = false;
#endif
        static inline const Date MAX_RETENTION_DELETION_INFO = 60 * 24 * 30; // after 30 days, remove entries 'i don't have the chunk anymore'

        void init(std::shared_ptr<FsStorage> file_storage, std::shared_ptr<ClusterManager> network);

        /// <summary>
        /// It will create the different message manager and link them to the network and the file storage.
        /// kind of init.
        /// </summary>
        void launch();

        void receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) override;

        /// <summary>
        /// has to be call some times per minute.
        /// It will fetch on accessible servers for latest updates, if no push have been received since a 'long' time.
        /// It will also emit push updates, or ping/ttl if nothing to share.
        /// </summary>
        void update(DateTime current);

        /// <summary>
        /// Update: push fetch answer to neighbor about unfetch changes.
        /// Peers should be smart enough to fetch changes when i send them invalidation, but just in case somthing went wrong.
        /// </summary>
        /// <param name="current"></param>
        void push_modifications(DateTime current);

        /// <summary>
        /// received a fetch, update our 'last_fetch_date' for this ComputerId
        /// </summary>
        /// <param cid=""></param>
        void updateFetchTime(ComputerId cid);

        /// <summary>
        /// When we fetch data, we receive FsElt with only the last commits that should be useful for us. (it shouldn't send data much older than what we have).
        /// The object only have the most basic implementation (ids of commits with ids of content, with dates).
        /// This method use this information to modify our own implementation of the FsStorage to insert the data we diddn't known yet.
        /// Note: we may only get the state of the last commit (for each object) since the last fetch. Each server have an incomplete knowledge of the past commit, as these
        ///  can be destroyed at will to avoid clutter. The important thing is that a commit id is unique and the state of the FsElt for his commit is the same evrywhere.
        /// </summary>
        /// <param name="new_commit"></param>
        /// <param name="commit_time"></param>
        /// <param name="modifier">The computer that has created this commit/revision</param>
        //bool mergeCommit_deprecated(ComputerId from, const FsElt& to_merge, const std::unordered_map<FsID, const FsElt*>& extra_db, ComputerId modifier = NO_COMPUTER_ID);
        bool mergeCommit(ComputerId from, const std::vector<const FsElt*>& to_merge, ComputerId modifier = NO_COMPUTER_ID);

        /// <summary>
        /// Ensure that this element is up to date (for file & dir) and locally accessible (for chunk, but you should use retreiveChunks instead).
        /// When the local filesystem is updated, it will call the callback with the updated (if any) element.
        /// It will call with an empty ptr if there is the elmeent doesn't exist. If it was deleted, it will have its 'date_deleted' set, so don't only check for existence.
        /// TODO: a promise version of it?
        /// TODO: remove the answer compoenent of the callback (it's not used)?
        /// </summary>
        /// <param name="elt_to_get">Id of the element to refresh</param>
        /// <param name="callback">Callback to call when the element is refreshed.</param>
        void askForFsSynch(FsID elt_to_get, std::function<void(FsEltPtr answer)> callback);

        /// <summary>
        /// Retreive and store (at least temporary) the all chunk of 'file' to be able to read at least 'min_size' bytes.
        /// </summary>
        /// <param name="file">object file that contains current chunks ids to fetch</param>
        /// <param name="size_min">chunks ids to fetch</param>
        /// <param name="calback">callback to call when chunks are available via filesystem call. with false if the fetch wasn't possible.</param>
        void retreiveChunks(FsFilePtr file, std::vector<FsID> chunks_id_to_retreive, std::function<void(const std::vector<FsChunkPtr>&)> calback);

        void notifyObjectChanged(FsID id);

        void addInvalidatedElements(const std::vector<FsID>& invadidated, const std::vector<FsCommitID>& commits, ComputerId sender, ComputerId modifier, DateTime last_modif_time);
        void removeInvalidatedElements(const std::vector<FsID>& not_invadidated_anymore);
        bool hasInvalidation();
#ifdef _DEBUG
        bool isInvalidated(FsID to_test);
#endif
        Invalidation getInvalidation(FsID to_test);
        ComputerId notifierInvalidation(FsID to_test);

        void load(std::filesystem::path& file);
        void save(std::filesystem::path& file);


        ComputerId getComputerId() {
            ComputerId cid = m_network->getComputerId();
            if (cid == 0) return NO_COMPUTER_ID;
            return cid;
        }
        std::unordered_set<ComputerId> getComputerIds() {
            std::unordered_set<ComputerId> cids;
            {
                std::lock_guard lock{ m_computer_states_mutex };
                for (auto& cs : m_computer_states) {
                    cids.insert(cs.first);
                }
            }
            {
                std::lock_guard lock{ m_current_states_mutex };
                for (auto& cs : m_current_states) {
                    cids.insert(cs.first);
                }
            }
            return cids;
        }
        ComputerState getComputerState(ComputerId cid) {
            std::lock_guard lock{ m_computer_states_mutex };
            if (auto cstate = m_computer_states.find(cid); cstate != m_computer_states.end()) {
                return cstate->second;
            }
            return SynchroDb::NO_COMPUTER_STATE;
        }
        SynchState getSynchState(ComputerId cid) {
            std::lock_guard lock{ m_current_states_mutex };
            if (auto cstate = m_current_states.find(cid); cstate != m_current_states.end()) {
                return cstate->second;
            }
            return SynchroDb::NO_SYNC_STATE;
        }
        void notifyPushElement(ComputerId cid, FsID elt_id) {
            std::lock_guard lock{ m_current_states_mutex };
            if (auto cstate = m_current_states.find(cid); cstate != m_current_states.end()) {
                cstate->second.unpushed_elts.erase(elt_id);
            }
        }
        //return a copy, to be sure it's not invalidated by getAvailability (as it can create things) by another thread
        Availability getAvailability(FsID id) {
            std::lock_guard lock{ m_known_availability_mutex };
            return m_known_availability[id];
        }
        void setAvailabilityMetadata(FsID id, const Availability& availability) {
            std::lock_guard lock{ m_known_availability_mutex };
            m_known_availability[id].current_best_path = availability.current_best_path;
            m_known_availability[id].nb_read_local = availability.nb_read_local;
            m_known_availability[id].date_last_local_read = availability.date_last_local_read;
        }
        //return a copy, to be sure it's not invalidated by setAvailability by another thread
        std::optional<ComputerAvailability> getAvailability(FsID id, ComputerId cid) {
            std::lock_guard lock{ m_known_availability_mutex };
            const Availability& ava = m_known_availability[id];
            auto it = ava.available.find(cid);
            return (it == ava.available.end()) ? std::nullopt : std::optional(it->second);
        }
        void setAvailability(FsID id, ComputerId cid, ComputerAvailability availability) {
            std::lock_guard lock{ m_known_availability_mutex };
            m_known_availability[id].available[cid] = availability;
        }

        std::recursive_mutex& fsSynchronize() { return m_file_storage_mutex; }

#ifdef _DEBUG
        std::unordered_map<FsID, Invalidation> get_test_wait_current_invalidation() {
            return m_newly_invalidated;
        }

        void test_emit_invalidation_quick() {
            m_invalidation_wait_since_to_send = 0;
        }
        std::shared_ptr<ExchangeChunkMessageManager> test_chunkManager() { return m_synch_chunk; }
        std::shared_ptr<SynchTreeMessageManager> test_treeManager() { return m_synch_tree; }
#endif
    };




}