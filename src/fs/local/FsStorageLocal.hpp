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
    class FsDirectoryLocal;
    class FsChunkLocal;

    class FsObjectLocal {
    protected:
        std::filesystem::path m_real_file_path;
    public:
        std::shared_ptr<FsDirectoryLocal> m_loaded_parent;
        virtual void remove(DateTime time, FsID renamed_to, const std::filesystem::path& new_path) = 0;
        void set(std::shared_ptr<FsDirectoryLocal> parent, const std::filesystem::path& new_path);
        std::filesystem::path getRealPath() { return m_real_file_path; }

    };
    class FsStorageLocal : public FsStorage {
    protected:
        std::unordered_map<FsID, FsEltPtr> database;
        std::atomic<uint64_t> m_id_generator;
        FsID m_root_id;
        std::filesystem::path m_root_dir;
        std::filesystem::path m_deleted_dir;

        virtual FsID getNextId() override;
        std::string remove_root_dir_prefix(const std::filesystem::path&);
    public:
        FsStorageLocal(ComputerId my_id, std::filesystem::path root_dir, std::filesystem::path deleted_dir) : FsStorage(my_id),
            m_root_dir(root_dir), m_deleted_dir(deleted_dir) {}

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
        static FsEltPtr deserialize(ByteBuff& buffer, FsStorageLocal& database);
        static void serialize(FsEltPtr obj, ByteBuff& buffer);

        std::vector<FsDirPtr> getDirs(FsDirPtr dir);

        std::vector<FsFilePtr> getFiles(FsDirPtr dir);
    };
}
