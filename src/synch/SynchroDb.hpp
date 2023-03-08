#pragma once

#include "network/ClusterManager.hpp"
#include "fs/base/FsStorage.hpp"
#include "fs/base/FsElt.hpp"
#include "utils/Utils.hpp"

#include <filesystem>
#include <unordered_map>
#include <optional>

namespace supercloud {

    struct SynchState {
        //this is the synch state for this computer.
        ComputerId id;
        // last commit created on the computer
        FsID last_commit;
        // date of the commit (cache to avoid calling the fs)
        DateTime last_commit_date;
        // date of the moment we where notified of it.
        DateTime last_commit_received_date;
        // date of last itme we ask for a refresh of our knownledge.
        DateTime last_fetch_date;
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
        // wher eit's available, with the date of the last check.
        std::unordered_map<ComputerId, ComputerAvailability> available;
        //number of times it was read locally. High number means that it's useful to keep this file here.
        size_t nb_read_local = 0;
        Date date_last_local_read = 0;
        ComputerId current_best_path = NO_COMPUTER_ID; //Note: it may be useful to also store the peerid?
    };

    class SynchroDb {
    protected:

        bool need_to_save = false;

        /// <summary>
        /// The current state of the filesystem has taken into account these commits.
        /// </summary>
        std::unordered_map<ComputerId, SynchState> m_current_states;

        // what is available where
        std::unordered_map<FsID, Availability> m_known_availability;
        std::mutex m_known_availability_mutex;

        /// <summary>
        /// our stats about the other peers, to know if they are reliable
        /// </summary>
        std::unordered_map<ComputerId, ComputerState> m_computer_states;

        /// <summary>
        ///  the place where we store our local copy of the filesystem we synch.
        /// </summary>
        std::shared_ptr<FsStorage> m_file_storage;

        /// <summary>
        /// The ditributed network we use to communicate with other peers.
        /// </summary>
        std::shared_ptr<ClusterManager> m_network;



    public:

        static inline Date MAX_RETENTION_DELETION_INFO = 60 * 24 * 30; // after 30 days, remove entries 'i don't have the chunk anymore'

        void init(std::shared_ptr<FsStorage> file_storage, std::shared_ptr<ClusterManager> network) {
            m_network = network;
            m_file_storage = file_storage;
        }

        /// <summary>
        /// It will create the different message manager and link them to the network and the file storage.
        /// kind of init.
        /// </summary>
        void launch();


        /// <summary>
        /// has to be call some times per minute.
        /// It will fetch on accessible servers for latest updates, if no push have been received since a 'long' time.
        /// It will also emit push updates, or ping/ttl if nothing to share.
        /// </summary>
        void update();

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
        bool mergeCommit(ComputerId from, const FsElt& to_merge, const std::unordered_map<FsID, const FsElt*>& extra_db);

        void load(std::filesystem::path& file);
        void save(std::filesystem::path& file);


        ComputerId getComputerId() {
            ComputerId cid = m_network->getComputerId();
            if (cid == 0) return NO_COMPUTER_ID;
            return cid;
        }
        const ComputerState& getComputerState(ComputerId cid) {
            if (auto cstate = m_computer_states.find(cid); cstate != m_computer_states.end()) {
                return cstate->second;
            }
            return {};
        }
        const SynchState& getSynchState(ComputerId cid) {
            if (auto cstate = m_current_states.find(cid); cstate != m_current_states.end()) {
                return cstate->second;
            }
            return {};
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
    };

    class FsExternalInterface {
    protected:
        std::shared_ptr<SynchroDb> m_db;

    public:

        struct RequestAnswer {
            uint16_t error_code; // 0 if no error TODO:enum
            std::string error_message; // empty if no error
        };

        struct ObjectRequestAnswer : RequestAnswer {
            bool is_file;
            FsObjectPtr object; //empty if error
        };
        /// <summary>
        /// request a fs object at a path.
        /// </summary>
        /// <param name="path"></param>
        /// <returns></returns>
        std::future<ObjectRequestAnswer> get(const std::filesystem::path& path);
        std::future<ObjectRequestAnswer> get(FsID id);
        std::future<ObjectRequestAnswer> contains(FsDirPtr dir, const std::string& name);

        //used to get the content of a directory
        struct MultiObjectRequestAnswer : RequestAnswer {
            std::vector<FsObjectPtr> objects;
        };
        std::future<MultiObjectRequestAnswer> getAll(std::vector<std::filesystem::path> paths);
        std::future<MultiObjectRequestAnswer> getAll(std::vector<FsID> ids);

        /// <summary>
        /// request data from a file oebjct
        /// </summary>
        /// <param name="path"></param>
        /// <returns></returns>
        std::future<RequestAnswer> getData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size);
        std::future<RequestAnswer> writeData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size);
        std::future<ObjectRequestAnswer> resize(FsFilePtr file, size_t new_size);

        /// <summary>
        /// no future yet? (it's not needed?) Maybe it's more future-proof to use them anyway...
        /// </summary>
        /// <param name="obj"></param>
        /// <returns></returns>
        int createFile(FsDirPtr parent_dir, const std::string& name, CUGA rights);
        int createDirectory(FsDirPtr parent_dir, const std::string& name, CUGA rights);
        int removeObject(FsObjectPtr obj);
        int moveObject(FsObjectPtr obj, const std::filesystem::path& new_path);
        int setCUGA(FsObjectPtr obj, CUGA rights);

        ComputerId getOurComputerId() { return m_db->getComputerId(); }
        uint32_t getUserId(ComputerId cid) { return m_db->getComputerState(cid).user_id; }

    };




}