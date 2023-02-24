#include "FsFileLocal.hpp"

#include "FsDirLocal.hpp"
#include "FsChunkLocal.hpp"

namespace supercloud {

    FsFileLocal::FsFileLocal(FsID id, DateTime date, std::string name, CUGA puga, FsID parent)
        : FsFile(id, date, name, puga, parent) {}


    std::filesystem::path FsFileLocal::computePathCommitId(FsID commit) {
        //get commit id
        size_t idx = 0;
        for(;idx < m_commits.size() && m_commits[idx].id != commit; ++idx){}
        if (idx >= m_commits.size()) {
            return "";
        }
        return computePathCommitIndex(idx);
    }


    std::filesystem::path FsFileLocal::computePathCommitIndex(size_t idx_commit) {
        //get commit id
        assert(idx_commit < m_commits.size());
        //last commit is current file
        if (idx_commit == m_commits.size() - 1) {
            return this->m_real_file_path;
        }
        /// <summary>
        /// older commit is stored in copy
        /// </summary>
        /// <param name="idx_commit"></param>
        /// <returns></returns>
        FsID commit_id = this->m_commits[idx_commit].id;
        std::string filename = ".";
        filename += this->m_real_file_path.filename().string();
        filename += "_";
        filename += std::to_string(commit_id);
        return this->m_real_file_path.parent_path() / filename;
    }

    void FsFileLocal::serialize(FsFileLocal* thi, ByteBuff& buffer) {
        std::cout << "File id=" << thi->getId() << "@" << buffer.position() << "\n";
        buffer.putLong(thi->m_creation_date);
        buffer.putUTF8(thi->m_name);
        buffer.putUTF8(thi->m_real_file_path.string());
        buffer.putShort(thi->m_puga);
        buffer.putULong(thi->m_parent);
        buffer.putLong(thi->m_date_deleted);
        buffer.putULong(thi->m_renamed_to);
        buffer.putSize(thi->m_current_state.size());
        for (FsID id : thi->m_current_state) {
            buffer.putULong(id);
        }
        buffer.putSize(thi->m_commits.size());
        std::cout << "m_commits=" << thi->m_commits.size() << "@" << buffer.position() << "\n";
        if (thi->m_commits.size() == 1) {
            buffer.putULong(thi->m_commits.front().id);
            buffer.putLong(thi->m_commits.front().date);
        } else {
            for (Commit& commit : thi->m_commits) {
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

    std::shared_ptr<FsFileLocal> FsFileLocal::deserialize(FsID id, ByteBuff& buffer) {
        std::cout << "File id=" << id << "@" << buffer.position() << "\n";
        DateTime creation_date = buffer.getLong();
        std::string name = buffer.getUTF8();
        std::string path = buffer.getUTF8();
        CUGA puga = buffer.getShort();
        FsID parent = buffer.getULong();
        std::shared_ptr<FsFileLocal> file = std::shared_ptr<FsFileLocal>{ new FsFileLocal {id, creation_date, name, puga, parent} };
        file->m_real_file_path = path;
        file->m_date_deleted = buffer.getLong();
        file->m_renamed_to = buffer.getULong();
        size_t current_state_size = buffer.getSize();
        for (size_t i = 0; i < current_state_size; i++) {
            file->m_current_state.push_back(buffer.getULong());
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
        return file;
    }

    //to call after the creation of the new commit
    void FsFileLocal::moveOldContentAndWriteNew(ByteBuff& data_new) {
        //if not first commit, then rename old file
        if (this->m_commits.size() > 1) {
            assert(std::filesystem::exists(this->getRealPath()));
            std::filesystem::path commit_path = this->computePathCommitIndex(this->m_commits.size() - 2);
            assert(!std::filesystem::exists(commit_path));
            std::filesystem::rename(this->getRealPath(), commit_path);
        } else {
            assert(!std::filesystem::exists(this->getRealPath()));
        }
        //write data in new file
        auto myfile = std::fstream(this->getRealPath(), std::ios::out | std::ios::binary);
        myfile.write((char*)data_new.raw_array() + data_new.position(), data_new.limit());
        myfile.close();
    }

    //void FsFileLocal::addChunk(std::shared_ptr<FsChunkLocal> new_chunk) {
    //    FsObject::addThing(new_chunk->getId(), new_chunk->getDate());
    //}

    //void FsFileLocal::replaceChunk(FsChunkPtr old, std::shared_ptr<FsChunkLocal> new_chunk) {
    //    FsFile::replaceChunk(old->getId(), new_chunk->getId(), new_chunk->getDate());
    //    moveOldContentAndWriteNew(data);
    //}

    void FsFileLocal::replaceContent(const std::vector<FsID>& new_content, std::vector<std::pair<FsID, FsID>> commit, FsID commit_id, DateTime commit_time, ByteBuff& data) {
        FsObject::replaceContent(new_content, commit, commit_id, commit_time);
        moveOldContentAndWriteNew(data);
    }

    void FsFileLocal::remove(DateTime time, FsID renamed_to, const std::filesystem::path& new_path) {
        m_date_deleted = time;
        m_renamed_to = renamed_to;
        this->m_real_file_path = new_path;
    }

}
