#include "FsDirInMemory.hpp"

#include "FsFileInMemory.hpp"
#include "FsChunkInMemory.hpp"

namespace supercloud {

    void FsDirectoryInMemory::notifyModificationChained(FsUpdater& storage_updater, FsID last_commit_id, DateTime date) {
        FsDirectoryInMemoryFactory factory{ this };
        factory.notifyModification(last_commit_id, date);
        FsDirectoryInMemoryPtr ptr = factory.create(this);
        if (this->modify(ptr)) {
            storage_updater.updateEltPtr(ptr);
            // if not root, continue to root
            if (ptr->m_parent != this->m_id) {
                FsObjectPtr dir = FsObject::refresh(storage_updater.getObjPtr(ptr->m_parent));
                assert(dir);
                assert(dynamic_cast<FsDirectoryInMemory*>(dir.get()));
                if (dir) {
                    static_cast<FsDirectoryInMemory*>(dir.get())->notifyModificationChained(storage_updater, last_commit_id, date);
                }
            }
        } else {
            //NICETOHAVE: check that it can't occur -> just do nothing
            //assert(false); // it's a bit dangerous, please be sure to have lock the acces to it & updated your FsFilePtr before calling this.
            // Note: it's hard to update the m_loaded_parent, so it's normal that it's not up-to-date.
            //check if last_commit_date is older than our own
            assert(this->isObsolete());
            FsObjectPtr refreshed_object = this->force_refresh();
            assert((bool)refreshed_object);
            FsDirectoryInMemory* refreshed = dynamic_cast<FsDirectoryInMemory*>(refreshed_object.get());
            if (refreshed && date > refreshed->m_last_modification_date) {
                //retry (this one should success)
                refreshed->notifyModificationChained(storage_updater, last_commit_id, date);
            }
        }
    }

    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::addFile(std::shared_ptr<FsFile> file) {
        FsObjectFactory::addThing(file->getId(), file->getCreationTime());
        FsDirectoryFactory::notifyModification(file->getId(), file->getCreationTime());
        return this;
    }

    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::addDir(std::shared_ptr<FsDirectory> file) {
        FsObjectFactory::addThing(file->getId(), file->getCreationTime());
        FsDirectoryFactory::notifyModification(file->getId(), file->getCreationTime());
        return this;
    }

    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time) {
        FsObjectCommit commit;
        commit.id = commit_id;
        commit.time = commit_time;
        for (FsID id : new_items) {
            commit.changes.push_back({ 0, id });
        }
        FsObjectFactory::replaceContent(new_items, commit);
        FsDirectoryFactory::notifyModification(commit_id, commit_time);
        return this;
    }
    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::replaceContent(const std::vector<FsID>& new_content, const FsObjectCommit& commit) {
        FsObjectFactory::replaceContent(new_content, commit);
        FsDirectoryFactory::notifyModification(commit.id, commit.time);
        return this;
    }
    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::replace(FsID old_id, FsID new_id, DateTime commit_time) {
        for (auto it = current_state.begin(); it != current_state.end(); ++it) {
            if (*it == old_id) {
                *it = new_id;
            }
        }
        this->commits.emplace_back();
        this->commits.back().id = new_id;
        this->commits.back().time = commit_time;
        this->commits.back().changes = { {old_id, new_id} };
        FsDirectoryFactory::notifyModification(new_id, commit_time);
        return this;
    }

    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::delFile(FsID to_del, FsID commit_id, DateTime commit_time) {
        bool found = false;
        auto it = std::find(this->current_state.begin(), this->current_state.end(), to_del);
        if (it != this->current_state.end()) {
            found = true;
            this->commits.emplace_back();
            this->commits.back().id = commit_id;
            this->commits.back().time = commit_time;
            this->commits.back().changes.push_back({ to_del, 0 });
            this->current_state.erase(it);
        }
        if (found) {
            FsDirectoryFactory::notifyModification(commit_id, commit_time);
        }
        return this;
    }

    FsDirectoryInMemoryFactory* FsDirectoryInMemoryFactory::remove(DateTime time, FsID renamed_to) {
        this->date_deleted = time;
        this->renamed_to = renamed_to;
        FsDirectoryFactory::notifyModification(renamed_to, time);
        return this;
    }

    void FsDirectoryInMemory::serialize(FsDirectoryInMemory* thi, ByteBuff& buffer) {
        std::cout << "Dir id=" << thi->getId() << "@" << buffer.position() << "\n";
        // immutable section
        buffer.putLong(thi->m_creation_time);
        buffer.putUTF8(thi->m_name);
        buffer.putShort(thi->m_puga);
        buffer.putUInt(thi->m_group_id);
        buffer.putULong(thi->m_parent);
        buffer.putUShort(thi->m_depth);
        buffer.putULong(thi->m_renamed_from);
        // mutable section
        buffer.putLong(thi->m_last_modification_date);
        buffer.putULong(thi->m_last_modification_commit_id);
        buffer.putLong(thi->m_time_deleted);
        buffer.putULong(thi->m_renamed_to);
        buffer.putSize(thi->m_current_state.size());
        for (FsID id : thi->m_current_state) {
            buffer.putULong(id);
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

    std::shared_ptr<FsDirectoryInMemory> FsDirectoryInMemory::deserialize(FsID id, ByteBuff& buffer) {
        std::cout << "Dir id=" << id << "@" << buffer.position() << "\n";
        DateTime creation_date = buffer.getLong();
        std::string name = buffer.getUTF8();
        CUGA puga = buffer.getShort();
        uint32_t group = buffer.getUInt();
        FsID parent = buffer.getULong();
        uint16_t depth = buffer.getUShort();
        FsID renamed_from = buffer.getULong();
        FsDirectoryStub pattern{ id, creation_date, name, puga, group, parent, depth, renamed_from };
        FsDirectoryInMemoryFactory factory{ &pattern };
        factory.last_modification_date = buffer.getLong();
        factory.last_modification_commit_id = buffer.getULong();
        factory.date_deleted = buffer.getLong();
        factory.renamed_to = buffer.getULong();
        size_t current_state_size = buffer.getSize();
        for (size_t i = 0; i < current_state_size; i++) {
            factory.current_state.push_back(buffer.getULong());
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
            for (size_t i = 0; i < current_state_size; i++) {
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
        return factory.create(&pattern);
    }
}
