#pragma once

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"
#include "utils/Utils.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace supercloud {
    class FsDirectoryInMemory;
    class FsChunkInMemory;
    class FsSerializableInMemory {
    public:
        //virtual void serialize(ByteBuff& buffer) = 0;
        static FsEltPtr deserialize(ByteBuff& buffer);
        static void serialize(FsEltPtr obj, ByteBuff& buffer);
    };

    class FsObjectInMemory {
    protected:
    public:
        std::shared_ptr<FsDirectoryInMemory> m_loaded_parent;
        virtual void remove(DateTime time, FsID renamed_to) = 0;
        void set_loaded_parent(std::shared_ptr<FsDirectoryInMemory> parent);
    };
    class FsFileInMemory : public FsFile, public FsSerializableInMemory, public FsObjectInMemory{
    public:
        FsFileInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent);
        static void serialize(FsFileInMemory* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsFileInMemory> deserialize(FsID id, ByteBuff& buffer);
        void addChunk(std::shared_ptr<FsChunkInMemory> new_chunk);
        void replaceChunk(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk);
        void replaceContent(const std::vector<FsID>& new_content, std::vector<std::pair<FsID, FsID>> commit, FsID commit_id, DateTime commit_time);
        virtual void remove(DateTime time, FsID renamed_to) override;
        virtual void accept(void* visitor) {}

        /**
         * Perform deletion operation : delete sub-thing, delete content, delete entry in fs.
         * It's final and irrecoverable. If you want just mark this as "deleted", use removeDir/removeFile/removeChunk instead.
         */
        virtual void remove() {}

        /**
         * Call this when a changes occur on this element. It updates the ModifyDate.
         */
        virtual void changes() {}

        /**
         * Notify that modifications on this object are finished and now should be saved / transmitted.
         */
        virtual void flush() {}
    };
    class FsDirectoryInMemory : public FsDirectory, public FsSerializableInMemory, public FsObjectInMemory {
    public:
        FsDirectoryInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent);
        void notifyModificationChained(FsID last_commit_id, DateTime date);
        void addFile(std::shared_ptr<FsFileInMemory> file);
        void addDir(std::shared_ptr<FsDirectoryInMemory> file);
        void setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time);
        void delFile(FsID to_del, FsID commit_id, DateTime commit_time);
        virtual void remove(DateTime time, FsID renamed_to) override;
        static void serialize(FsDirectoryInMemory* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsDirectoryInMemory> deserialize(FsID id, ByteBuff& buffer);
        virtual void accept(void* visitor) {}

        /**
         * Perform deletion operation : delete sub-thing, delete content, delete entry in fs.
         * It's final and irrecoverable. If you want just mark this as "deleted", use removeDir/removeFile/removeChunk instead.
         */
        virtual void remove() {}

        /**
         * Call this when a changes occur on this element. It updates the ModifyDate.
         */
        virtual void changes() {}

        /**
         * Notify that modifications on this object are finished and now should be saved / transmitted.
         */
        virtual void flush() {}
    };
    class FsStorageInMemory : public FsStorage {
    protected:
        std::unordered_map<FsID, FsEltPtr> database;
        std::atomic<uint64_t> m_id_generator;
        FsID m_root_id;

        virtual FsID getNextId() override;
    public:
        FsStorageInMemory(ComputerId my_id) : FsStorage(my_id) {}

        //get elts
        virtual bool hasLocally(FsID id) override;
        virtual FsEltPtr load(FsID id) override;
        virtual FsID getRoot() override;
        FsDirPtr createNewRoot();
        //create / modify elts
        // ==================================== chunk ====================================

        /// <summary>
        /// Add a new chunk to the end of the file.
        /// </summary>
        /// <param name="file"></param>
        /// <param name="data"></param>
        /// <returns>new chunk</returns>
        virtual FsChunkPtr addChunkToFile(FsFilePtr file, ByteBuff data) override;

        /// <summary>
        /// Create a new chunk, and place it at the position of 'old_chunk' inside old_chunk's file.
        /// If the buffer is empty, the chunk is deleted.
        /// </summary>
        /// <param name="old"></param>
        /// <param name="new_data"></param>
        /// <returns>new chunk, or nullptr if it's a deletion</returns>
        virtual FsChunkPtr modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, ByteBuff new_data) override;

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
        virtual FsFilePtr createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks = {}, CUGA rights = CUGA_7777) override;

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
        virtual FsDirPtr createNewDirectory(FsDirPtr directory_parent, const std::string& name, std::vector<FsObjectPtr> data = {}, CUGA rights = CUGA_7777) override;

        /// <summary>
        /// Delete a file / directory
        /// </summary>
        /// <param name="old_file">delete this object form the directory</param>
        virtual void deleteObject(FsObjectPtr old_file) override;


        void save_all(const std::filesystem::path& file);

        void load_all(const std::filesystem::path& file);

        std::vector<FsDirPtr> getDirs(FsDirPtr dir);

        std::vector<FsFilePtr> getFiles(FsDirPtr dir);
    };
}
