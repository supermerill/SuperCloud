#pragma once

//to define ComputerId
#include "network/ClusterManager.hpp"

#include "utils/ByteBuff.hpp"

#include <filesystem>

//TODO: time manager to get it from it.

namespace supercloud {
    class FsDirectory;
    class FsFile;
    class FsChunk;
    class FsObject;
    class FsElt;
    enum class FsType : uint8_t {
        NONE = 0, //used for commit when it's not a creation
        CHUNK = 1,
        FILE = 2,
        DIRECTORY = 3
    };

    typedef std::shared_ptr<FsChunk> FsChunkPtr;
    typedef std::shared_ptr<FsFile> FsFilePtr;
    typedef std::shared_ptr<FsDirectory> FsDirPtr;
    typedef std::shared_ptr<FsObject> FsObjectPtr;
    typedef std::shared_ptr<FsElt> FsEltPtr;
    typedef uint64_t FsID;

    // rights for computer - user - group - all
    // each group of four bits have (unused) - read - write - execute
    typedef uint16_t CUGA;
    constexpr CUGA CUGA_7777 = CUGA(0x7777);
    constexpr uint16_t CUGA_CR = 0x4000;  // read permission, owner
    constexpr uint16_t CUGA_CW = 0x2000;  // write permission, owner
    constexpr uint16_t CUGA_CX = 0x1000;  // execute/search permission, owner
    constexpr uint16_t CUGA_UR = 0x0400;  // read permission, owner
    constexpr uint16_t CUGA_UW = 0x0200;  // write permission, owner
    constexpr uint16_t CUGA_UX = 0x0100;  // execute/search permission, owner
    constexpr uint16_t CUGA_GR = 0x0040;  // read permission, group
    constexpr uint16_t CUGA_GW = 0x0020;  // write permission, group
    constexpr uint16_t CUGA_GX = 0x0010;  // execute/search permission, group
    constexpr uint16_t CUGA_AR = 0x0004;  // read permission, other
    constexpr uint16_t CUGA_AW = 0x0002;  // write permission, other
    constexpr uint16_t CUGA_AX = 0x0001;  // execute permission, other

    /// <summary>
    /// If it has a chunk id > 0, then raw_data should be nullptr and raw_data_size 0.
    /// If raw_data is used, then it should point to a valid uint8_t buffer of at least raw_data_size entry (>0), and chunk_id and chunk_id_size should be set to 0.
    /// chunk_size can be 0 even if chunk_id isn't because it's possible to not know the size of a speciifc chunk. But the sum of all chunk size must be correct.
    /// </summary>
    struct ChunkOrRawData {
        const FsID chunk_id;
        const size_t chunk_size;
        const uint8_t* raw_data;
        const size_t raw_data_size;
    };

    class FsStorage {
    protected:
        ComputerId m_cid = NO_COMPUTER_ID;

        std::recursive_mutex m_write_mutex;

        virtual FsID getNextId(FsType type) = 0;
    public:
        FsStorage(ComputerId my_id) : m_cid(my_id) {}

        ComputerId getMyComputerId() { assert(m_cid > 0 && m_cid != NO_COMPUTER_ID); return m_cid; }
        void setMyComputerId(ComputerId id) {
            assert(m_cid == 0 || m_cid == NO_COMPUTER_ID);
            m_cid = id;
        }

        //the root is always the id 3 (a directory with id 0 from computer 0).
        FsID getRoot() const;

        /// <summary>
        /// lock if you want to modify the filesystem.
        /// </summary>
        /// <returns></returns>
        std::recursive_mutex& synchronize() { return m_write_mutex; }

        //get elts
        virtual bool hasLocally(FsID id) = 0;
        virtual FsEltPtr load(FsID id) = 0;
        //specialized load. useful to reimplement if the storage is separate. 
        virtual FsChunkPtr loadChunk(FsID id);
        virtual FsFilePtr loadFile(FsID id);
        virtual FsDirPtr loadDirectory(FsID id);
        //create / modify elts
        // ==================================== chunk ====================================
        
