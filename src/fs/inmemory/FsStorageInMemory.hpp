#pragma once

#include "fs/base/FsStorage.hpp"
#include "utils/Utils.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace supercloud {
    class FsDirectoryInMemory;
    class FsFileInMemory;
    class FsChunkInMemory;
    struct FsObjectCommit;

    class FsSerializableInMemory {
    public:
        static FsEltPtr deserialize(ByteBuff& buffer);
        static void serialize(FsEltPtr obj, ByteBuff& buffer);
    };

    // to allow FsDirectoryInMemory::notifyModificationChained to update our database
    class FsStorageInMemory;
    class FsUpdater {
        FsStorageInMemory* m_database;
    public:
        FsUpdater(FsStorageInMemory* database) : m_database(database) {}
        void updateEltPtr(FsEltPtr ptr);
        FsObjectPtr getObjPtr(FsID id);
    };

    class FsStorageInMemory : public FsStorage {
        friend FsUpdater;
    protected:
        std::unordered_map<FsID, FsEltPtr> m_database;
        std::mutex m_database_mutex;

        std::atomic<uint64_t> m_id_generator;

        std::shared_ptr<Clock> m_clock;

        virtual FsID getNextId(FsType type) override;
        void updateEltPtr(FsEltPtr ptr);
        FsUpdater m_updater;

    public:
        FsStorageInMemory(ComputerId my_id, std::shared_ptr<Clock> clock) : FsStorage(my_id), m_clock(clock), m_updater(this) {}

        //get elts
        virtual bool hasLocally(FsID id) override;
        virtual FsEltPtr load(FsID id) override;
        FsDirPtr createNewRoot();
        //create / modify elts
        // ==================================== chunk ====================================

        /// <summary>
        /// Add a new chunk to the end of the file.
        /// </summary>
        /// <param name="file">The file in which a chunk is added. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="data"></param>
        /// <returns>new chunk</returns>
        virtual FsChunkPtr addChunkToFile(FsFilePtr& file, uint8_t* new_data, size_t new_data_size) override;

        /// <summary>
        /// Create a new chunk, and place it at the position of 'old_chunk' inside old_chunk's file.
        /// If the buffer is empty, the chunk is deleted.
        /// </summary>
        /// <param name="file">The file in which a chunk is modified. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="new_data"></param>
        /// <returns>new chunk, or nullptr if it's a deletion</returns>
        virtual FsChunkPtr modifyChunk(FsFilePtr& file, FsChunkPtr old_chunk, uint8_t* new_data, size_t new_data_size) override;

        /// <summary>
        /// Change many chunks at the same time
        /// </summary>
        /// <param name="file">The file modified. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="new_chunks"></param>
        virtual void modifyFile(FsFilePtr& file, std::vector<ChunkOrRawData> new_chunks) override;
    protected:
        void fillFile(FsFilePtr& file, FsObjectCommit& commit, std::vector<ChunkOrRawData> new_chunks);
    public:
        // ==================================== file ====================================

        /// <summary>
        /// Create a new file inside a directory.
        /// You can't modify the name/right of a file, you have to create a new one (and replace the previous one)
        /// </summary>
        /// <param name="directory">The directory in which a file is added. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="name"></param>
        /// <param name="chunks"></param>
        /// <param name="rights"></param>
        /// <returns>the new file</returns>
        virtual FsFilePtr createNewFile(FsDirPtr& directory, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777, uint32_t group_id = 0, FsFilePtr from = {}) override;

        /// <summary>
        /// Create a new file inside a directory, replacing another one.
        /// You can't modify the name/right of a file, you have to create a new one (and replace the previous one)
        /// if the name & rights haven't changed, then you can use the other method.
        /// </summary>
        /// <param name="old_file">The file modified. The pointer may be refresh to the refreshed version, event if this event is strongly discouraged.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="name">new name</param>
        /// <param name="chunks"></param>
        /// <param name="rights"></param>
        /// <returns>The new file, with a new id.</returns>
        virtual FsFilePtr modifyFile(FsFilePtr& old_file, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777, uint32_t group_id = 0) override;

        // ==================================== directory ====================================

        /// <summary>
        /// 
        /// </summary>
        /// <param name="directory">The directory in which a directory is added. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="name"></param>
        /// <param name="data"></param>
        /// <param name="rights"></param>
        /// <returns>the new directory</returns>
        virtual FsDirPtr createNewDirectory(FsDirPtr& directory_parent, const std::string& name, std::vector<FsObjectPtr> data = {}, CUGA rights = CUGA_7777, uint32_t group = 0, FsDirPtr from = {}) override;
        virtual FsDirPtr createNewDirectory(FsDirPtr& directory_parent, const std::string& name, const std::vector<FsID>& data, CUGA rights = CUGA_7777, uint32_t group = 0, FsDirPtr from = {}) override;

        /// <summary>
        /// Create a new directory inside a directory, replacing another one.
        /// You can't modify the name/right of a file, you have to create a new one (and replace the previous one)
        /// if the name & rights haven't changed, then you can use the other method.
        /// </summary>
        /// <param name="old_file">The directory in which a directory is added. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        /// <param name="name">new name</param>
        /// <param name="chunks"></param>
        /// <param name="rights"></param>
        /// <returns>the new directory</returns>
        virtual FsDirPtr modifyDirectory(FsDirPtr& old_file, const std::string& name, const std::vector<FsID>& data = {}, CUGA rights = CUGA_7777, uint32_t group_id = 0, FsDirPtr from = {}) override;

        /// <summary>
        /// Delete a file / directory
        /// </summary>
        /// <param name="old_file">The object deleted. The pointer will be updated to the new object.
        /// Please ensure you lock the access to this resources and have refreshed this file to have the latest version</param>
        virtual void deleteObject(FsObjectPtr& old_file) override;
    protected:
        void deleteObject(FsObjectPtr& old_file, FsDirPtr& parent_directory, FsID commit_id, DateTime commit_time, FsID renamed_to = 0);
    public:

        virtual void serialize(const std::filesystem::path& file) override;

        virtual void deserialize(const std::filesystem::path& file) override;

        virtual bool checkFilesystem() override;

        std::vector<FsDirPtr> getDirs(FsDirPtr dir);

        std::vector<FsFilePtr> getFiles(FsDirPtr dir);

        /// <summary>
        /// merge filesystem data from a peer insode ours.
        /// </summary>
        /// <param name="new_commit">the commit to merge</param>
        /// <param name="extra_db">other directory & files that will be merged /had been merged</param>
        /// <returns>true if the object now exists</returns>
        //virtual bool mergeFileCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) override;
        //virtual bool mergeDirectoryCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) override;
        //bool mergeObjectCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db);
        virtual void mergeObjectsCommit(const std::vector<const FsObject*>& commit) override;
        //TODO: optimization for mergeCommits(const std::unordered_map<FsID, const FsElt*>& commits), by 'consuming' commits when done in recursive mode.

    protected:
        //size_t createNewFileMergeCommit(FsID file_id, FsObjectCommit& commit, const std::vector<FsID>& old_chunks, const std::vector<FsID>& new_chunks);

#ifdef _DEBUG
        public:
            std::unordered_map<FsID, FsEltPtr>& getDatabase() override { return m_database; }

#endif
    };
}
