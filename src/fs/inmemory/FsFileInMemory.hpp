#pragma once

#include "fs/base/FsFile.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {

    class FsFileInMemory : public FsFile, public FsSerializableInMemory, public FsObjectInMemory {
    public:
        FsFileInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent);
        static void serialize(FsFileInMemory* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsFileInMemory> deserialize(FsID id, ByteBuff& buffer);
        void addChunk(std::shared_ptr<FsChunkInMemory> new_chunk);
        void replaceChunk(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk);
        void replaceContent(const std::vector<FsID>& new_content, const FsObjectCommit& commit, size_t new_size);
        virtual void remove(DateTime time, FsID renamed_to) override;
        virtual uint16_t getDepth() const override { return computeDepth(); }
    };

}
