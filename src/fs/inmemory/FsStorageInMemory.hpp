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

    class Clock {
    public:
        virtual DateTime getCurrrentTime() = 0;
    };

    class FsSerializableInMemory {
    public:
        //virtual void serialize(ByteBuff& buffer) = 0;
        static FsEltPtr deserialize(ByteBuff& buffer);
        static void serialize(FsEltPtr obj, ByteBuff& buffer);
    };

    class FsObjectInMemory {
    protected:
        uint16_t computeDepth() const;
    public:
        std::shared_ptr<FsDirectoryInMemory> m_loaded_parent;
        virtual void remove(DateTime time, FsID renamed_to) = 0;
        void set_loaded_parent(std::shared_ptr<FsDirectoryInMemory> parent);
    };

    class FsStorageInMemory : public FsStorage {
    protected:
        std::unordered_map<FsID, FsEltPtr> m_database;
        std::atomic<uint64_t> m_id_generator;

        std::shared_ptr<Clock> m_clock;

        virtual FsID getNextId(FsType type) override;
    public:
        FsStorageInMemory(ComputerId my_id, std::shared_ptr<Clock> clock) : FsStorage(my_id), m_clock(clock){}

        //get elts
        virtual bool hasLocally(FsID id) override;
        virtual FsEltPtr load(FsID id) override;
        FsDirPtr createNewRoot();
        //create / modify elts
        // ==================================== chunk ====================================

        /// <summary>
        /// Add a new chunk to the end of the file.
        /// </summary>
        /// <param name="file"></param>
        /// <param name="data"></param>
        /// <returns>new chunk</returns>
        virtual FsChunkPtr addChunkToFile(FsFilePtr file, uint8_t* new_data, size_t new_data_size) override;

        /// <summary>
        /// Create a new chunk, and place it at the position of 'old_chunk' inside old_chunk's file.
        /// If the buffer is empty, the chunk is deleted.
        /// </summary>
        /// <param name="old"></param>
        /// <param name="new_data"></param>
        /// <returns>new chunk, or nullptr if it's a deletion</returns>
        virtual FsChunkPtr modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, uint8_t* new_data, size_t new_data_size) override;

        /// <summary>
        /// Change many chunks at the same time
        /// </summary>
        /// <param name="file"></param>
        /// <param name="new_chunks"></param>
        virtual void modifyFile(FsFilePtr file, std::vector<ChunkOrRawData> new_chunks) override;

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
        virtual FsFilePtr createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777, FsFilePtr from = {}) override;

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
        virtual FsFilePtr modifyFile(FsFilePtr old_file, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777) override;

        // ==================================== directory ====================================

        /// <summary>
        /// 
        /// </summary>
        /// <param name="directory">where to create it</param>
        /// <param name="name"></param>
        /// <param name="data"></param>
        /// <param name="rights"></param>
        /// <returns>the new directory</returns>
        virtual FsDirPtr createNewDirectory(FsDirPtr directory_parent, const std::string& name, std::vector<FsObjectPtr> data = {}, CUGA rights = CUGA_7777, FsDirPtr from = {}) override;
        virtual FsDirPtr createNewDirectory(FsDirPtr directory_parent, const std::string& name, const std::vector<FsID>& data, CUGA rights = CUGA_7777, FsDirPtr from = {}) override;

        /// <summary>
        /// Delete a file / directory
        /// </summary>
        /// <param name="old_file">delete this object form the directory</param>
        virtual void deleteObject(FsObjectPtr old_file) override;


        virtual void serialize(const std::filesystem::path& file) override;

        virtual void deserialize(const std::filesystem::path& file) override;

        virtual bool checkFilesystem() override;

        std::vector<FsDirPtr> getDirs(FsDirPtr dir);

        std::vector<FsFilePtr> getFiles(FsDirPtr dir);

        /// <summary>
        /// 
        /// </summary>
        /// <param name="new_commit"></param>
        /// <param name="extra_db"></param>
        /// <returns>true if the file now exists</returns>
        virtual bool mergeFileCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) override;
        virtual bool mergeDirectoryCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) override;
        bool mergeObjectCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db);
        //TODO: optimization for mergeCommits(const std::unordered_map<FsID, const FsElt*>& commits), by 'consuming' commits when done in recursive mode.

    protected:
        //size_t createNewFileMergeCommit(FsID file_id, FsObjectCommit& commit, const std::vector<FsID>& old_chunks, const std::vector<FsID>& new_chunks);
    };
}
