#include "FsDirLocal.hpp"

#include "FsFileLocal.hpp"
#include "FsChunkLocal.hpp"

namespace supercloud {

    FsDirectoryLocal::FsDirectoryLocal(FsID id, DateTime date, std::string name, CUGA puga, FsID parent)
        :FsDirectory(id, date, name, puga, parent) {}
    void FsDirectoryLocal::notifyModificationChained(FsID last_commit_id, DateTime date) {
        FsDirectory::notifyModification(last_commit_id, date);
        assert(m_loaded_parent || m_parent == m_id);
        if (m_loaded_parent && m_parent != m_id && m_loaded_parent.get() != this)
            m_loaded_parent->notifyModificationChained(last_commit_id, date);
    }

    void FsDirectoryLocal::addFile(std::shared_ptr<FsFileLocal> file) {
        FsObject::addThing(file->getId(), file->getDate());
    }

    void FsDirectoryLocal::addDir(std::shared_ptr<FsDirectoryLocal> file) {
        FsObject::addThing(file->getId(), file->getDate());
    }

    void FsDirectoryLocal::setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time) {
        std::vector<std::pair<FsID, FsID>> commit;
        for (FsID id : new_items) {
            commit.push_back({ 0, id });
        }
        FsObject::replaceContent(new_items, commit, commit_id, commit_time);
    }

    void FsDirectoryLocal::delFile(FsID to_del, FsID commit_id, DateTime commit_time) {
        bool found = false;
        {
            std::lock_guard lock{ m_modify_load_mutex };
            auto it = std::find(this->m_current_state.begin(), this->m_current_state.end(), to_del);
            if (it != this->m_current_state.end()) {
                found = true;
                this->m_commits.emplace_back();
                this->m_commits.back().id = commit_id;
                this->m_commits.back().date = commit_time;
                this->m_commits.back().changes.push_back({ to_del, 0 });
                this->m_current_state.erase(it);
            }
        }
    }

    void FsDirectoryLocal::remove(DateTime time, FsID renamed_to, const std::filesystem::path& new_path) {
        m_date_deleted = time;
        m_renamed_to = renamed_to;
        m_real_file_path = new_path;
    }

    void FsDirectoryLocal::serialize(FsDirectoryLocal* thi, ByteBuff& buffer) {
        std::cout << "Dir id=" << thi->getId() << "@" << buffer.position() << "\n";
        buffer.putLong(thi->m_creation_date);
        buffer.putUTF8(thi->m_name);
        buffer.putUTF8(thi->m_real_file_path.string());
        buffer.putShort(thi->m_puga);
        buffer.putULong(thi->m_parent);
        buffer.putLong(thi->m_last_modification_date);
        buffer.putULong(thi->m_last_modification_commit_id);
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

    std::shared_ptr<FsDirectoryLocal> FsDirectoryLocal::deserialize(uint64_t id, ByteBuff& buffer) {
        std::cout << "Dir id=" << id << "@" << buffer.position() << "\n";
        DateTime creation_date = buffer.getLong();
        std::string name = buffer.getUTF8();
        std::string path = buffer.getUTF8();
        CUGA puga = buffer.getShort();
        FsID parent = buffer.getULong();
        std::shared_ptr<FsDirectoryLocal> file = std::shared_ptr<FsDirectoryLocal>{ new FsDirectoryLocal {id, creation_date, name, puga, parent} };
        file->m_real_file_path = path;
        file->m_last_modification_date = buffer.getLong();
        file->m_last_modification_commit_id = buffer.getULong();
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
            for (size_t i = 0; i < current_state_size; i++) {
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
}
