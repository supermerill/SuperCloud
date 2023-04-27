#pragma once

#include "fs/base/FsFile.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {

    class FsFileInMemory : public FsFile, public FsSerializableInMemory {
    public:
        FsFileInMemory(FsID id, DateTime date_creation, const std::string& name, CUGA puga, uint32_t group_id, FsID parent, uint16_t depth, FsID renamed_from, //const data
            const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, const std::vector<size_t>& current_size) //mutable data
            : FsFile(id, date_creation, name, puga, group_id, parent, depth, renamed_from, current_state, commits, date_deleted, renamed_to, current_size) {}
        FsFileInMemory(const FsFile& old_file, //const data
            const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, const std::vector<size_t>& current_size) //mutable data
            : FsFile(old_file, current_state, commits, date_deleted, renamed_to, current_size) {}
        static void serialize(FsFileInMemory* to_serialize, ByteBuff& buffer);
        static std::shared_ptr<FsFileInMemory> deserialize(FsID id, ByteBuff& buffer);
    };

    typedef std::shared_ptr<FsFileInMemory> FsFileInMemoryPtr;

    
    class FsFileInMemoryFactory : public FsFileFactory {
    public:
        FsFileInMemoryFactory(const FsFile* pattern) : FsFileFactory(*pattern) {
            assert(pattern);
        }

        FsFileInMemoryFactory* addChunk(std::shared_ptr<FsChunkInMemory> new_chunk);
        FsFileInMemoryFactory* replaceChunkWithSize(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk);
        FsFileInMemoryFactory* replaceContentWithSize(const std::vector<FsID>& new_content, const std::vector<size_t>& new_content_size, const FsObjectCommit& commit);
        FsFileInMemoryFactory* remove(DateTime time, FsID renamed_to);

        FsFileInMemoryPtr create(const FsFile* old) {
            assert(old);
            FsFileInMemoryPtr file{ new FsFileInMemory{*old, this->current_state, this->commits, this->date_deleted, this->renamed_to, this->current_size} };
            return file;
        }
    };

}