        /// <summary>
        /// Add a new chunk to the end of the file.
        /// </summary>
        /// <param name="file"></param>
        /// <param name="data"></param>
        /// <returns>new chunk</returns>
        virtual FsChunkPtr addChunkToFile(FsFilePtr file, uint8_t* new_data, size_t new_data_size) = 0;

        /// <summary>
        /// Create a new chunk, and place it at the position of 'old_chunk' inside old_chunk's file.
        /// If the buffer is empty, the chunk is deleted.
        /// </summary>
        /// <param name="old"></param>
        /// <param name="new_data"></param>
        /// <returns>new chunk, or nullptr if it's a deletion</returns>
        virtual FsChunkPtr modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, uint8_t* new_data, size_t new_data_size) = 0;

        /// <summary>
        /// Change many chunks at the same time
        /// note: chunk_id s from new_chunk are assumed to be already inside this file, and so no "parent" id is added to them.
        /// </summary>
        /// <param name="file"></param>
        /// <param name="new_chunks"></param>
        virtual void modifyFile(FsFilePtr file, std::vector<ChunkOrRawData> new_chunks) = 0;

        // ==================================== file ====================================

        /// <summary>
        /// Create a new file inside a directory.
        /// You can't modify the name/right of a file, you have to create a new one (and replace the previous one)
        /// </summary>
        /// <param name="directory">where to create it</param>
        /// <param name="old_file">if not null_ptr, replace this file</param>
        /// <param name="name"></param>
        /// <param name="chunks"></param>
        /// <param name="rights"></param>
        /// <returns>the new file</returns>
        virtual FsFilePtr createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777, FsFilePtr from = {}) = 0;

        /// <summary>
        /// Create a new file inside a directory, replacing another one.
        /// You can't modify the name/right of a file, you have to create a new one (and replace the previous one)
        /// if the name & rights haven't changed, then you can use the other method.
        /// </summary>
        /// <param name="old_file">replace this file inside the directory</param>
        /// <param name="name">new name</param>
        /// <param name="chunks"></param>
        /// <param name="rights"></param>
        /// <returns>the new file</returns>
        virtual FsFilePtr modifyFile(FsFilePtr old_file, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777) = 0;

        // ==================================== directory ====================================

        /// <summary>
        /// 
        /// </summary>
        /// <param name="directory">where to create it</param>
        /// <param name="name"></param>
        /// <param name="data"></param>
        /// <param name="rights"></param>
        /// <returns>the new directory</returns>
        virtual FsDirPtr createNewDirectory(FsDirPtr directory, const std::string& name, std::vector<FsObjectPtr> data = {}, CUGA rights = CUGA_7777, FsDirPtr from = {}) = 0;
        virtual FsDirPtr createNewDirectory(FsDirPtr directory, const std::string& name, const std::vector<FsID>& data = {}, CUGA rights = CUGA_7777, FsDirPtr from = {}) = 0;

        /// <summary>
        /// Delete a file / directory
        /// </summary>
        /// <param name="old_file">delete this object form the directory</param>
        virtual void deleteObject(FsObjectPtr old_file) = 0;

        virtual void serialize(const std::filesystem::path& file) = 0;
        virtual void deserialize(const std::filesystem::path& file) = 0;

        /// ---=== methods for synch ===--- ///
        
        /// <summary>
        /// ask this filesystem to create a new file/state from given information.
        /// </summary>
        /// <param name="new_commit">stub object (hence why it's not a smart pointer: it's a temporary object). 
        /// If the associated object isn't created, a new implementation object is created.</param>
        /// <returns>true if it uses the new state</returns>
        virtual bool mergeFileCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) = 0;

        /// <summary>
        /// ask this filesystem to create a new dir/state from given information.
        /// </summary>
        /// <param name="new_commit">stub object. If the associated object isn't created, a new implementation object is created.</param>
        /// <returns>true if it uses the new state</returns>
        virtual bool mergeDirectoryCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) = 0;

        /// <summary>
        /// check that the filesystem has no unknown ids
        /// check that all directory chains go to the root (no loop).
        /// check that the commits are ordered by time.
        /// check that the commit chain allow to go to the current state.
        /// </summary>
        /// <returns>true if there is no error detected/fixed</returns>
        virtual bool checkFilesystem() = 0;

    };
}
