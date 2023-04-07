#include "FsFileInMemory.hpp"

#include "FsDirInMemory.hpp"
#include "FsChunkInMemory.hpp"

namespace supercloud {

    FsFileInMemory::FsFileInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent)
        : FsFile(id, date, name, puga, parent) {}
    FsFileInMemory::FsFileInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent, FsID renamed_from)
        : FsFile(id, date, name, puga, parent, renamed_from) {}

    void FsFileInMemory::serialize(FsFileInMemory* thi, ByteBuff& buffer) {
        std::cout << "File id=" << thi->getId() << "@" << buffer.position() << "\n";
        buffer.putLong(thi->m_creation_date);
        buffer.putUTF8(thi->m_name);
        buffer.putShort(thi->m_puga);
        buffer.putULong(thi->m_parent);
        buffer.putLong(thi->m_date_deleted);
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
            buffer.putLong(thi->m_commits.front().date);
        } else {
            for (FsObjectCommit& commit : thi->m_commits) {
                buffer.putULong(commit.id);
                buffer.putLong(commit.date);
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
        FsID parent = buffer.getULong();
        std::shared_ptr<FsFileInMemory> file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {id, creation_date, name, puga, parent} };
        file->m_date_deleted = buffer.getLong();
        file->m_renamed_to = buffer.getULong();
        size_t current_state_size = buffer.getSize();
        for (size_t i = 0; i < current_state_size; i++) {
            file->m_current_state.push_back(buffer.getULong());
            file->m_current_size.push_back(buffer.getSize());
        }
        size_t commit_size = buffer.getSize();
        std::cout << "m_commits=" << commit_size << "@" << buffer.position() << "\n";
        if (commit_size == 1) {
            file->m_commits.emplace_back();
            file->m_commits.back().id = buffer.getULong();
            file->m_commits.back().date = buffer.getLong();
            for (FsID id : file->m_current_state) {
                file->m_commits.back().changes.push_back({ 0, id });
            }
        } else {
            for (size_t i = 0; i < commit_size; i++) {
                file->m_commits.emplace_back();
                file->m_commits.back().id = buffer.getULong();
                file->m_commits.back().date = buffer.getLong();
                size_t changes_size = buffer.getSize();
                for (size_t ic = 0; ic < changes_size; ic++) {
                    file->m_commits.back().changes.emplace_back();
                    file->m_commits.back().changes.back().first = buffer.getSize();
                    file->m_commits.back().changes.back().second = buffer.getSize();
                }
            }
        }
        assert(file->m_current_size.size() == file->m_current_state.size());
        assert(file->m_size == std::accumulate(file->m_current_size.begin(), file->m_current_size.end(), size_t(0)));
        return file;
    }

    void FsFileInMemory::addChunk(std::shared_ptr<FsChunkInMemory> new_chunk) {
        assert(new_chunk->size() > 0 && new_chunk->size() != size_t(-1));
        FsObject::addThing(new_chunk->getId(), new_chunk->getDate());
        m_current_size.push_back(new_chunk->size());
        this->m_size += new_chunk->size();
        assert(m_current_size.size() == m_current_state.size());
        assert(m_size == std::accumulate(m_current_size.begin(), m_current_size.end(), size_t(0)));
    }

    void FsFileInMemory::replaceChunk(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk) {
        assert(new_chunk->size() > 0 && new_chunk->size() != size_t(-1));
        FsFile::replaceChunk(old->getId(), new_chunk->getId(), new_chunk->size(), new_chunk->getDate());
        this->m_size += new_chunk->size() - old->size();
        assert(m_current_size.size() == m_current_state.size());
        assert(m_size == std::accumulate(m_current_size.begin(), m_current_size.end(), size_t(0)));
    }

    void FsFileInMemory::replaceContent(const std::vector<FsID>& new_content, const std::vector<size_t>& new_content_size, const FsObjectCommit& commit) {
        FsObject::replaceContent(new_content, commit);
        m_current_size = new_content_size;
        m_size = std::accumulate(m_current_size.begin(), m_current_size.end(), size_t(0));
        assert(m_current_size.size() == m_current_state.size());
    }

    void FsFileInMemory::remove(DateTime time, FsID renamed_to) {
        m_date_deleted = time;
        m_renamed_to = renamed_to;
    }

}
