#include "FsFileInMemory.hpp"

#include "FsDirInMemory.hpp"
#include "FsChunkInMemory.hpp"

namespace supercloud {

    void FsFileInMemory::serialize(FsFileInMemory* thi, ByteBuff& buffer) {
        std::cout << "File id=" << thi->getId() << "@" << buffer.position() << "\n";
        // immutable section
        buffer.putLong(thi->m_creation_time);
        buffer.putUTF8(thi->m_name);
        buffer.putShort(thi->m_puga);
        buffer.putUInt(thi->m_group_id);
        buffer.putULong(thi->m_parent);
        buffer.putUShort(thi->m_depth);
        buffer.putULong(thi->m_renamed_from);
        // mutable section
        buffer.putLong(thi->m_time_deleted);
        buffer.putULong(thi->m_renamed_to);
        buffer.putSize(thi->m_current_state.size());
        assert(thi->m_current_size.size() == thi->m_current_state.size());
        for (size_t i = 0; i < thi->m_current_state.size(); ++i) {
            buffer.putULong(thi->m_current_state[i]);
            buffer.putSize(thi->m_current_size[i]);
        }
        buffer.putSize(thi->m_commits.size());
        std::cout << "m_commits=" << thi->m_commits.size() << "@" << buffer.position() << "\n";
        if (thi->m_commits.size() == 1) {
            buffer.putULong(thi->m_commits.front().id);
            buffer.putLong(thi->m_commits.front().time);
        } else {
            for (const FsObjectCommit& commit : thi->m_commits) {
                buffer.putULong(commit.id);
                buffer.putLong(commit.time);
                buffer.putSize(commit.changes.size());
                for (auto& del2add : commit.changes) {
                    buffer.putSize(del2add.first);
                    buffer.putSize(del2add.second);
                }
            }
        }
    }

    std::shared_ptr<FsFileInMemory> FsFileInMemory::deserialize(FsID id, ByteBuff& buffer) {
        std::cout << "File id=" << id << "@" << buffer.position() << "\n";
        DateTime creation_date = buffer.getLong();
        std::string name = buffer.getUTF8();
        CUGA puga = buffer.getShort();
        uint32_t group = buffer.getUInt();
        FsID parent = buffer.getULong();
        uint16_t depth = buffer.getUShort();
        FsID renamed_from = buffer.getULong();
        FsFileStub pattern{ id, creation_date, name, puga, group, parent, depth, renamed_from };
        FsFileInMemoryFactory factory{ &pattern };
        factory.date_deleted = buffer.getLong();
        factory.renamed_to = buffer.getULong();
        size_t current_state_size = buffer.getSize();
        for (size_t i = 0; i < current_state_size; i++) {
            factory.current_state.push_back(buffer.getULong());
            factory.current_size.push_back(buffer.getSize());
        }
        size_t commit_size = buffer.getSize();
        std::cout << "m_commits=" << commit_size << "@" << buffer.position() << "\n";
        if (commit_size == 1) {
            factory.commits.emplace_back();
            factory.commits.back().id = buffer.getULong();
            factory.commits.back().time = buffer.getLong();
            for (FsID id : factory.current_state) {
                factory.commits.back().changes.push_back({ 0, id });
            }
        } else {
            for (size_t i = 0; i < commit_size; i++) {
                factory.commits.emplace_back();
                factory.commits.back().id = buffer.getULong();
                factory.commits.back().time = buffer.getLong();
                size_t changes_size = buffer.getSize();
                for (size_t ic = 0; ic < changes_size; ic++) {
                    factory.commits.back().changes.emplace_back();
                    factory.commits.back().changes.back().first = buffer.getSize();
                    factory.commits.back().changes.back().second = buffer.getSize();
                }
            }
        }
        assert(factory.current_size.size() == factory.current_state.size());
        return factory.create(&pattern);
    }

    FsFileInMemoryFactory* FsFileInMemoryFactory::addChunk(std::shared_ptr<FsChunkInMemory> new_chunk) {
        assert(new_chunk->size() > 0 && new_chunk->size() != size_t(-1));
        this->addThing(new_chunk->getId(), new_chunk->getCreationTime());
        this->current_size.push_back(new_chunk->size());
        assert(current_size.size() == current_state.size());
        return this;
    }

    FsFileInMemoryFactory* FsFileInMemoryFactory::replaceChunkWithSize(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk) {
        assert(new_chunk->size() > 0 && new_chunk->size() != size_t(-1));
        this->replaceChunk(old->getId(), new_chunk->getId(), new_chunk->size(), new_chunk->getCreationTime());
        assert(current_size.size() == current_state.size());
        return this;
    }

    FsFileInMemoryFactory* FsFileInMemoryFactory::replaceContentWithSize(const std::vector<FsID>& new_content, const std::vector<size_t>& new_content_size, const FsObjectCommit& commit) {
        this->replaceContent(new_content, commit);
        this->current_size = new_content_size;
        assert(current_size.size() == current_state.size());
        return this;
    }

    FsFileInMemoryFactory* FsFileInMemoryFactory::remove(DateTime time, FsID renamed_to_id) {
        this->date_deleted = time;
        this->renamed_to = renamed_to_id;
        return this;
    }

}
