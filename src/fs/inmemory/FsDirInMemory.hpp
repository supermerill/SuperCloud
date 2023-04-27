#pragma once

#include "fs/base/FsDirectory.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {
    class FsFileInMemory;

    class FsDirectoryInMemory : public FsDirectory, public FsSerializableInMemory {
    public:
        FsDirectoryInMemory(FsID id, DateTime date_creation, const std::string& name, CUGA puga, uint32_t group_id, FsID parent, uint16_t depth, FsID renamed_from, //const data
            const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, FsID last_modification_commit_id, DateTime last_modification_date) //mutable data
            : FsDirectory(id, date_creation, name, puga, group_id, parent, depth, renamed_from, current_state, commits, date_deleted, renamed_to, last_modification_commit_id, last_modification_date) {}
        FsDirectoryInMemory(const FsDirectory& old_dir, //const data
            const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, FsID last_modification_commit_id, DateTime last_modification_date) //mutable data
            : FsDirectory(old_dir, current_state, commits, date_deleted, renamed_to, last_modification_commit_id, last_modification_date) {}
        static void serialize(FsDirectoryInMemory* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsDirectoryInMemory> deserialize(FsID id, ByteBuff& buffer);
        void notifyModificationChained(FsUpdater& storage_updater, FsID last_commit_id, DateTime date);
    };
    typedef std::shared_ptr<FsDirectoryInMemory> FsDirectoryInMemoryPtr;

    class FsDirectoryInMemoryFactory : public FsDirectoryFactory {
    public:
        FsDirectoryInMemoryFactory(const FsDirectory* pattern) : FsDirectoryFactory(*pattern) {
        }

        FsDirectoryInMemoryFactory* addFile(std::shared_ptr<FsFile> file);
        FsDirectoryInMemoryFactory* addDir(std::shared_ptr<FsDirectory> file);
        FsDirectoryInMemoryFactory* setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time);
        FsDirectoryInMemoryFactory* replaceContent(const std::vector<FsID>& new_content, const FsObjectCommit& commit);
        FsDirectoryInMemoryFactory* replace(FsID old_id, FsID new_id, DateTime commit_time);
        FsDirectoryInMemoryFactory* delFile(FsID to_del, FsID commit_id, DateTime commit_time);
        FsDirectoryInMemoryFactory* remove(DateTime time, FsID renamed_to);

        FsDirectoryInMemoryPtr create(const FsDirectory* old) {
            assert(old);
            FsDirectoryInMemoryPtr dir{ new FsDirectoryInMemory{*old,
                this->current_state, this->commits, this->date_deleted, this->renamed_to, this->last_modification_commit_id, this->last_modification_date} };
            return dir;
        }
    };

}
