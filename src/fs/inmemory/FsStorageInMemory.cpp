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


    FsEltPtr FsSerializableInMemory::deserialize(ByteBuff& buffer) {
        FsID id = buffer.getULong();
        FsEltPtr elt;
        if (FsElt::isChunk(id)) {
            assert(buffer.get() == 'c');
            elt = FsChunkInMemory::deserialize(id, buffer);
            assert(buffer.get() == 'C');
        } else  if (FsElt::isFile(id)) {
            assert(buffer.get() == 'f');
            elt = FsFileInMemory::deserialize(id, buffer);
            assert(buffer.get() == 'F');
        } else if (FsElt::isDirectory(id)) {
            assert(buffer.get() == 'd');
            elt = FsDirectoryInMemory::deserialize(id, buffer);
            assert(buffer.get() == 'D');
        } else {
            throw std::exception("errro, unknown id");
        }
        return elt;
    }

    void FsSerializableInMemory::serialize(FsEltPtr obj, ByteBuff& buffer) {
        FsID id = obj->getId();
        if (FsElt::isChunk(id)) {
            buffer.put('c');
            FsChunkInMemory::serialize((FsChunkInMemory*)obj.get(), buffer);
            buffer.put('C');
        } else  if (FsElt::isFile(id)) {
            buffer.put('f');
            FsFileInMemory::serialize((FsFileInMemory*)obj.get(), buffer);
            buffer.put('F');
        } else if (FsElt::isDirectory(id)) {
            buffer.put('d');
            FsDirectoryInMemory::serialize((FsDirectoryInMemory*)obj.get(), buffer);
            buffer.put('D');
        } else {
            throw std::exception("error, unknown id");
        }
    }

    void FsUpdater::updateEltPtr(FsEltPtr ptr) {
        m_database->updateEltPtr(ptr);
    }
    FsObjectPtr FsUpdater::getObjPtr(FsID id) {
        return m_database->loadObject(id);
    }

    void FsStorageInMemory::updateEltPtr(FsEltPtr ptr) {
        std::lock_guard lock{ m_database_mutex };
        m_database[ptr->getId()] = ptr;
    }

    FsID FsStorageInMemory::getNextId(FsType type) {
        assert(m_cid != NO_COMPUTER_ID && m_cid > 0);
        FsID id = FsElt::createId(type, m_id_generator.fetch_add(1), m_cid);
        assert(FsElt::getComputerId(id) == m_cid);
        return id;
    }

    bool FsStorageInMemory::hasLocally(FsID id) {
        std::lock_guard lock{ m_database_mutex };
        return m_database.find(id) != m_database.end();
    }

    FsEltPtr FsStorageInMemory::load(FsID id) {
        std::lock_guard lock{ m_database_mutex };
        auto it = m_database.find(id);
        if (it != m_database.end())
            return it->second;
        return {};
    }

    FsDirPtr FsStorageInMemory::createNewRoot() {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = this->getRoot();
        DateTime time = (DateTime)m_clock->getCurrentTime();
        std::shared_ptr<FsDirectoryInMemory> new_dir = std::shared_ptr<FsDirectoryInMemory>{ new FsDirectoryInMemory {new_id, time, "", CUGA_7777, 0, new_id, 0/*depth=0 for root*/, 0,
            {}, {}, 0, 0, new_id, time} };
        updateEltPtr(new_dir);
        return new_dir;
    }

    FsChunkPtr FsStorageInMemory::addChunkToFile(FsFilePtr& file, uint8_t* new_data, size_t new_data_size) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::CHUNK);
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{
            new FsChunkInMemory {new_id, m_clock->getCurrentTime(), new_data,
            compute_naive_hash(new_data, new_data_size), new_data_size} };
        updateEltPtr(new_chunk); // chunk has to be added before modifying the file
        // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
        while (!file->modify(FsFileInMemoryFactory{ file.get() }.addChunk(new_chunk)->create(file.get()))) {
            file = FsFile::refresh(file);
            assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
        }
        file = FsFile::refresh(file); // set the new file ptr (the one we just created)
        updateEltPtr(file);
        return new_chunk;
    }

    FsChunkPtr FsStorageInMemory::modifyChunk(FsFilePtr& file, FsChunkPtr old_chunk, uint8_t* new_data, size_t new_data_size) {
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::CHUNK);
        //create new chunk
        std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{ 
            new FsChunkInMemory {new_id, m_clock->getCurrentTime(), new_data, 
            compute_naive_hash(new_data, new_data_size), new_data_size} };
        updateEltPtr(new_chunk); // chunk has to be added before modifying the file
        // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
        while (!file->modify(FsFileInMemoryFactory{ file.get() }.replaceChunkWithSize(old_chunk, new_chunk)->create(file.get()))) {
            file = FsFile::refresh(file);
            assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
        }
        file = FsFile::refresh(file); // set the new file ptr (the one we just created)
        updateEltPtr(file);
        return new_chunk;
    }

    void FsStorageInMemory::modifyFile(FsFilePtr& file, std::vector<ChunkOrRawData> new_chunks) {
        std::lock_guard lock{ this->synchronize() };
        FsObjectCommit commit;
        commit.time = m_clock->getCurrentTime();
        fillFile(file, commit, new_chunks);
        updateEltPtr(file);
    }

    void FsStorageInMemory::fillFile(FsFilePtr& file, FsObjectCommit& commit, std::vector<ChunkOrRawData> new_chunks) {

        //note: the shuffle of chunks isn't detected, but it's something that can't happen.
        //  Files are modified completly, or at the end.
        // It something is added at the start, or in the middle, then "evrything modified" is detected. I'm not git.
        assert(!file->isObsolete());
        std::vector<FsID> old_chunks = file->getCurrent();
        //create the updating request
        std::vector<FsID> new_file_content;
        std::vector<size_t> new_file_size;
        size_t min_size = std::min(new_chunks.size(), old_chunks.size());
        size_t idx = 0;
        for (; idx < min_size; idx++) {
            if (new_chunks[idx].chunk_id != 0) {
                assert(new_chunks[idx].raw_data == nullptr && new_chunks[idx].raw_data_size == 0);
                if (old_chunks[idx] != new_chunks[idx].chunk_id) {
                    commit.changes.push_back({ old_chunks[idx], new_chunks[idx].chunk_id });
                }
                new_file_content.push_back(new_chunks[idx].chunk_id);
                new_file_size.push_back(new_chunks[idx].chunk_size);
            } else {
                assert(new_chunks[idx].raw_data != nullptr && new_chunks[idx].raw_data_size > 0);
                FsID new_id = getNextId(FsType::CHUNK);
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{
                    new FsChunkInMemory {new_id, commit.time, new_chunks[idx].raw_data,
                    compute_naive_hash(new_chunks[idx].raw_data, new_chunks[idx].raw_data_size), new_chunks[idx].raw_data_size} };
                if (commit.id == 0) { commit.id = new_id; }
                updateEltPtr(new_chunk);
                commit.changes.push_back({ old_chunks[idx], new_id });
                new_file_content.push_back(new_id);
                new_file_size.push_back(new_chunk->size());
            }
        }
        for (; idx < old_chunks.size(); idx++) {
            commit.changes.push_back({ old_chunks[idx], 0 });
        }
        for (; idx < new_chunks.size(); idx++) {
            if (new_chunks[idx].chunk_id != 0) {
                assert(new_chunks[idx].raw_data == nullptr && new_chunks[idx].raw_data_size == 0);
                commit.changes.push_back({ 0, new_chunks[idx].chunk_id });
                new_file_content.push_back(new_chunks[idx].chunk_id);
                new_file_size.push_back(new_chunks[idx].chunk_size);
            } else {
                assert(new_chunks[idx].raw_data != nullptr && new_chunks[idx].raw_data_size > 0);
                FsID new_id = getNextId(FsType::CHUNK);
                std::shared_ptr<FsChunkInMemory> new_chunk = std::shared_ptr<FsChunkInMemory>{
                    new FsChunkInMemory {new_id, commit.time, new_chunks[idx].raw_data,
                    compute_naive_hash(new_chunks[idx].raw_data, new_chunks[idx].raw_data_size), new_chunks[idx].raw_data_size} };
                if (commit.id == 0) { commit.id = new_id; }
                updateEltPtr(new_chunk);
                commit.changes.push_back({ 0, new_id });
                new_file_content.push_back(new_id);
                new_file_size.push_back(new_chunk->size());
            }
        }
        if (commit.id == 0) { commit.id = getNextId(FsType::NONE); }
        // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
        while (!file->modify(FsFileInMemoryFactory{ file.get() }.replaceContentWithSize(new_file_content, new_file_size, commit)->create(file.get()))) {
            file = FsFile::refresh(file);
            assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
        }
        file = FsFile::refresh(file); // set the new file ptr (the one we just created)
    }

    FsFilePtr FsStorageInMemory::createNewFile(FsDirPtr& directory_parent, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights, uint32_t group_id, FsFilePtr from) {
        assert(directory_parent);
        assert(!name.empty());
        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::FILE);
        DateTime time = (DateTime)m_clock->getCurrentTime();
        std::shared_ptr<FsFile> new_file;
        if (from) {
            new_file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {new_id, time, name, rights, group_id, directory_parent->getId(), uint16_t(directory_parent->getDepth() + 1), from->getId(),
                {}, {}, 0, 0, {} } };
        } else {
            new_file = std::shared_ptr<FsFileInMemory>{ new FsFileInMemory {new_id, time, name, rights, group_id, directory_parent->getId(), uint16_t(directory_parent->getDepth() + 1), 0,
                {}, {}, 0, 0, {} } };
        }

        // add chunks inside the file (reuse our method: warning: new_file ptr is modified)
        FsObjectCommit commit{ 0, time, {} };
        if (!chunks.empty()) {
            fillFile(new_file, commit, chunks);
            updateEltPtr(new_file); // add file before referencing it in the parent.
        }

        // modify the directory (create factory; call the modification method; create the new directory; assign it inside the revision line; verify we were already at the last revision).
        while (!directory_parent->modify(FsDirectoryInMemoryFactory{ directory_parent.get() }.addFile(new_file)->create(directory_parent.get()))) {
            directory_parent = FsDirectory::refresh(directory_parent);
            assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
        }
        directory_parent = FsDirectory::refresh(directory_parent); // set the new directory ptr (the one we just created)
        updateEltPtr(directory_parent);

        return new_file;
    }

    FsFilePtr FsStorageInMemory::modifyFile(FsFilePtr& old_file, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights, uint32_t group_id) {
        std::lock_guard lock{ this->synchronize() };
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory_parent = FsElt::toDirectory(directory_elt); directory_parent) {
            FsID new_id = getNextId(FsType::FILE);
            DateTime time = (DateTime)m_clock->getCurrentTime();
            
            // create new file
            FsFileStub new_pattern{ new_id, time, name, rights, group_id, directory_parent->getId(), old_file->getDepth(), old_file->getId() };
            std::shared_ptr<FsFile> new_file = FsFileInMemoryFactory{ old_file.get() }.create(&new_pattern);
            updateEltPtr(new_file); // has to be added before modifying old file & changing the directory
            
            // modify the directory (create factory; call the modification method; create the new directory; assign it inside the revision line; verify we were already at the last revision).
            while (!directory_parent->modify(FsDirectoryInMemoryFactory{ directory_parent.get() }.replace(old_file->getId(), new_id, time)->create(directory_parent.get()))) {
                directory_parent = FsDirectory::refresh(directory_parent);
                assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
            }
            directory_parent = FsDirectory::refresh(directory_parent); // set the new directory ptr (the one we just created)
            updateEltPtr(directory_parent); // has to be modified before making old_file as removed.

            // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
            while (!old_file->modify(FsFileInMemoryFactory{ old_file.get() }.remove(time, new_file->getId())->create(old_file.get()))) {
                old_file = FsFile::refresh(old_file);
                assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
            }
            old_file = FsFile::refresh(old_file); // set the new file ptr (the one we just created)
            updateEltPtr(old_file);

            return new_file;
        }
        assert(false);
        return {};
    }

    FsDirPtr FsStorageInMemory::createNewDirectory(FsDirPtr& directory_parent, const std::string& name, std::vector<FsObjectPtr> data, CUGA rights, uint32_t group, FsDirPtr from) {
        std::vector<FsID> vect;
        if (!data.empty()) {
            for (FsObjectPtr& item : data) {
                vect.push_back(item->getId());
            }
        }
        return createNewDirectory(directory_parent, name, vect, rights, group, from);
    }

    FsDirPtr FsStorageInMemory::createNewDirectory(FsDirPtr& directory_parent, const std::string& name, const std::vector<FsID>& data, CUGA rights, uint32_t group, FsDirPtr from) {
        assert(directory_parent);
        assert(!directory_parent->isObsolete());
        if (!directory_parent) return {};

        std::lock_guard lock{ this->synchronize() };
        FsID new_id = getNextId(FsType::DIRECTORY);
        DateTime time = (DateTime)m_clock->getCurrentTime();
        FsDirectoryStub new_pattern{ new_id, time, name, rights, group, directory_parent->getId(), uint16_t(directory_parent->getDepth() + 1), from ? from->getId() : 0 };
        FsDirectoryInMemoryFactory factory = FsDirectoryInMemoryFactory{ &new_pattern };
        if (!data.empty()) {
            factory.setFiles(data, new_id, time);
        }
        std::shared_ptr<FsDirectory> new_dir = factory.create(&new_pattern);
        updateEltPtr(new_dir); // has to be created before the parent is modified

        // modify the parentdirectory (create factory; call the modification method; create the new directory; assign it inside the revision line; verify we were already at the last revision).
        while (!directory_parent->modify(FsDirectoryInMemoryFactory{ directory_parent.get() }.addDir(new_dir)->create(directory_parent.get()))) {
            directory_parent = FsDirectory::refresh(directory_parent);
            assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
        }
        directory_parent = FsDirectory::refresh(directory_parent); // set the new directory ptr (the one we just created)
        updateEltPtr(directory_parent);

        return new_dir;
    }


    FsDirPtr FsStorageInMemory::modifyDirectory(FsDirPtr& old_dir, const std::string& name, const std::vector<FsID>& data, CUGA rights, uint32_t group_id, FsDirPtr from) {
        std::lock_guard lock{ this->synchronize() };
        FsEltPtr directory_elt = load(old_dir->getParent());
        if (FsDirPtr directory_parent = FsElt::toDirectory(directory_elt); directory_parent) {
            FsID new_id = getNextId(FsType::FILE);
            DateTime time = (DateTime)m_clock->getCurrentTime();

            // create new directory
            FsDirectoryStub new_pattern{ new_id, time, name, rights, group_id, directory_parent->getId(), old_dir->getDepth(), old_dir->getId() };
            std::shared_ptr<FsDirectory> new_dir = FsDirectoryInMemoryFactory{ old_dir.get() }.create(&new_pattern);
            updateEltPtr(new_dir); // has to be added before modifying old file & changing the directory

            // modify the directory (create factory; call the modification method; create the new directory; assign it inside the revision line; verify we were already at the last revision).
            while (!directory_parent->modify(FsDirectoryInMemoryFactory{ directory_parent.get() }.replace(old_dir->getId(), new_id, time)->create(directory_parent.get()))) {
                directory_parent = FsDirectory::refresh(directory_parent);
                assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
            }
            directory_parent = FsDirectory::refresh(directory_parent); // set the new directory_parent ptr (the one we just created)
            updateEltPtr(directory_parent); // has to be modified before making old_file as removed.

            // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
            while (!old_dir->modify(FsDirectoryInMemoryFactory{ old_dir.get() }.remove(time, new_dir->getId())->create(old_dir.get()))) {
                old_dir = FsDirectory::refresh(old_dir);
                assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
            }
            old_dir = FsDirectory::refresh(old_dir); // set the new file ptr (the one we just created)
            updateEltPtr(old_dir);

            return new_dir;
        }
        assert(false);
        return {};
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
    void FsStorageInMemory::deleteObject(FsObjectPtr& old_object) {
        std::lock_guard lock{ this->synchronize() };
        FsEltPtr directory_elt = load(old_object->getParent());
        if (FsDirPtr directory_parent = FsElt::toDirectory(directory_elt); directory_parent) {
            assert(directory_parent != old_object);
            FsID new_id = getNextId(FsType::NONE);
            DateTime time = (DateTime)m_clock->getCurrentTime();
            this->deleteObject(old_object, directory_parent, new_id, time);
            return;
        }
        assert(false);
    }

    void FsStorageInMemory::deleteObject(FsObjectPtr& old_object, FsDirPtr& directory_parent, FsID commit_id, DateTime commit_time, FsID renamed_to) {

        // modify the parentdirectory (create factory; call the modification method; create the new directory; assign it inside the revision line; verify we were already at the last revision).
        while (!directory_parent->modify(FsDirectoryInMemoryFactory{ directory_parent.get() }.delFile(old_object->getId(), commit_id, commit_time)->create(directory_parent.get()))) {
            directory_parent = FsDirectory::refresh(directory_parent);
            assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
        }
        directory_parent = FsDirectory::refresh(directory_parent); // set the new directory ptr (the one we just created)
        updateEltPtr(directory_parent); // dereference before setting the flag

        // suicide the object by setting flags
        if (FsElt::isFile(old_object->getId())) {
            FsFilePtr old_file = std::static_pointer_cast<FsFile>(old_object);
            // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
            while (!old_file->modify(FsFileInMemoryFactory{ old_file.get() }.remove(commit_time, renamed_to)->create(old_file.get()))) {
                old_file = FsFile::refresh(old_file);
                assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
            }
            old_file = FsFile::refresh(old_file); // set the new file ptr (the one we just created)
            updateEltPtr(old_file);
        } else {
            FsDirPtr old_dir = std::static_pointer_cast<FsDirectory>(old_object);
            // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
            while (!old_dir->modify(FsDirectoryInMemoryFactory{ old_dir.get() }.remove(commit_time, renamed_to)->create(old_dir.get()))) {
                old_dir = FsDirectory::refresh(old_dir);
                assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
            }
            old_dir = FsDirectory::refresh(old_dir); // set the new file ptr (the one we just created)
            updateEltPtr(old_dir);
        }
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

    //size_t FsStorageInMemory::createNewFileMergeCommit(FsID file_id, FsObjectCommit& commit, const std::vector<FsID>& old_chunks, const std::vector<FsID>& new_chunks, const size_t new_size) {
    //    std::lock_guard lock{ this->synchronize() };
    //    size_t new_file_size = 0;
    //    size_t min_size = std::min(new_chunks.size(), old_chunks.size());
    //    size_t idx = 0;
    //    bool unknown_size = false;
    //    for (; idx < min_size; idx++) {
    //        if (old_chunks[idx] != new_chunks[idx]) {
    //            commit.changes.push_back({ old_chunks[idx], new_chunks[idx] });
    //            if (FsChunkPtr chunk = loadChunk(new_chunks[idx]); chunk) {
    //                new_file_size += chunk->size();
    //            } else {
    //                //create distant one
    //                std::shared_ptr<FsChunkInMemory> new_stub_chunk = std::shared_ptr<FsChunkInMemory>{
    //                    new FsChunkInMemory{new_chunks[idx], 0, 0, 0} };
    //                m_database[new_chunks[idx]] = new_stub_chunk;
    //                new_file_size += new_stub_chunk->size(); //FIXME?
    //                unknown_size = true;
    //            }
    //        }
    //    }
    //    for (; idx < old_chunks.size(); idx++) {
    //        commit.changes.push_back({ old_chunks[idx], 0 });
    //    }
    //    for (; idx < new_chunks.size(); idx++) {
    //        commit.changes.push_back({ 0, new_chunks[idx] });
    //        if (FsChunkPtr chunk = loadChunk(new_chunks[idx]); chunk) {
    //            new_file_size += chunk->size();
    //        } else {
    //            //create distant one
    //            std::shared_ptr<FsChunkInMemory> new_stub_chunk = std::shared_ptr<FsChunkInMemory>{
    //                new FsChunkInMemory{new_chunks[idx], 0, 0, 0} };
    //            m_database[new_chunks[idx]] = new_stub_chunk;
    //            new_file_size += new_stub_chunk->size(); //FIXME?
    //        }
    //    }
    //    if (new_size != 0 && new_size != size_t(-1)) {
    //        new_file_size = new_size;
    //        assert(unknown_size || new_file_size == new_size);
    //    }
    //    return new_file_size;
    //}

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

    // unknown objects: it creates them and add them into the database, before the parent reference them.
    // modified/deleted file: update to the new version if latest (TODO: and rights are valid)
    // modified directory: update to the last version, and create the subtree of any new items inside.
    void FsStorageInMemory::mergeObjectsCommit(const std::vector<const FsObject*>& commit) {
        std::unordered_map<FsID, const FsElt*> map_commit;
        std::vector<FsObjectPtr> created;
        std::vector<std::pair<FsObjectPtr, FsObjectPtr>> modified;
        std::vector<FsDirPtr> directories;
        for (const FsObject* elt : commit) {
            map_commit[elt->getId()] = elt;
        }

        //for each elt, create something.
        for (const FsObject* new_commit : commit) {
            if (!FsElt::isObject(new_commit->getId())) {
                //shouldn't be possible.
                return;
            }
            const FsFile* new_file_commit = nullptr;
            const FsDirectory* new_dir_commit = nullptr;
            if (FsElt::isFile(new_commit->getId())) {
                new_file_commit = static_cast<const FsFile*>(new_commit);
            } else {
                new_dir_commit = static_cast<const FsDirectory*>(new_commit);
            }
            //get our object
            FsObjectPtr stored_obj = this->loadObject(new_commit->getId());
            if (!stored_obj) {
                // the object isn't in our database
                //-> we create it and add it into the temp database
                // note: have to create the commit if not present
                std::unique_ptr<FsFileInMemoryFactory> file_factory;
                std::unique_ptr<FsDirectoryInMemoryFactory> dir_factory;
                FsObjectFactory* factory;
                if (new_file_commit) {
                    file_factory = std::make_unique<FsFileInMemoryFactory>(new_file_commit);
                    factory = file_factory.get();
                } else {
                    dir_factory = std::make_unique<FsDirectoryInMemoryFactory>(new_dir_commit);
                    factory = dir_factory.get();
                }
                // it should have at least one comit if not empty.
                if (factory->commits.empty()) {
                    assert(factory->current_state.empty());
                    factory->current_state.clear();
                    if (file_factory) {
                        file_factory->current_size.clear();
                    }
                } else {
                    // creation commit only have one commit with only id & time, we have to rebuild them from current state.
                    if (factory->commits.size() > 1) {
                        assert(false);
                        factory->commits.erase(factory->commits.begin(), factory->commits.end() - 1);
                    }
                    factory->commits.back().changes.clear();
                    for (FsID item : factory->current_state) {
                        factory->commits.back().changes.emplace_back( 0, item );
                    }
                }
                //create new object
                if (new_file_commit) {
                    created.push_back(file_factory->create(new_file_commit));
                } else {
                    FsDirPtr new_dir = dir_factory->create(new_dir_commit);
                    created.push_back(new_dir);
                    directories.push_back(new_dir);
                }
            } else {
                // the object is in our database
                stored_obj = FsObject::refresh(stored_obj);

                //check immutable part (if a creation commit)
                if (new_commit->getParent() != 0) {
                    assert(new_commit->getParent() == stored_obj->getParent());
                    assert(new_commit->getName() == stored_obj->getName());
                    assert(new_commit->getGroupId() == stored_obj->getGroupId());
                    assert(new_commit->getCUGA() == stored_obj->getCUGA());
                    assert(new_commit->getRenamedFrom() == stored_obj->getRenamedFrom());
                    bool not_same = false;
                    not_same |= new_commit->getParent() != stored_obj->getParent();
                    not_same |= new_commit->getName() != stored_obj->getName();
                    not_same |= new_commit->getGroupId() != stored_obj->getGroupId();
                    not_same |= new_commit->getCUGA() != stored_obj->getCUGA();
                    not_same |= new_commit->getRenamedFrom() != stored_obj->getRenamedFrom();
                    assert(!not_same);
                    if (not_same) {
                        map_commit.erase(new_commit->getId());
                        continue;
                    }
                }
                // CHECK CREATION? is it only a creation check?
                if (new_commit->getCommits().empty()) {
                    assert(new_commit->getCurrent().empty());
                    //whatever the state, it's already here. And that's what this want.
                    map_commit.erase(new_commit->getId());
                    continue;
                }
                // CHECK DELETION? (newly) deleted?
                if (new_commit->getDeletedDate() > 0 && new_commit->getDeletedDate() >= new_commit->getCreationTime() && stored_obj->getDeletedDate() < stored_obj->getCreationTime()) {
                    //check if we are deleted from our parent in the commit.
                    auto it = map_commit.find(new_commit->getParent());
                    if (it == map_commit.end()) {
                        error("Error: CommitMerge:  can't delete an object without removing it from its parent.");
                        map_commit.erase(new_commit->getId());
                        continue;
                    }
                    if (contains(static_cast<const FsObject*>(it->second)->getCurrent(), new_commit->getId())) {
                        error("Error: CommitMerge:  can't delete an object without removing it from its parent (2).");
                        map_commit.erase(new_commit->getId());
                        continue;
                    }
                    //check renamed
                    if (new_commit->getRenamedTo() > 0) {
                        if (map_commit.find(new_commit->getRenamedTo()) == map_commit.end()) {
                            error("Error: CommitMerge:  can't rename an object to somethign that doesn't exist.");
                            map_commit.erase(new_commit->getId());
                            continue;
                        }
                    }

                    //create the deletion
                    if (new_file_commit) {
                        assert(dynamic_cast<FsFileInMemory*>(stored_obj.get()));
                        const FsFileInMemory* old_file = static_cast<FsFileInMemory*>(stored_obj.get());
                        FsFilePtr new_file = FsFileInMemoryFactory{ old_file }.remove(new_commit->getDeletedDate(), new_commit->getRenamedTo())->create(old_file);
                        modified.emplace_back(new_file, stored_obj);
                    } else {
                        assert(dynamic_cast<FsDirectoryInMemory*>(stored_obj.get()));
                        const FsDirectoryInMemory* old_dir = static_cast<FsDirectoryInMemory*>(stored_obj.get());
                        FsDirPtr new_dir = FsDirectoryInMemoryFactory{ old_dir }.remove(new_commit->getDeletedDate(), new_commit->getRenamedTo())->create(old_dir);
                        modified.emplace_back(new_dir, stored_obj);
                        directories.push_back(new_dir);
                    }
                    continue;
                }
                // IS_MODIFICATION!
                {
                    //now, i'm sure i have a commit -> modification (or deletion?) commit
                    const FsObjectCommit& last_new_commit = new_commit->getCommits().back();
                    //note: even if we are deleted, we can still modify the content. But the "delete-commit" may not have the current state, so we don't touch it when we receive one.

                    // check if we aren't already at this state
                    if (!stored_obj->getCommits().empty() && stored_obj->getCommits().back().id == last_new_commit.id) {
                        assert(stored_obj->getCurrent() == new_commit->getCurrent());
                        //already done, maybe by the parent directory
                        map_commit.erase(new_commit->getId());
                        continue;
                    }
                    //we are different, is the new state newer?
                    //ignore if we have a more recent commit.
                    if (!stored_obj->getCommits().empty() && stored_obj->getCommits().back().time > last_new_commit.time) {
                        //it's more recent, ignore.
                        //TODO: maybe merge the old commit if you have the role (enforced merge?) and it's recent enough.
                        //TODO: maybe add it into the commit history if needed.
                        map_commit.erase(new_commit->getId());
                        continue;
                    }

                    //get previous state for me and his
                    if (new_commit->getCurrent() != stored_obj->getCurrent()) {
                        //check ids (for directory)
                        std::vector<FsID> new_current = new_commit->getCurrent();
                        if (new_dir_commit) {
                            std::unordered_set<FsID> old_ids;
                            for (FsID id : stored_obj->getCurrent()) {
                                old_ids.insert(id);
                            }
                            foreach(it_id, new_current) {
                                if (old_ids.find(*it_id) != old_ids.end()) {
                                    continue;
                                }
                                if (map_commit.find(*it_id) != map_commit.end()) {
                                    continue;
                                }
                                if (this->hasLocally(*it_id)) {
                                    continue;
                                }
                                error("Error: CommitMerge: can't have in a commit an object that doesn't exist.");
                                it_id.erase();
                                assert(false);
                            }
                        }
                        //if (!new_current.empty()) // mayeb it's to revert a change. get this new commit & date to ensure you don't get the useless change.
                        {
                            //create the modification commit
                            FsObjectCommit modif_commit{ last_new_commit.id, last_new_commit.time, {} };
                            //now create a commit to go from my 
                            size_t new_file_size = 0;
                            size_t min_size = std::min(stored_obj->getCurrent().size(), new_current.size());
                            for (size_t idx = 0; idx < min_size; idx++) {
                                if (stored_obj->getCurrent()[idx] != new_current[idx]) {
                                    modif_commit.changes.push_back({ stored_obj->getCurrent()[idx], new_current[idx] });
                                }
                            }
                            for (size_t idx = min_size; idx < stored_obj->getCurrent().size(); idx++) {
                                modif_commit.changes.push_back({ stored_obj->getCurrent()[idx], 0 });
                            }
                            for (size_t idx = min_size; idx < new_current.size(); idx++) {
                                modif_commit.changes.push_back({ 0, new_current[idx] });
                            }

                            //add commit
                            if (FsElt::isFile(new_commit->getId())) {
                                assert(dynamic_cast<FsFileInMemory*>(stored_obj.get()));
                                const FsFileInMemory* old_file = static_cast<FsFileInMemory*>(stored_obj.get());
                                FsFilePtr new_file = FsFileInMemoryFactory{ old_file }.replaceContentWithSize(new_current, new_commit->sizes(), modif_commit)->create(old_file);
                                assert(new_file->size() > 0 || new_current.empty());
                                modified.emplace_back(new_file, stored_obj);
                            } else {
                                assert(dynamic_cast<FsDirectoryInMemory*>(stored_obj.get()));
                                const FsDirectoryInMemory* old_dir = static_cast<FsDirectoryInMemory*>(stored_obj.get());
                                FsDirPtr new_dir = FsDirectoryInMemoryFactory{ old_dir }.replaceContent(new_current, modif_commit)->create(old_dir);
                                modified.emplace_back(new_dir, stored_obj);
                                directories.push_back(new_dir);
                            }
                        }
                    }
                }
            }
        }
        // check that all created objects are inside a created/modified directory
        foreach(it_created, created) {
            FsID id_created = (*it_created)->getId();
            if (id_created == this->getRoot()) continue;
            // search it
            bool found = false;
            for (FsDirPtr& dir : directories) {
                if (contains(dir->getCurrent(), id_created)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                log("Error: CommitMerge: can't create an object that is in no directory.");
                assert(false);
                it_created.erase();
            }
        }

        //update objects in database
        assert(checkFilesystem());
        {
            std::lock_guard lock{ m_database_mutex };
            //update objects
            for (auto& newobj : created) {
                if (m_database.find(newobj->getId()) == m_database.end()) {
                    m_database[newobj->getId()] = newobj;
                } else {
                    log("Warning: CommitMerge: database was modified at the same time a merge occur. It's dangerous and shouldn't happen.");
                }
            }
            for (auto& new_2_old : modified) {
                if (new_2_old.second->modify(new_2_old.first)) {
                    m_database[new_2_old.first->getId()] = new_2_old.first;
                } else {
                    //...
                    log("Error: CommitMerge: the database was modified at the same time a merge occur. A collission has occur. The merge will take precedence.");
                    new_2_old.second->force_modify(new_2_old.first);
                    m_database[new_2_old.first->getId()] = new_2_old.first;
                }
            }
        }
        assert(checkFilesystem());

        //FIXME: update last_modification_date ?

        //now, all objects are in new2old
        //we still have to remove unfindable object from directories.
        //// special case: we have to create the root
        //if (m_database.empty() && commit.find(this->getRoot()) != commit.end()) {
        //    // root, do we have a root? not yet! strange...
        //    assert(new_commit.getId() == getRoot());
        //    assert(m_database.empty());
        //    // create root
        //    FsDirPtr root_dir = this->createNewRoot();
        //    // fill root
        //    m_database[root_dir->getId()] = root_dir = FsDirectoryInMemoryFactory{ *root_dir }.setFiles(new_commit.getCurrent(), new_commit.getId(), new_commit.getCreationTime())->create(*root_dir);
        //    return true;
        //}
    }

    //bool FsStorageInMemory::mergeObjectCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) {
    //    std::lock_guard lock{ this->synchronize() };

    //    //get our file
    //    FsObjectPtr good_obj = this->loadObject(new_commit.getId());
    //    if (!good_obj) {
    //        //create the object
    //        //ensure the parent exists
    //        FsDirPtr parent = this->loadDirectory(new_commit.getParent());
    //        if (!parent) {
    //            // special case: our database is empty, we have to create the root
    //            if (new_commit.getParent() == new_commit.getId()) {
    //                // root, do we have a root? not yet! strange...
    //                assert(new_commit.getId() == getRoot());
    //                assert(m_database.empty());
    //                // create root
    //                FsDirPtr root_dir = this->createNewRoot();
    //                // fill root
    //                m_database[root_dir->getId()] = root_dir = FsDirectoryInMemoryFactory{ *root_dir }.setFiles(new_commit.getCurrent(), new_commit.getId(), new_commit.getCreationTime())->create(*root_dir);
    //                return true;
    //            }
    //            parent = loadDirectory(new_commit.getParent());
    //            assert(parent);
    //        }
    //        //please merge only known objects. will call it right now ?
    //        if (auto it = extra_db.find(new_commit.getParent()); it != extra_db.end()) {
    //            // parent is inside the list, so i can create myself

    //        } else {
    //            error("Error, try to insert an object that has no parent.");
    //            assert(false);
    //            return false;
    //        }

    //        ////we should be inside the parent current state (or maybe it's not done yet, as it's in the extra_db.
    //        //if (!contains(parent->getCurrent(), new_commit.getId())) {
    //        //    if (auto it = extra_db.find(new_commit.getParent()); it == extra_db.end()
    //        //        || !contains(static_cast<const FsObject*>(it->second)->getCurrent(), new_commit.getId())) {
    //        //        error("Error, try to insert an object into a parent that hasn't it inside.");
    //        //        assert(false);
    //        //        return false;
    //        //    } else {
    //        //        // please only merge the roots.
    //        //        return false;
    //        //    }
    //        //}
    //        ////ok, we have a parent and it has us inside
    //        //if (FsElt::isFile(new_commit.getId())) {
    //        //    const FsFile* new_commit_as_file = static_cast<const FsFile*>(&new_commit);
    //        //    FsFileInMemoryPtr new_file = FsFileInMemoryFactory{ *new_commit_as_file }.create(*new_commit_as_file);
    //        //    new_file->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(parent));
    //        //    m_database[new_file->getId()] = new_file;
    //        //    ((FsDirectoryInMemory*)parent.get())->notifyModificationChained(m_updater, new_commit.getId(), new_commit.getCreationTime());
    //        //} else {
    //        //    const FsDirectory* new_commit_as_dir = static_cast<const FsDirectory*>(&new_commit);
    //        //    FsDirectoryInMemoryFactory create_new_dir{ *new_commit_as_dir };

    //        //    //verify that each entry is inside the extra_db
    //        //    create_new_dir.commits.clear(); //don't keep historay, only the last one.
    //        //    FsObjectCommit& create_commit = create_new_dir.commits.emplace_back(FsObjectCommit{ new_commit.getId(), new_commit.getCreationTime(), {} });
    //        //    if (!create_new_dir.current_state.empty()) {
    //        //        foreach(it_id, create_new_dir.current_state) {
    //        //            if (!verify_extra_db_contains(*it_id, extra_db) || !FsElt::isObject(*it_id)) {
    //        //                error("Error, the directory contains an unkown entry");
    //        //                it_id.erase();
    //        //            } else {
    //        //                create_commit.changes.push_back({ 0,*it_id });
    //        //            }
    //        //        }
    //        //    }
    //        //    //create the dir.create(*new_commit_as_dir);
    //        //    std::shared_ptr<FsDirectoryInMemory> new_dir = create_new_dir.create(*new_commit_as_dir);
    //        //    new_dir->set_loaded_parent(std::dynamic_pointer_cast<FsDirectoryInMemory>(parent));
    //        //    m_database[new_dir->getId()] = new_dir;

    //        //    //add it inside parent?
    //        //    while (!parent->modify(FsDirectoryInMemoryFactory{ *parent }.addDir(new_dir)->create(*parent))) {
    //        //        parent = FsDirectory::refresh(parent);
    //        //        assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
    //        //    }
    //        //    parent = m_database[parent->getId()] = FsDirectory::refresh(parent); // set the new directory ptr (the one we just created)

    //        //    
    //        //    //now add childs
    //        //    for (const FsID& id : content) {
    //        //        const FsElt* elt = extra_db.at(id);
    //        //        this->mergeFileCommit(*(static_cast<const FsObject*>(elt)), extra_db);
    //        //    }
    //        //}
    //        //now we have our object inside our database (and its childs if directory).
    //        //return true;
    //    } else { // this obj is already here.
    //        //check immutable part (if a creation commit)
    //        if (new_commit.getParent() != 0) {
    //            assert(new_commit.getParent() == good_obj->getParent());
    //            assert(new_commit.getName() == good_obj->getName());
    //            assert(new_commit.getGroupId() == good_obj->getGroupId());
    //            assert(new_commit.getCUGA() == good_obj->getCUGA());
    //            assert(new_commit.getRenamedFrom() == good_obj->getRenamedFrom());
    //            bool not_same = false;
    //            not_same |= new_commit.getParent() != good_obj->getParent();
    //            not_same |= new_commit.getName() != good_obj->getName();
    //            not_same |= new_commit.getGroupId() != good_obj->getGroupId();
    //            not_same |= new_commit.getCUGA() != good_obj->getCUGA();
    //            not_same |= new_commit.getRenamedFrom() != good_obj->getRenamedFrom();
    //            assert(!not_same);
    //            if (not_same) return false;
    //        }
    //        // IS_CREATION? is it only a creation check?
    //        if (new_commit.getCommits().size() == 0) {
    //            assert(new_commit.getCurrent().empty());
    //            //whatever the state, it's here. And that's what this want.
    //            return true;
    //        }
    //        // IS_DELETION? (newly) deleted?
    //        if (new_commit.getDeletedDate() > 0 && new_commit.getDeletedDate() >= new_commit.getCreationTime() && good_obj->getDeletedDate() < good_obj->getCreationTime()) {
    //            //check if we are deleted from our parent in the commit.
    //            auto it = extra_db.find(new_commit.getParent());
    //            if (it == extra_db.end()) {
    //                error("error, can't delete an object without removing it from its parent.");
    //                assert(false);
    //                return true;
    //            }
    //            if (contains(static_cast<const FsObject*>(it->second)->getCurrent(), new_commit.getId())) {
    //                error("error, can't delete an object without removing it from its parent (2).");
    //                assert(false);
    //                return true;
    //            }
    //            //delete us (parent will be done in its turn)
    //            if (FsElt::isFile(new_commit.getId())) {
    //                FsFilePtr old_file = std::static_pointer_cast<FsFile>(good_obj);
    //                // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
    //                while (!old_file->modify(FsFileInMemoryFactory{ *old_file }.remove(new_commit.getDeletedDate(), new_commit.getRenamedTo())->create(*old_file))) {
    //                    old_file = FsFile::refresh(old_file);
    //                    assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
    //                }
    //                old_file = m_database[old_file->getId()] = FsFile::refresh(old_file); // set the new file ptr (the one we just created)
    //            } else {
    //                FsDirPtr old_dir = std::static_pointer_cast<FsDirectory>(good_obj);
    //                // modify the file (create factory; call the modification method; create the new file; assign it inside the revision line; verify we were already at the last revision).
    //                while (!old_dir->modify(FsDirectoryInMemoryFactory{ *old_dir }.remove(new_commit.getDeletedDate(), new_commit.getRenamedTo())->create(*old_dir))) {
    //                    old_dir = FsDirectory::refresh(old_dir);
    //                    assert(false); // it's a bit dangerous, please be sure to have lock the access to it & updated your FsFilePtr before calling this.
    //                }
    //                old_dir = m_database[old_dir->getId()] = FsDirectory::refresh(old_dir); // set the new file ptr (the one we just created)
    //            }
    //            return false;
    //        }
    //        // IS_MODIFICATION!
    //        {
    //            //now, i'm sure i have a commit -> modification (or deletion?) commit
    //            const FsObjectCommit& last_new_commit = new_commit.getCommits().back();
    //            //note: even if we are deleted, we can still modify the content. But the "delete-commit" may not have the current state, so we don't touch it when we receive one.

    //            // check if we aren't already at this state
    //            if (good_obj->getCommitsSize() > 0 && good_obj->backCommit().id == new_commit.backCommit().id) {
    //                //already done, maybe by the parent directory
    //                return good_obj->getDeletedDate() < good_obj->getCreationTime();
    //            }
    //            //we are different, is the new state newer?
    //            //ignore if we have a more recent commit.
    //            if (good_obj->getCommitsSize() > 0 && good_obj->backCommit().date > last_new_commit.date) {
    //                //it's more recent, ignore.
    //                //TODO: maybe merge the old commit if you have the role (enforced merge?) and it's recent enough.
    //                return good_obj->getDeletedDate() < good_obj->getCreationTime();
    //            }

    //            //get previous state for me and his
    //            if (new_commit.getCurrent() != good_obj->getCurrent()) {
    //                //create the modification commit
    //                FsObjectCommit modif_commit{ last_new_commit.id, last_new_commit.date, {} };
    //                //now create a commit to go from my 
    //                size_t new_file_size = 0;
    //                size_t min_size = std::min(new_commit.getCurrent().size(), good_obj->getCurrent().size());
    //                for (size_t idx = 0; idx < min_size; idx++) {
    //                    if (good_obj->getCurrent()[idx] != new_commit.getCurrent()[idx]) {
    //                        modif_commit.changes.push_back({ good_obj->getCurrent()[idx], new_commit.getCurrent()[idx] });
    //                    }
    //                }
    //                for (size_t idx = min_size; idx < good_obj->getCurrent().size(); idx++) {
    //                    modif_commit.changes.push_back({ good_obj->getCurrent()[idx], 0 });
    //                }
    //                for (size_t idx = min_size; idx < new_commit.getCurrent().size(); idx++) {
    //                    modif_commit.changes.push_back({ 0, new_commit.getCurrent()[idx] });
    //                }

    //                //add commit
    //                if (FsElt::isFile(new_commit.getId())) {
    //                    static_cast<FsFileInMemory*>(good_obj.get())->replaceContent(new_commit.getCurrent(), new_commit.sizes(), modif_commit);
    //                } else {
    //                    static_cast<FsDirectoryInMemory*>(good_obj.get())->replaceContent(new_commit.getCurrent(), modif_commit);
    //                }
    //            }
    //            return good_obj->getDeletedDate() < good_obj->getCreationTime();
    //        }
    //    }
    //    assert(false);
    //}

    //bool FsStorageInMemory::mergeFileCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) {
    //    return mergeObjectCommit(new_commit, extra_db);
    //}

    //bool FsStorageInMemory::mergeDirectoryCommit(const FsObject& new_commit, const std::unordered_map<FsID, const FsElt*>& extra_db) {
    //    return mergeObjectCommit(new_commit, extra_db);
    //}


    bool FsStorageInMemory::checkFilesystem() {
        size_t errors_found = 0;
        if (m_database.empty()) return true;
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
                    for (size_t commit_idx = 0; commit_idx < obj->getCommits().size(); ++commit_idx) {
                        if (last_time > obj->getCommits()[commit_idx].time) {
                            error(std::string("Error: in the fs object (") + cur_id + "), commits are not ordered by time.");
                            ++errors_found;
                            assert(false);
                        }
                        for (const auto& id2id : obj->getCommits()[commit_idx].changes) {
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
                                    assert(false);
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
                        assert(false);
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
                assert(false);
            }
        }

        //check that id & type are good
        for (auto& id_2_elt : m_database) {
            FsType type = FsElt::getType(id_2_elt.first);
            FsElt* elt = id_2_elt.second.get();
            if (type == FsType::CHUNK) {
                if (dynamic_cast<FsChunkInMemory*>(elt) == nullptr) {
                    ++errors_found;
                    error(std::string("Error: an item that should be a chunk isn't a chunk."));
                    assert(false);
                }
            } else if (type == FsType::FILE) {
                if (dynamic_cast<FsFileInMemory*>(elt) == nullptr) {
                    ++errors_found;
                    error(std::string("Error: an item that should be a file isn't a chunk."));
                    assert(false);
                }
            } else if (type == FsType::DIRECTORY) {
                if (dynamic_cast<FsDirectoryInMemory*>(elt) == nullptr) {
                    ++errors_found;
                    error(std::string("Error: an item that should be a dir isn't a chunk."));
                    assert(false);
                }
            } else {
                ++errors_found;
                error(std::string("Error: an item as a commit id."));
                assert(false);
            }
        }

        // check??? TODO
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
