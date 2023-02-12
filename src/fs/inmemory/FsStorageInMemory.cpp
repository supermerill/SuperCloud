#include "FsStorageInMemory.hpp"

#include "FsChunkInMemory.hpp"
#include "fs/base/FsStorage.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"
#include "utils/Utils.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace supercloud {

    void FsObjectInMemory::set_loaded_parent(std::shared_ptr<FsDirectoryInMemory> parent) {
        m_loaded_parent = parent;
    }

    FsFileInMemory::FsFileInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent)
        : FsFile(id, date, name, puga, parent) {}
    FsEltPtr FsSerializableInMemory::deserialize(ByteBuff& buffer) {
        FsID id = buffer.getULong();
        if (FsElt::isChunk(id)) {
            return FsChunkInMemory::deserialize(id, buffer);
        } else  if (FsElt::isFile(id)) {
            return FsFileInMemory::deserialize(id, buffer);
        } else if (FsElt::isDirectory(id)) {
            return FsDirectoryInMemory::deserialize(id, buffer);
        } else {
            throw std::exception("errro, unknown id");
        }
    }
    void FsSerializableInMemory::serialize(FsEltPtr obj, ByteBuff& buffer) {
        FsID id = obj->getId();
        if (FsElt::isChunk(id)) {
            FsChunkInMemory::serialize((FsChunkInMemory*)obj.get(), buffer);
        } else  if (FsElt::isFile(id)) {
            FsFileInMemory::serialize((FsFileInMemory*)obj.get(), buffer);
        } else if (FsElt::isDirectory(id)) {
            FsDirectoryInMemory::serialize((FsDirectoryInMemory*)obj.get(), buffer);
        } else {
            throw std::exception("errro, unknown id");
        }
    }

    void FsFileInMemory::serialize(FsFileInMemory* thi, ByteBuff& buffer) {
        std::cout << "File id=" << thi->getId() << "@" << buffer.position() << "\n";
        buffer.putLong(thi->m_creation_date);
        buffer.putUTF8(thi->m_name);
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

    void FsFileInMemory::addChunk(std::shared_ptr<FsChunkInMemory> new_chunk) {
        FsObject::addThing(new_chunk->getId(), new_chunk->getDate());
    }

    void FsFileInMemory::replaceChunk(FsChunkPtr old, std::shared_ptr<FsChunkInMemory> new_chunk) {
        FsFile::replaceChunk(old->getId(), new_chunk->getId(), new_chunk->getDate());
    }

    void FsFileInMemory::replaceContent(const std::vector<FsID>& new_content, std::vector<std::pair<FsID, FsID>> commit, FsID commit_id, DateTime commit_time) {
        FsObject::replaceContent(new_content, commit, commit_id, commit_time);
    }

    void FsFileInMemory::remove(DateTime time, FsID renamed_to) {
        m_date_deleted = time;
        m_renamed_to = renamed_to;
    }

    FsDirectoryInMemory::FsDirectoryInMemory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent)
        :FsDirectory(id, date, name, puga, parent) {}
    void FsDirectoryInMemory::notifyModificationChained(FsID last_commit_id, DateTime date) {
        FsDirectory::notifyModification(last_commit_id, date);
        assert(m_loaded_parent || m_parent == m_id);
        if (m_loaded_parent && m_parent != m_id && m_loaded_parent.get() != this)
            m_loaded_parent->notifyModificationChained(last_commit_id, date);
    }

    void FsDirectoryInMemory::addFile(std::shared_ptr<FsFileInMemory> file) {
        FsObject::addThing(file->getId(), file->getDate());
    }

    void FsDirectoryInMemory::addDir(std::shared_ptr<FsDirectoryInMemory> file) {
        FsObject::addThing(file->getId(), file->getDate());
    }

    void FsDirectoryInMemory::setFiles(std::vector<FsID> new_items, FsID commit_id, DateTime commit_time) {
        std::vector<std::pair<FsID, FsID>> commit;
        for (FsID id : new_items) {
            commit.push_back({ 0, id });
        }
        FsObject::replaceContent(new_items, commit, commit_id, commit_time);
    }

    void FsDirectoryInMemory::delFile(FsID to_del, FsID commit_id, DateTime commit_time) {
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

    void FsDirectoryInMemory::remove(DateTime time, FsID renamed_to) {
        m_date_deleted = time;
        m_renamed_to = renamed_to;
    }

    void FsDirectoryInMemory::serialize(FsDirectoryInMemory* thi, ByteBuff& buffer) {
        std::cout << "Dir id=" << thi->getId() << "@" << buffer.position() << "\n";
        buffer.putLong(thi->m_creation_date);
        buffer.putUTF8(thi->m_name);
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

    std::shared_ptr<FsDirectoryInMemory> FsDirectoryInMemory::deserialize(FsID id, ByteBuff& buffer) {
        std::cout << "Dir id=" << id << "@" << buffer.position() << "\n";
        DateTime creation_date = buffer.getLong();
        std::string name = buffer.getUTF8();
        CUGA puga = buffer.getShort();
        FsID parent = buffer.getULong();
        std::shared_ptr<FsDirectoryInMemory> file = std::shared_ptr<FsDirectoryInMemory>{ new FsDirectoryInMemory {id, creation_date, name, puga, parent} };
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

    FsID FsStorageInMemory::getNextId() {
        return FsElt::createId(m_id_generator.fetch_add(4), m_cid);
    }

    bool FsStorageInMemory::hasLocally(FsID id) { 
        return database.find(id) != database.end();
    }

    FsEltPtr FsStorageInMemory::load(FsID id) {
        auto it = database.find(id);
        if (it != database.end())
            return it->second;
        return {};
    }

    FsID FsStorageInMemory::getRoot() {
        return m_root_id;
    }

    FsDirPtr FsStorageInMemory::createNewRoot() {
        FsDirPtr root = this->createNewDirectory({}, "", {}, CUGA_7777);
        m_root_id = root->getId();
        return root;
    }

    FsChunkPtr FsStorageInMemory::addChunkToFile(FsFilePtr file, ByteBuff data) {
        FsID new_id = FsElt::setChunk(getNextId());
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ new FsChunkInMemory {new_id, get_current_time_milis(), data, data.available()} };
        database[new_chunk->getId()] = new_chunk;
        ((FsFileInMemory*)file.get())->addChunk(new_chunk);
        ((FsFileInMemory*)file.get())->m_loaded_parent->notifyModificationChained(new_chunk->getId(), new_chunk->getDate());
        return new_chunk;
    }

    FsChunkPtr FsStorageInMemory::modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, ByteBuff new_data) {
        FsID new_id = FsElt::setChunk(getNextId());
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ new FsChunkInMemory {new_id, get_current_time_milis(), new_data, new_data.available()} };
        database[new_chunk->getId()] = new_chunk;
        ((FsFileInMemory*)file.get())->replaceChunk(old_chunk, new_chunk);
        ((FsFileInMemory*)file.get())->m_loaded_parent->notifyModificationChained(new_chunk->getId(), new_chunk->getDate());
        return new_chunk;
    }

    void FsStorageInMemory::modifyFile(FsFilePtr file, std::vector<ChunkOrRawData> new_chunks) {
        //note: the shuffle of chunks isn't detected, but it's something that can't happen.
        //  Files are modified completly, or at the end.
        // It something is added at the start, or in the middle, then "evrything modified" is detected. I'm not git.
        std::vector<FsID> old_chunks = file->getCurrent();
        FsID commit_id = 0;
        DateTime commit_time = get_current_time_milis();
        //create the updating request
        std::vector<FsID> new_file_content;
        std::vector<std::pair<FsID, FsID>> commit;
        size_t min_size = std::min(new_chunks.size(), old_chunks.size());
        size_t idx = 0;
        for (; idx < min_size; idx++) {
            if (new_chunks[idx].chunk) {
                if (old_chunks[idx] != new_chunks[idx].chunk->getId()) {
                    commit.push_back({ old_chunks[idx], new_chunks[idx].chunk->getId() });
                }
                new_file_content.push_back(new_chunks[idx].chunk->getId());
            } else {
                FsID new_id = FsElt::setChunk(getNextId());
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ new FsChunkInMemory {new_id, commit_time, *new_chunks[idx].raw_data, new_chunks[idx].raw_data->available()} };
                if (commit_id == 0) { commit_id = new_id; }
                database[new_id] = new_chunk;
                commit.push_back({ old_chunks[idx], new_id });
                new_file_content.push_back(new_id);
            }
        }
        for (; idx < old_chunks.size(); idx++) {
            commit.push_back({ old_chunks[idx], 0 });
        }
        for (; idx < new_chunks.size(); idx++) {
            if (new_chunks[idx].chunk) {
                commit.push_back({ 0, new_chunks[idx].chunk->getId() });
                new_file_content.push_back(new_chunks[idx].chunk->getId());
            } else {
                FsID new_id = FsElt::setChunk(getNextId());
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ new FsChunkInMemory {new_id, commit_time, *new_chunks[idx].raw_data, new_chunks[idx].raw_data->available()} };
                if (commit_id == 0) { commit_id = new_id; }
                database[new_id] = new_chunk;
                commit.push_back({ 0, new_id });
                new_file_content.push_back(new_id);
            }
        }
        if (commit_id == 0) { commit_id = FsElt::setNone(getNextId()); }
        ((FsFileInMemory*)file.get())->replaceContent(new_file_content, commit, commit_id, commit_time);
        assert(((FsFileInMemory*)file.get())->m_loaded_parent);
        ((FsFileInMemory*)file.get())->m_loaded_parent->notifyModificationChained(commit_id, commit_time);
    }

    FsFilePtr FsStorageInMemory::createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        FsID new_id = FsElt::setFile(getNextId());
        DateTime time = (DateTime)get_current_time_milis();
        std::shared_ptr<FsFileInMemory> new_file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {new_id, time, name, rights, directory->getId()} };
        new_file->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(directory));
        modifyFile(new_file, chunks);
        database[new_file->getId()] = new_file;
        ((FsDirectoryInMemory*)directory.get())->addFile(new_file);
        ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(new_file->getId(), time);
        return new_file;
    }

    FsFilePtr FsStorageInMemory::modifyFile(FsFilePtr old_file, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            FsID new_id = FsElt::setFile(getNextId());
            DateTime time = (DateTime)get_current_time_milis();
            std::shared_ptr<FsFileInMemory> new_file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {new_id, time, name, rights, directory->getId()} };
            if (!chunks.empty()) {
                modifyFile(new_file, chunks);
            }
            database[new_file->getId()] = new_file;
            new_file->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(directory));
            ((FsObjectInMemory*)old_file.get())->remove(time, new_file->getId());
            ((FsDirectoryInMemory*)directory.get())->addFile(new_file);
            ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(new_file->getId(), time);
            return new_file;
        }
        assert(false);
        return {};
    }

    FsDirPtr FsStorageInMemory::createNewDirectory(FsDirPtr directory_parent, const std::string& name, std::vector<FsObjectPtr> data, CUGA rights) {
        FsID new_id = FsElt::setDirectory(getNextId());
        DateTime time = (DateTime)get_current_time_milis();
        std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>{ new FsDirectoryInMemory {new_id, time, name, rights, directory_parent ? directory_parent->getId() : new_id} };
        if (!data.empty()) {
            std::vector<FsID> vect;
            for (FsObjectPtr& item : data) {
                vect.push_back(item->getId());
            }
            new_dir->setFiles(vect, new_dir->getId(), time);
        }
        database[new_dir->getId()] = new_dir;
        new_dir->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(directory_parent));
        if (directory_parent) {
            ((FsDirectoryInMemory*)directory_parent.get())->addDir(new_dir);
            new_dir->notifyModificationChained(new_dir->getId(), time);
        }
        return new_dir;
    }

    void FsStorageInMemory::deleteObject(FsObjectPtr old_file) {
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            assert(directory != old_file);
            FsID new_id = FsElt::setFile(getNextId());
            DateTime time = (DateTime)get_current_time_milis();
            ((FsObjectInMemory*)old_file.get())->remove(time, 0);
            ((FsDirectoryInMemory*)directory.get())->delFile(old_file->getId(), new_id, time);
            ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(new_id, time);
            return;
        }
        assert(false);
    }

    void FsStorageInMemory::save_all(const std::filesystem::path& file) {

        ByteBuff buffer;
        buffer.putInt(this->m_cid);
        buffer.putULong(this->m_root_id);
        buffer.putULong(this->m_id_generator.load());
        buffer.putSize(database.size());
        for (auto& id_2_elt : database) {
            buffer.putULong(id_2_elt.first);
            FsSerializableInMemory::serialize(id_2_elt.second, buffer);
        }
        buffer.flip();
        auto myfile = std::fstream(file, std::ios::out | std::ios::binary);
        myfile.write((char*)buffer.raw_array() + buffer.position(), buffer.limit());
        myfile.close();
    }

    void FsStorageInMemory::load_all(const std::filesystem::path& file) {
        std::ifstream infile(file, std::ios_base::binary);
        std::vector<char> vector_buffer{ std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>() };
        infile.close();

        ByteBuff buffer{ (uint8_t*)vector_buffer.data(), vector_buffer.size() };
        this->m_cid = buffer.getInt();
        this->m_root_id = buffer.getULong();
        this->m_id_generator.store(buffer.getULong());
        const size_t nb_elts = buffer.getSize();
        for (size_t idx = 0; idx < nb_elts; ++idx) {
            FsEltPtr elt = FsSerializableInMemory::deserialize(buffer);
            database[elt->getId()] = elt;
        }
        //set loaded_parent property
        for (auto& it : database) {
            if (auto chunk = FsElt::toChunk(it.second); chunk) {

            } else if (auto file = FsElt::toFile(it.second); chunk) {
                ((FsFileInMemory*)file.get())->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(database[file->getParent()]));
            } else if (auto dir = FsElt::toDirectory(it.second); chunk) {
                ((FsDirectoryInMemory*)dir.get())->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(database[file->getParent()]));
            }
        }

    }

    std::vector<FsDirPtr> FsStorageInMemory::getDirs(FsDirPtr dir) {
        std::vector<FsDirPtr> childs;
        for (FsID child : dir->getCurrent()) {
            if (FsElt::isDirectory(child)) {
                childs.push_back(FsElt::toDirectory(this->load(child)));
            }
        }
        return childs;
    }

    std::vector<FsFilePtr> FsStorageInMemory::getFiles(FsDirPtr dir) {
        std::vector<FsFilePtr> childs;
        for (FsID child : dir->getCurrent()) {
            if (FsElt::isFile(child)) {
                childs.push_back(FsElt::toFile(this->load(child)));
            }
        }
        return childs;
    }

}
