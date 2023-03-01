#pragma once

#include "fs/base/FsDirectory.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {
    class FsFileInMemory;

    class FsDirectoryInMemory : public FsDirectory, public FsSerializableInMemory, public FsObjectInMemory {
    public:
        FsDirectoryInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent);
        void notifyModificationChained(FsID last_commit_id, DateTime date);
        void addFile(std::shared_ptr<FsFileInMemory> file);
        void addDir(std::shared_ptr<FsDirectoryInMemory> file);
        void setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time);
        void replaceContent(const std::vector<FsID>& new_content, const FsObjectCommit& commit);
        void delFile(FsID to_del, FsID commit_id, DateTime commit_time);
        virtual void remove(DateTime time, FsID renamed_to) override;
        static void serialize(FsDirectoryInMemory* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsDirectoryInMemory> deserialize(FsID id, ByteBuff& buffer);
        virtual uint16_t getDepth() const override { return computeDepth(); }
    };

}
