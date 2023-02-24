#pragma once

#include "fs/base/FsFile.hpp"
#include "FsStorageLocal.hpp"

#include <filesystem>

namespace supercloud {

    class FsFileLocal : public FsFile, public FsObjectLocal {
    protected:
        void moveOldContentAndWriteNew(ByteBuff& data_new);
        std::filesystem::path computePathCommitIndex(size_t idx_commit);
    public:
        FsFileLocal(FsID id, DateTime date, std::string name, CUGA puga, FsID parent);
        static void serialize(FsFileLocal* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsFileLocal> deserialize(FsID id, ByteBuff& buffer);
        std::filesystem::path computePathCommitId(FsID commit);
        //void addChunk(std::shared_ptr<FsChunkInMemory> new_chunk);
        //void replaceChunk(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk);
        void replaceContent(const std::vector<FsID>& new_content, std::vector<std::pair<FsID, FsID>> commit, FsID commit_id, DateTime commit_time, ByteBuff& data);
        //also move commit files.
        virtual void remove(DateTime time, FsID renamed_to, const std::filesystem::path& new_garbage_path) override;
    };

}
