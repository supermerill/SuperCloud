#include "FsStorageInMemory.hpp"

#include "FsChunkInMemory.hpp"
#include "FsFileInMemory.hpp"
#include "FsDirInMemory.hpp"
#include "utils/Utils.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace supercloud {

    void FsObjectInMemory::set_loaded_parent(std::shared_ptr<FsDirectoryInMemory> parent) {
        m_loaded_parent = parent;
    }

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
            throw std::exception("error, unknown id");
        }
    }

    FsID FsStorageInMemory::getNextId() {
        return FsElt::createId(m_id_generator.fetch_add(4), m_cid);
    }

    bool FsStorageInMemory::hasLocally(FsID id) { 
        return m_database.find(id) != m_database.end();
    }

    FsEltPtr FsStorageInMemory::load(FsID id) {
        auto it = m_database.find(id);
        if (it != m_database.end())
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
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = FsElt::setChunk(getNextId());
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{
            new FsChunkInMemory {new_id, m_clock->getCurrrentTime(), data.rewind(), FsChunk::getHash(data.rewind()), data.available()} };
        m_database[new_chunk->getId()] = new_chunk;
        static_cast<FsFileInMemory*>(file.get())->addChunk(new_chunk);
        static_cast<FsFileInMemory*>(file.get())->m_loaded_parent->notifyModificationChained(new_chunk->getId(), new_chunk->getDate());
        new_chunk->addParent(file->getId());
        return new_chunk;
    }

    FsChunkPtr FsStorageInMemory::modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, ByteBuff new_data) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = FsElt::setChunk(getNextId());
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
            new FsChunkInMemory {new_id, m_clock->getCurrrentTime(), new_data.rewind(), FsChunk::getHash(new_data.rewind()), new_data.available()} };
        m_database[new_chunk->getId()] = new_chunk;
        static_cast<FsFileInMemory*>(file.get())->replaceChunk(old_chunk, new_chunk);
        static_cast<FsFileInMemory*>(file.get())->m_loaded_parent->notifyModificationChained(new_chunk->getId(), new_chunk->getDate());
        new_chunk->addParent(file->getId());
        return new_chunk;
    }

    void FsStorageInMemory::modifyFile(FsFilePtr file, std::vector<ChunkOrRawData> new_chunks) {
        std::lock_guard lock{ this->synchronize() };
        //note: the shuffle of chunks isn't detected, but it's something that can't happen.
        //  Files are modified completly, or at the end.
        // It something is added at the start, or in the middle, then "evrything modified" is detected. I'm not git.
        std::vector<FsID> old_chunks = file->getCurrent();
        //create the updating request
        std::vector<FsID> new_file_content;
        FsObject::Commit commit;
        commit.date = m_clock->getCurrrentTime();
        size_t min_size = std::min(new_chunks.size(), old_chunks.size());
        size_t idx = 0;
        size_t file_size = 0;
        for (; idx < min_size; idx++) {
            if (new_chunks[idx].chunk) {
                if (old_chunks[idx] != new_chunks[idx].chunk->getId()) {
                    commit.changes.push_back({ old_chunks[idx], new_chunks[idx].chunk->getId() });
                }
                new_file_content.push_back(new_chunks[idx].chunk->getId());
                ((FsChunkInMemory*)new_chunks[idx].chunk.get())->addParent(file->getId());
                file_size += new_chunks[idx].chunk->size();
            } else {
                FsID new_id = FsElt::setChunk(getNextId());
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
                    new FsChunkInMemory {new_id, commit.date, new_chunks[idx].raw_data->rewind(), FsChunk::getHash(new_chunks[idx].raw_data->rewind()), new_chunks[idx].raw_data->available()} };
                if (commit.id == 0) { commit.id = new_id; }
                m_database[new_id] = new_chunk;
                commit.changes.push_back({ old_chunks[idx], new_id });
                new_file_content.push_back(new_id);
                new_chunk->addParent(file->getId());
                file_size += new_chunk->size();
            }
        }
        for (; idx < old_chunks.size(); idx++) {
            commit.changes.push_back({ old_chunks[idx], 0 });
        }
        for (; idx < new_chunks.size(); idx++) {
            if (new_chunks[idx].chunk) {
                commit.changes.push_back({ 0, new_chunks[idx].chunk->getId() });
                new_file_content.push_back(new_chunks[idx].chunk->getId());
                ((FsChunkInMemory*)new_chunks[idx].chunk.get())->addParent(file->getId());
                file_size += new_chunks[idx].chunk->size();
            } else {
                FsID new_id = FsElt::setChunk(getNextId());
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
                    new FsChunkInMemory {new_id, commit.date, new_chunks[idx].raw_data->rewind(), FsChunk::getHash(new_chunks[idx].raw_data->rewind()), new_chunks[idx].raw_data->available()} };
                if (commit.id == 0) { commit.id = new_id; }
                m_database[new_id] = new_chunk;
                commit.changes.push_back({ 0, new_id });
                new_file_content.push_back(new_id);
                new_chunk->addParent(file->getId());
                file_size += new_chunk->size();
            }
        }
        if (commit.id == 0) { commit.id = FsElt::setNone(getNextId()); }
        static_cast<FsFileInMemory*>(file.get())->replaceContent(new_file_content, commit, file_size);
        assert(static_cast<FsFileInMemory*>(file.get())->m_loaded_parent);
        static_cast<FsFileInMemory*>(file.get())->m_loaded_parent->notifyModificationChained(commit.id, commit.date);
    }

    FsFilePtr FsStorageInMemory::createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = FsElt::setFile(getNextId());
        DateTime time = (DateTime)m_clock->getCurrrentTime();
        std::shared_ptr<FsFileInMemory> new_file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {new_id, time, name, rights, directory->getId()} };
        new_file->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(directory));
        modifyFile(new_file, chunks);
        m_database[new_file->getId()] = new_file;
        ((FsDirectoryInMemory*)directory.get())->addFile(new_file);
        ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(new_file->getId(), time);
        return new_file;
    }

    FsFilePtr FsStorageInMemory::modifyFile(FsFilePtr old_file, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        std::lock_guard lock{ this->synchronize() };
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            FsID new_id = FsElt::setFile(getNextId());
            DateTime time = (DateTime)m_clock->getCurrrentTime();
            std::shared_ptr<FsFileInMemory> new_file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {new_id, time, name, rights, directory->getId()} };
            if (!chunks.empty()) {
                modifyFile(new_file, chunks);
            }
            m_database[new_file->getId()] = new_file;
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
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = FsElt::setDirectory(getNextId());
        DateTime time = (DateTime)m_clock->getCurrrentTime();
        std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>{ new FsDirectoryInMemory {new_id, time, name, rights, directory_parent ? directory_parent->getId() : new_id} };
        if (!data.empty()) {
            std::vector<FsID> vect;
            for (FsObjectPtr& item : data) {
                vect.push_back(item->getId());
            }
            new_dir->setFiles(vect, new_dir->getId(), time);
        }
        m_database[new_dir->getId()] = new_dir;
        new_dir->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(directory_parent));
        if (directory_parent) {
            ((FsDirectoryInMemory*)directory_parent.get())->addDir(new_dir);
            new_dir->notifyModificationChained(new_dir->getId(), time);
        }
        return new_dir;
    }

    void FsStorageInMemory::deleteObject(FsObjectPtr old_file) {
        std::lock_guard lock{ this->synchronize() };
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            assert(directory != old_file);
            FsID new_id = FsElt::setFile(getNextId());
            DateTime time = (DateTime)m_clock->getCurrrentTime();
            ((FsObjectInMemory*)old_file.get())->remove(time, 0);
            ((FsDirectoryInMemory*)directory.get())->delFile(old_file->getId(), new_id, time);
            ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(new_id, time);
            return;
        }
        assert(false);
    }

    void FsStorageInMemory::serialize(const std::filesystem::path& file) {
        std::lock_guard lock{ this->synchronize() };

        ByteBuff buffer;
        buffer.putInt(this->m_cid);
        buffer.putULong(this->m_root_id);
        buffer.putULong(this->m_id_generator.load());
        buffer.putSize(m_database.size());
        for (auto& id_2_elt : m_database) {
            buffer.putULong(id_2_elt.first);
            FsSerializableInMemory::serialize(id_2_elt.second, buffer);
        }
        buffer.flip();
        auto myfile = std::fstream(file, std::ios::out | std::ios::binary);
        myfile.write((char*)buffer.raw_array() + buffer.position(), buffer.limit());
        myfile.close();
    }

    void FsStorageInMemory::deserialize(const std::filesystem::path& file) {
        std::lock_guard lock{ this->synchronize() };
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
            m_database[elt->getId()] = elt;
        }
        //set loaded_parent property
        for (auto& it : m_database) {
            if (auto chunk = FsElt::toChunk(it.second); chunk) {

            } else if (auto file = FsElt::toFile(it.second); chunk) {
                FsEltPtr parent = m_database[file->getParent()];
                FsElt* eltp = parent.get();
                FsDirectoryInMemory* dirp = static_cast<FsDirectoryInMemory*>(eltp);
                static_cast<FsFileInMemory*>(file.get())->set_loaded_parent(std::static_pointer_cast<FsDirectoryInMemory>(parent));
            } else if (auto dir = FsElt::toDirectory(it.second); chunk) {
                static_cast<FsDirectoryInMemory*>(dir.get())->set_loaded_parent(std::static_pointer_cast<FsDirectoryInMemory>(m_database[file->getParent()]));
            }
        }

    }

    std::vector<FsDirPtr> FsStorageInMemory::getDirs(FsDirPtr dir) {
        std::lock_guard lock{ this->synchronize() };
        std::vector<FsDirPtr> childs;
        for (FsID child : dir->getCurrent()) {
            if (FsElt::isDirectory(child)) {
                childs.push_back(FsElt::toDirectory(this->load(child)));
            }
        }
        return childs;
    }

    std::vector<FsFilePtr> FsStorageInMemory::getFiles(FsDirPtr dir) {
        std::lock_guard lock{ this->synchronize() };
        std::vector<FsFilePtr> childs;
        for (FsID child : dir->getCurrent()) {
            if (FsElt::isFile(child)) {
                childs.push_back(FsElt::toFile(this->load(child)));
            }
        }
        return childs;
    }

    size_t FsStorageInMemory::createNewMergeCommit(FsID file_id, FsObject::Commit& commit, const std::vector<FsID>& old_chunks, const std::vector<FsID>& new_chunks) {
        std::lock_guard lock{ this->synchronize() };
        size_t new_file_size = 0;
        size_t min_size = std::min(new_chunks.size(), old_chunks.size());
        size_t idx = 0;
        for (; idx < min_size; idx++) {
            if (old_chunks[idx] != new_chunks[idx]) {
                commit.changes.push_back({ old_chunks[idx], new_chunks[idx] });
                if (FsChunkPtr chunk = loadChunk(new_chunks[idx]); chunk) {
                    static_cast<FsChunkInMemory*>(chunk.get())->addParent(file_id);
                    new_file_size += chunk->size();
                } else {
                    //create distant one
                    std::shared_ptr<FsChunkInMemory> new_stub_chunk = std::shared_ptr<FsChunkInMemory>{
                        new FsChunkInMemory{new_chunks[idx], 0, 0, 0} };
                    new_stub_chunk->addParent(file_id);
                    m_database[new_chunks[idx]] = new_stub_chunk;
                    new_file_size += new_stub_chunk->size(); //FIXME?
                }
            }
        }
        for (; idx < old_chunks.size(); idx++) {
            commit.changes.push_back({ old_chunks[idx], 0 });
        }
        for (; idx < new_chunks.size(); idx++) {
            commit.changes.push_back({ 0, new_chunks[idx] });
            if (FsChunkPtr chunk = loadChunk(new_chunks[idx]); chunk) {
                static_cast<FsChunkInMemory*>(chunk.get())->addParent(file_id);
                new_file_size += chunk->size();
            } else {
                //create distant one
                std::shared_ptr<FsChunkInMemory> new_stub_chunk = std::shared_ptr<FsChunkInMemory>{
                    new FsChunkInMemory{new_chunks[idx], 0, 0, 0} };
                new_stub_chunk->addParent(file_id);
                m_database[new_chunks[idx]] = new_stub_chunk;
                new_file_size += new_stub_chunk->size(); //FIXME?
            }
        }
        return new_file_size;
    }

    bool FsStorageInMemory::mergeFileCommit(const FsObject& new_commit) {
        std::lock_guard lock{ this->synchronize() };
        assert(new_commit.getCommitsSize() >= 1);
        const FsObject::Commit& last_new_commit = new_commit.backCommit();
        std::vector<FsID> new_commit_state = new_commit.getCurrent();
        //get our file
        FsFilePtr goodfile = loadFile(new_commit.getId());
        if (!goodfile) {
            if (new_commit.getCurrent().empty()) {
                //is deleted anyway... I don't care.
                return;
            }
            //have to create it.
            assert(new_commit.getDate() != 0);
            assert(new_commit.getName() != "");
            assert(new_commit.getParent() != 0);
            
            // hopefully, its parent exists.
            FsDirPtr parent = loadDirectory(new_commit.getParent());

            //.... i'll add it parent or no parent, so it's here for when i'll receive its parent.
            std::shared_ptr<FsFileInMemory> new_file = std::shared_ptr< FsFileInMemory>(
                new FsFileInMemory{ new_commit.getId(), new_commit.getDate(), new_commit.getName(), new_commit.getCUGA(), new_commit.getParent() });
            //we only store the last commit. (because of laziness) Please create a BackupFs if you want to store evrything.
            //create the creation commit
            FsObject::Commit create_commit{ last_new_commit.id, last_new_commit.date, {} };
            for (const FsID& id : new_commit_state) {
                create_commit.changes.push_back({ 0,id });
            }
            //push it
            new_file->replaceContent(new_commit_state, create_commit, 0); //FIXME 0 size

            if (!parent) {
                //NOTE: currently they are not sorted, so this isn't respected.
                error("Error, try to insert a file that has no parent.");
                //assert(false);
            }
        } else {
            //ignore if we have a more recent commit.
            if (goodfile->getCommitsSize() > 0 && goodfile->backCommit().date > last_new_commit.date) {
                //more recent, ignore.
                return false;
            }
            //deleted?
            if (new_commit_state.empty()) {
                if (FsDirPtr directory = loadDirectory(new_commit.getParent()); directory) {
                    ((FsObjectInMemory*)goodfile.get())->remove(last_new_commit.date, new_commit.getRenamedTo());
                    ((FsDirectoryInMemory*)directory.get())->delFile(goodfile->getId(), last_new_commit.id, last_new_commit.date);
                    ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(last_new_commit.id, last_new_commit.date);
                    return;
                } else {
                    //error, no parent but the file exists!
                    assert(false);
                }
            }else{
                //get previous state for me and his
                //create the modification commit
                FsObject::Commit modif_commit{ last_new_commit.id, last_new_commit.date, {} };
                //now create a commit to go from my 
                size_t new_size = createNewMergeCommit(goodfile->getId(), modif_commit, goodfile->getCurrent(), new_commit_state);

                //add commit
                static_cast<FsFileInMemory*>(goodfile.get())->replaceContent(new_commit.getCurrent(), modif_commit, new_size);
            }
        }
        return true;
    }

    bool FsStorageInMemory::mergeDirectoryCommit(const FsObject& new_commit) {
        std::lock_guard lock{ this->synchronize() };
        assert(new_commit.getCommitsSize() >= 1);
        const FsObject::Commit& last_new_commit = new_commit.backCommit();
        std::vector<FsID> new_commit_state = new_commit.getCurrent();
        //get our file
        FsDirPtr gooddir = loadDirectory(new_commit.getId());
        if (!gooddir) {
            if (new_commit_state.empty()) {
                //is deleted anyway... I don't care. But this shouldn't happen...
                assert(false);
                return;
            }
            //have to create it... hopefully, its parent exists.
            FsDirPtr parent = loadDirectory(new_commit.getParent());

            //.... i'll add it parent or no praent, so it's here for when i'll receive its parent.
            std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>(
                new FsDirectoryInMemory{ new_commit.getId(), new_commit.getDate(), new_commit.getName(), new_commit.getCUGA(), new_commit.getParent() });
            //we only store the last commit. (because of laziness) Please create a BackupFs if you want to store evrything.
            //create the creation commit
            FsObject::Commit create_commit{ last_new_commit.id, last_new_commit.date, {} };
            for (const FsID& id : new_commit_state) {
                create_commit.changes.push_back({ 0,id });
            }
            //push it
            new_dir->replaceContent(new_commit_state, create_commit);

            if (!parent) {
                error("Error, try to insert a directory that has no parent.");
                assert(false);
            }
        } else {
            //ignore if we have a more recent commit.
            if (gooddir->getCommitsSize() > 0 && gooddir->backCommit().date > last_new_commit.date) {
                //more recent, ignore.
                return false;
            }
            //deleted?
            if (new_commit_state.empty()) {
                if (FsDirPtr dir_parent = loadDirectory(new_commit.getParent()); dir_parent) {
                    static_cast<FsDirectoryInMemory*>(gooddir.get())->remove(last_new_commit.date, new_commit.getRenamedTo());
                    static_cast<FsDirectoryInMemory*>(dir_parent.get())->delFile(gooddir->getId(), last_new_commit.id, last_new_commit.date);
                    static_cast<FsDirectoryInMemory*>(dir_parent.get())->notifyModificationChained(last_new_commit.id, last_new_commit.date);
                    return;
                } else {
                    //error, no parent but the dir exists!
                    assert(false);
                }
            } else {
                //create the modification commit
                FsObject::Commit modif_commit{ last_new_commit.id, last_new_commit.date, {} };
                //now create a commit to go from my 
                createNewMergeCommit(gooddir->getId(), modif_commit, gooddir->getCurrent(), new_commit_state);
                //add commit
                static_cast<FsDirectoryInMemory*>(gooddir.get())->replaceContent(new_commit_state, modif_commit);
            }
        }
        return true;
    }
}
