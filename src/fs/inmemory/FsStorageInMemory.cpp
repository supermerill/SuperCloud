#include "FsStorageInMemory.hpp"

#include "FsChunkInMemory.hpp"
#include "FsFileInMemory.hpp"
#include "FsDirInMemory.hpp"
#include "utils/Utils.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace supercloud {


    uint16_t FsObjectInMemory::computeDepth() const {
        if (!m_loaded_parent || static_cast<FsObjectInMemory*>(m_loaded_parent.get()) == this) return 0;
        return m_loaded_parent->getDepth() + 1;
    }

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

    FsID FsStorageInMemory::getNextId(FsType type) {
        assert(m_cid != NO_COMPUTER_ID && m_cid > 0);
        FsID id = FsElt::createId(type, m_id_generator.fetch_add(1), m_cid);
        assert(FsElt::getComputerId(id) == m_cid);
        return id;
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

    FsDirPtr FsStorageInMemory::createNewRoot() {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = this->getRoot();
        DateTime time = (DateTime)m_clock->getCurrrentTime();
        std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>{ new FsDirectoryInMemory {new_id, time, "", CUGA_7777, new_id} };
        m_database[new_dir->getId()] = new_dir;
        new_dir->set_loaded_parent(new_dir);
        new_dir->notifyModificationChained(new_dir->getId(), time);
        return new_dir;
    }

    FsChunkPtr FsStorageInMemory::addChunkToFile(FsFilePtr file, ByteBuff data) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::CHUNK);
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{
            new FsChunkInMemory {new_id, m_clock->getCurrrentTime(), data.rewind(), 
            compute_naive_hash(data.raw_array(), data.limit()), data.available()} };
        m_database[new_chunk->getId()] = new_chunk;
        static_cast<FsFileInMemory*>(file.get())->addChunk(new_chunk);
        static_cast<FsFileInMemory*>(file.get())->m_loaded_parent->notifyModificationChained(new_chunk->getId(), new_chunk->getDate());
        new_chunk->addParent(file->getId());
        return new_chunk;
    }

    FsChunkPtr FsStorageInMemory::modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, ByteBuff new_data) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::CHUNK);
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
            new FsChunkInMemory {new_id, m_clock->getCurrrentTime(), new_data.rewind(), 
            compute_naive_hash(new_data.raw_array(), new_data.limit()), new_data.available()} };
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
        FsObjectCommit commit;
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
                FsID new_id = getNextId(FsType::CHUNK);
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
                    new FsChunkInMemory {new_id, commit.date, new_chunks[idx].raw_data->rewind(), 
                    compute_naive_hash(new_chunks[idx].raw_data->raw_array(), new_chunks[idx].raw_data->limit()), new_chunks[idx].raw_data->available()} };
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
                FsID new_id = getNextId(FsType::CHUNK);
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
                    new FsChunkInMemory {new_id, commit.date, new_chunks[idx].raw_data->rewind(), 
                    compute_naive_hash(new_chunks[idx].raw_data->raw_array(), new_chunks[idx].raw_data->limit()), new_chunks[idx].raw_data->available()} };
                if (commit.id == 0) { commit.id = new_id; }
                m_database[new_id] = new_chunk;
                commit.changes.push_back({ 0, new_id });
                new_file_content.push_back(new_id);
                new_chunk->addParent(file->getId());
                file_size += new_chunk->size();
            }
        }
        if (commit.id == 0) { commit.id = getNextId(FsType::NONE); }
        static_cast<FsFileInMemory*>(file.get())->replaceContent(new_file_content, commit, file_size);
        assert(static_cast<FsFileInMemory*>(file.get())->m_loaded_parent);
        static_cast<FsFileInMemory*>(file.get())->m_loaded_parent->notifyModificationChained(commit.id, commit.date);
    }

    FsFilePtr FsStorageInMemory::createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::FILE);
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
            FsID new_id = getNextId(FsType::FILE);
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
        FsID new_id = getNextId(FsType::DIRECTORY);
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
        if (directory_parent) {
            new_dir->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(directory_parent));
            ((FsDirectoryInMemory*)directory_parent.get())->addDir(new_dir);
            new_dir->notifyModificationChained(new_dir->getId(), time);
        } else {
            assert(false);
        }
        return new_dir;
    }

    //void FsStorageInMemory::deleteObject(FsObjectPtr old_file, FsID commit_id, DateTime date) {
    //    std::lock_guard lock{ this->synchronize() };
    //    FsEltPtr directory_elt = load(old_file->getParent());
    //    if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
    //        assert(directory != old_file);
    //        ((FsObjectInMemory*)old_file.get())->remove(date, 0);
    //        ((FsDirectoryInMemory*)directory.get())->delFile(old_file->getId(), commit_id, date);
    //        ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(commit_id, date);
    //        return;
    //    }
    //    assert(false);
    //}
    void FsStorageInMemory::deleteObject(FsObjectPtr old_file) {
        std::lock_guard lock{ this->synchronize() };
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            assert(directory != old_file);
            FsID new_id = getNextId(FsType::NONE);
            DateTime time = (DateTime)m_clock->getCurrrentTime();
            ((FsObjectInMemory*)old_file.get())->remove(time, 0);
            ((FsDirectoryInMemory*)directory.get())->delFile(old_file->getId(), new_id, time);
            ((FsDirectoryInMemory*)directory.get())->notifyModificationChained(new_id, time);
            return;
        }
        assert(false);
        //deleteObject(old_file, getNextId(FsType::NONE), m_clock->getCurrrentTime());
    }

    void FsStorageInMemory::serialize(const std::filesystem::path& file) {
        std::lock_guard lock{ this->synchronize() };

        ByteBuff buffer;
        buffer.putInt(this->m_cid);
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

    size_t FsStorageInMemory::createNewFileMergeCommit(FsID file_id, FsObjectCommit& commit, const std::vector<FsID>& old_chunks, const std::vector<FsID>& new_chunks) {
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

    bool verify_extra_db_contains(FsID id, const std::unordered_map<FsID, const FsElt*>& extra_db) {
        bool ok = true;
        auto it = extra_db.find(id);
        if (it == extra_db.end()) {
            return false;
        }
        if (FsElt::isDirectory(it->first)) {
            for(FsID id_child : static_cast<const FsObject*>(it->second)->getCurrent()){
                if (!verify_extra_db_contains(id_child, extra_db)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool FsStorageInMemory::mergeObjectCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) {
        std::lock_guard lock{ this->synchronize() };

        //get our file
        FsObjectPtr good_obj = FsElt::toObject(load(new_commit.getId()));
        if (!good_obj) {
            //create the object
            //ensure the parent exists
            FsDirPtr parent = loadDirectory(new_commit.getParent());
            if (!parent) {
                // special case: our database is empty, we have to create the root
                if (new_commit.getParent() == new_commit.getId()) {
                    //root, do we have a root?
                    assert(new_commit.getId() == getRoot());
                    assert(m_database.empty());
                    //create root
                    std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>{ new FsDirectoryInMemory {getRoot(), new_commit.getDate(), "", new_commit.getCUGA(), getRoot()} };
                    if (!new_commit.getCurrent().empty()) {
                        FsObjectCommit create_commit{ new_commit.getId(), new_commit.getDate(), {} };
                        for (const FsID& id : new_commit.getCurrent()) {
                            create_commit.changes.push_back({ 0,id });
                        }
                        new_dir->replaceContent(new_commit.getCurrent(), create_commit);
                    }
                    new_dir->set_loaded_parent(new_dir);
                    m_database[new_dir->getId()] = new_dir;
                    return true;
                }
                // normal case: a directory
                if (auto it = extra_db.find(new_commit.getParent()); it != extra_db.end()) {
                    // create parent first
                    bool result = mergeDirectoryCommit(*static_cast<const FsObject*>(it->second), extra_db);
                    if (!result) {
                        error("Error, try to insert an object that has no parent (parent can't be created).");
                        assert(false);
                        return false;
                    }
                } else {
                    error("Error, try to insert an object that has no parent.");
                    assert(false);
                    return false;
                }
                parent = loadDirectory(new_commit.getParent());
                assert(parent);
            }
            //now we have our parent

            //we should be inside the parent current state (or maybe it's not done yet, as it's in the extra_db.
            if (!contains(parent->getCurrent(), new_commit.getId())) {
                if (auto it = extra_db.find(new_commit.getParent()); it == extra_db.end()
                    || !contains(static_cast<const FsObject*>(it->second)->getCurrent(), new_commit.getId())) {
                    error("Error, try to insert an object into a parent that hasn't it inside.");
                    assert(false);
                    return false;
                }
            }
            //ok, we have a parent and it has us inside
            if (FsElt::isFile(new_commit.getId())) {
                std::shared_ptr<FsFileInMemory> new_file = std::shared_ptr< FsFileInMemory>(
                    new FsFileInMemory{ new_commit.getId(), new_commit.getDate(), new_commit.getName(), new_commit.getCUGA(), new_commit.getParent() });
                if (!new_commit.getCurrent().empty()) {
                    FsObjectCommit create_commit{ new_commit.getId(), new_commit.getDate(), {} };
                    for (const FsID& id : new_commit.getCurrent()) {
                        create_commit.changes.push_back({ 0,id });
                    }
                    new_file->replaceContent(new_commit.getCurrent(), create_commit, new_commit.size());
                }
                m_database[new_file->getId()] = new_file;
                new_file->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(parent));
                ((FsDirectoryInMemory*)parent.get())->notifyModificationChained(new_file->getId(), new_commit.getDate());
            } else {
                //verify that each entry is inside the extra_db
                FsObjectCommit create_commit{ new_commit.getId(), new_commit.getDate(), {} };
                std::vector<FsID> content;
                content.reserve(new_commit.getCurrent().size());
                if (!new_commit.getCurrent().empty()) {
                    for (const FsID& id : new_commit.getCurrent()) {
                        assert(FsElt::isObject(id));
                        if (!verify_extra_db_contains(id, extra_db) || !FsElt::isObject(id)) {
                            error("Error, the directory contains an unkown entry");
                        } else {
                            create_commit.changes.push_back({ 0,id });
                            content.push_back(id);
                        }
                    }
                }
                std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>{ 
                    new FsDirectoryInMemory {new_commit.getId(), new_commit.getDate(), new_commit.getName(), new_commit.getCUGA(), new_commit.getParent() } };
                if (!content.empty()) {
                    new_dir->replaceContent(content, create_commit);
                }
                m_database[new_dir->getId()] = new_dir;
                new_dir->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(parent));
                new_dir->notifyModificationChained(new_dir->getId(), new_commit.getDate());
                //now add childs
                for (const FsID& id : content) {
                    const FsElt* elt = extra_db.at(id);
                    this->mergeFileCommit(*(static_cast<const FsObject*>(elt)), extra_db);
                }
            }
            //now we have our object inside our database (and its childs if directory).
            return true;
        } else {
            //check immutable part (if a creation commit)
            if (new_commit.getParent() != 0) {
                assert(new_commit.getParent() == good_obj->getParent());
                assert(new_commit.getName() == good_obj->getName());
                assert(new_commit.getGroupId() == good_obj->getGroupId());
                assert(new_commit.getCUGA() == good_obj->getCUGA());
                assert(new_commit.getRenamedFrom() == good_obj->getRenamedFrom());
                bool not_same = false;
                not_same |= new_commit.getParent() != good_obj->getParent();
                not_same |= new_commit.getName() != good_obj->getName();
                not_same |= new_commit.getGroupId() != good_obj->getGroupId();
                not_same |= new_commit.getCUGA() != good_obj->getCUGA();
                not_same |= new_commit.getRenamedFrom() != good_obj->getRenamedFrom();
                assert(!not_same);
                if (not_same) return false;
            }
            //is it only a creation check?
            if (new_commit.getCommitsSize() == 0) {
                assert(new_commit.getCurrent().empty());
                //whatever the state, it's here. And that's what this want.
                return true;
            }
            //now, i'm sure i have a commit.
            const FsObjectCommit& last_new_commit = new_commit.backCommit();
            // (newly) deleted?
            if (new_commit.getDeletedDate() > 0 && new_commit.getDeletedDate() >= new_commit.getDate() && good_obj->getDeletedDate() < good_obj->getDate()) {
                //check if we are deleted from our parent in the commit.
                auto it = extra_db.find(new_commit.getParent());
                if (it == extra_db.end()) {
                    error("error, can't delete an object without removing it from its parent.");
                    assert(false);
                    return true;
                }
                if (contains(static_cast<const FsObject*>(it->second)->getCurrent(), new_commit.getId())) {
                    error("error, can't delete an object without removing it from its parent (2).");
                    assert(false);
                    return true;
                }
                //delete us (parent will be done in its turn)
                if (FsElt::isFile(new_commit.getId())) {
                    static_cast<FsFileInMemory*>(good_obj.get())->remove(new_commit.getDeletedDate(), new_commit.getRenamedTo());
                } else {
                    static_cast<FsDirectoryInMemory*>(good_obj.get())->remove(new_commit.getDeletedDate(), new_commit.getRenamedTo());
                }
                return false;
            } else {
                //note: even if we are deleted, we can still modify the content. But the "delete-commit" may not have the current state, so we don't touch it when we receive one.

                // check if we aren't already at this state
                if (good_obj->getCommitsSize() > 0 && good_obj->backCommit().id == new_commit.backCommit().id) {
                    //already done, maybe by the parent directory
                    return good_obj->getDeletedDate() < good_obj->getDate();
                }
                //we are different, is the new state newer?
                //ignore if we have a more recent commit.
                if (good_obj->getCommitsSize() > 0 && good_obj->backCommit().date > last_new_commit.date) {
                    //it's more recent, ignore.
                    //TODO: maybe merge the old commit if you have the role (enforced merge?) and it's recent enough.
                    return good_obj->getDeletedDate() < good_obj->getDate();
                }

                //get previous state for me and his
                if (new_commit.getCurrent() != good_obj->getCurrent()) {
                    //create the modification commit
                    FsObjectCommit modif_commit{ last_new_commit.id, last_new_commit.date, {} };
                    //now create a commit to go from my 
                    size_t new_file_size = 0;
                    size_t min_size = std::min(new_commit.getCurrent().size(), good_obj->getCurrent().size());
                    for (size_t idx = 0; idx < min_size; idx++) {
                        if (good_obj->getCurrent()[idx] != new_commit.getCurrent()[idx]) {
                            modif_commit.changes.push_back({ good_obj->getCurrent()[idx], new_commit.getCurrent()[idx] });
                        }
                    }
                    for (size_t idx = min_size; idx < good_obj->getCurrent().size(); idx++) {
                        modif_commit.changes.push_back({ good_obj->getCurrent()[idx], 0 });
                    }
                    for (size_t idx = min_size; idx < new_commit.getCurrent().size(); idx++) {
                        modif_commit.changes.push_back({ 0, new_commit.getCurrent()[idx] });
                    }

                    //add commit
                    static_cast<FsFileInMemory*>(good_obj.get())->replaceContent(new_commit.getCurrent(), modif_commit, new_commit.size());
                }
                return good_obj->getDeletedDate() < good_obj->getDate();
            }
        }
        assert(false);
    }

    bool FsStorageInMemory::mergeFileCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) {
        return mergeObjectCommit(new_commit, extra_db);
    }

    bool FsStorageInMemory::mergeDirectoryCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) {
        return mergeObjectCommit(new_commit, extra_db);
    }


    bool FsStorageInMemory::checkFilesystem() {
        size_t errors_found = 0;
        //get each id from root
        std::unordered_set<FsID> tree_id;
        std::vector<FsID> lookup_id; //in-place recurtion stack
        std::vector<FsID> unknown_id;
        std::vector<FsID> unlinked_id;
        lookup_id.push_back(this->getRoot());
        while (!lookup_id.empty()) { // recurtion loop
            FsID cur_id = lookup_id.back();
            lookup_id.pop_back();
            FsEltPtr elt = load(cur_id);
            //assert(elt.operator bool());
            if (!elt) {
                unknown_id.push_back(cur_id);
                if (FsElt::isChunk(cur_id)) {
                    //we allow chunks to not be loaded.
                } else {
                    error(std::string("Error: we have an fs id (") + cur_id + ") that doesn't exist in our filesystem.");
                    ++errors_found;
                }
            } else {
                tree_id.insert(cur_id);
                if (FsElt::isObject(cur_id)) {
                    FsObjectPtr obj = FsElt::toObject(elt);
                    DateTime last_time = 0;
                    std::unordered_set<FsID> my_commits_ids;
                    std::vector<FsID> my_commits_to; //check that commits go to the current state TODO:ordering
                    for (size_t commit_idx = 0; commit_idx < obj->getCommitsSize(); ++commit_idx) {
                        if (last_time > obj->getCommit(commit_idx).date) {
                            error(std::string("Error: in the fs object (") + cur_id + "), commits are not ordered by time.");
                            ++errors_found;
                        }
                        for (const auto& id2id : obj->getCommit(commit_idx).changes) {
                            if (id2id.first != 0) {
                                my_commits_ids.insert(id2id.first);
                                if (auto it = std::find(my_commits_to.begin(), my_commits_to.end(), id2id.first); it != my_commits_to.end()) {
                                    if (id2id.second != 0) {
                                        *it = id2id.second;
                                    } else {
                                        my_commits_to.erase(it);
                                    }
                                } else {
                                    error(std::string("Error: in the fs object (") + cur_id + ") have a commit that delete a chunk it doesn't have (" + id2id.first + ").");
                                    ++errors_found;
                                    //TODO fix it
                                }
                            } else {
                                if (id2id.second != 0) {
                                    my_commits_to.push_back(id2id.second);
                                }
                            }
                            if (id2id.second != 0) {
                                my_commits_ids.insert(id2id.second);
                            }
                        }
                    }
                    if (my_commits_to != obj->getCurrent()) {
                        error(std::string("Error: in the fs object (") + cur_id + ") the commits won't go to the current state.");
                        ++errors_found;
                        //TODO: fix that if an error exists (like removing all commit and create the one that goes from nothign to current state)
                    }
                    //add my childs to lookup
                    for (const FsID& id : my_commits_ids) {
                        if (tree_id.find(id) == tree_id.end()) {
                            lookup_id.push_back(id);
                        }
                    }
                }
            }
        }

        //then lookup for all ids from our db that aren't in tree_id.
        for (const auto& id_2_elt : m_database) {
            if (tree_id.find(id_2_elt.first) == tree_id.end()) {
                ++errors_found;
                error(std::string("Error: we have an element (") + id_2_elt.first + ") in our filesystem that isn't linked to a parent.");
                unlinked_id.push_back(id_2_elt.first);
            }
        }
        for (const FsID& bad_id : unlinked_id) {
            //if (FsElt::isChunk(bad_id)) {
                // delete the data (here, we're in-memory, so no data on disk)
                //static_cast<FsChunkInMemory*>(loadChunk(bad_id).get())->eraseDataFromDisk();
            //}
            m_database.erase(bad_id);
        }
        //TODO: remove unknown_id from the commits & current state, or try to get them from the network.


        return errors_found == 0;
    }

}
