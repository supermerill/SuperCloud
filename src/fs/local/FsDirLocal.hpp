#pragma once

#include "fs/base/FsDirectory.hpp"
#include "FsStorageLocal.hpp"

#include <filesystem>

namespace supercloud {
    class FsFileLocal;

    class FsDirectoryLocal : public FsDirectory, public FsObjectLocal {
    public:
        FsDirectoryLocal(FsID id, DateTime date, std::string name, CUGA puga, FsID parent);
        
        void notifyModificationChained(FsID last_commit_id, DateTime date);
        void addFile(std::shared_ptr<FsFileLocal> file);
        void addDir(std::shared_ptr<FsDirectoryLocal> file);
        void setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time);
        void delFile(FsID to_del, FsID commit_id, DateTime commit_time);
        virtual void remove(DateTime time, FsID renamed_to, const std::filesystem::path& new_path) override;
        static void serialize(FsDirectoryLocal* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsDirectoryLocal> deserialize(uint64_t id, ByteBuff& buffer);
    };

}
