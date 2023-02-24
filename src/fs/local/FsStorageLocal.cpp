#include "FsStorageLocal.hpp"

#include "FsChunkLocal.hpp"
#include "FsDirLocal.hpp"
#include "FsFileLocal.hpp"
#include "fs/base/FsStorage.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"
#include "utils/Utils.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace supercloud {

    FsEltPtr FsStorageLocal::deserialize(ByteBuff& buffer, FsStorageLocal& database) {
        FsID id = buffer.getULong();
        if (FsElt::isChunk(id)) {
            return FsChunkLocal::deserialize(id, buffer, database);
        } else  if (FsElt::isFile(id)) {
            return FsFileLocal::deserialize(id, buffer);
        } else if (FsElt::isDirectory(id)) {
            return FsDirectoryLocal::deserialize(id, buffer);
        } else {
            throw std::exception("error, unknown id");
        }
    }
    void FsStorageLocal::serialize(FsEltPtr obj, ByteBuff& buffer) {
        FsID id = obj->getId();
        if (FsElt::isChunk(id)) {
            FsChunkLocal::serialize((FsChunkLocal*)obj.get(), buffer);
        } else  if (FsElt::isFile(id)) {
            FsFileLocal::serialize((FsFileLocal*)obj.get(), buffer);
        } else if (FsElt::isDirectory(id)) {
            FsDirectoryLocal::serialize((FsDirectoryLocal*)obj.get(), buffer);
        } else {
            throw std::exception("errro, unknown id");
        }
    }

    void FsObjectLocal::set(std::shared_ptr<FsDirectoryLocal> parent, const std::filesystem::path& new_path) {
        m_loaded_parent = parent;
        m_real_file_path = new_path;
    }

    FsID FsStorageLocal::getNextId() {
        return FsElt::createId(m_id_generator.fetch_add(4), m_cid);
    }

    bool FsStorageLocal::hasLocally(FsID id) { 
        return database.find(id) != database.end();
    }

    FsEltPtr FsStorageLocal::load(FsID id) {
        auto it = database.find(id);
        if (it != database.end())
            return it->second;
        return {};
    }

    FsID FsStorageLocal::getRoot() {
        return m_root_id;
    }

    FsDirPtr FsStorageLocal::createNewRoot() {
        FsDirPtr root = this->createNewDirectory({}, "", {}, CUGA_7777);
        m_root_id = root->getId();
        return root;
    }

    //Reuse modifyFile
    FsChunkPtr FsStorageLocal::addChunkToFile(FsFilePtr file, ByteBuff data) {
        std::vector<ChunkOrRawData> new_chunks;
        for (FsID& chunk_id : file->getCurrent()) {
            new_chunks.push_back(ChunkOrRawData{ FsElt::toChunk(this->load(chunk_id)), {} });
        }
        new_chunks.push_back(ChunkOrRawData{ {}, std::shared_ptr<ByteBuff>{new ByteBuff{data}} });
        this->modifyFile(file, new_chunks);
        return FsElt::toChunk(this->load(file->getCurrent().back()));
    }

    //reuse modifyFile
    FsChunkPtr FsStorageLocal::modifyChunk(FsFilePtr file, FsChunkPtr old_chunk, ByteBuff new_data) {
        std::vector<ChunkOrRawData> new_chunks;
        size_t pos = 0, old_chunk_pos = 0;
        for (FsID& chunk_id : file->getCurrent()) {
            FsChunkPtr chunk = FsElt::toChunk(this->load(chunk_id));
            if (chunk != old_chunk) {
                new_chunks.push_back(ChunkOrRawData{ chunk, {} });
            } else {
                old_chunk_pos = pos;
                new_chunks.push_back(ChunkOrRawData{ {}, std::shared_ptr<ByteBuff>{new ByteBuff{new_data}} });
            }
            pos++;
        }
        this->modifyFile(file, new_chunks);
        return FsElt::toChunk(this->load(file->getCurrent()[old_chunk_pos]));
    }

    void FsStorageLocal::modifyFile(FsFilePtr file, std::vector<ChunkOrRawData> new_chunks) {
        FsFileLocal* file_ptr = ((FsFileLocal*)file.get());
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
        size_t offset = 0;
        size_t idx = 0;
        std::vector< std::pair<FsChunkLocal*, size_t>> chunk2offset;
        for (; idx < min_size; idx++) {
            if (new_chunks[idx].chunk) {
                if (old_chunks[idx] != new_chunks[idx].chunk->getId()) {
                    commit.push_back({ old_chunks[idx], new_chunks[idx].chunk->getId() });
                }
                new_file_content.push_back(new_chunks[idx].chunk->getId());
                chunk2offset.push_back({ (FsChunkLocal*)new_chunks[idx].chunk.get(), offset });
                //assert(offset == ((FsChunkLocal*)new_chunks[idx].chunk.get())->getOffset());
                offset += new_chunks[idx].chunk->currentSize();
            } else {
                FsID new_id = FsElt::setChunk(getNextId());
                std::shared_ptr<FsChunkLocal> new_chunk = std::shared_ptr<FsChunkLocal>{ 
                    new FsChunkLocal {new_id, commit_time, *this, new_chunks[idx].raw_data->available(), FsChunk::getHash(*new_chunks[idx].raw_data) } };
                if (commit_id == 0) { commit_id = new_id; }
                database[new_id] = new_chunk;
                commit.push_back({ old_chunks[idx], new_id });
                new_file_content.push_back(new_id);
                chunk2offset.push_back({ new_chunk.get(), offset });
                offset += new_chunks[idx].raw_data->available();
            }
        }
        for (; idx < old_chunks.size(); idx++) {
            commit.push_back({ old_chunks[idx], 0 });
        }
        for (; idx < new_chunks.size(); idx++) {
            if (new_chunks[idx].chunk) {
                commit.push_back({ 0, new_chunks[idx].chunk->getId() });
                new_file_content.push_back(new_chunks[idx].chunk->getId());
                chunk2offset.push_back({ (FsChunkLocal*)new_chunks[idx].chunk.get(), offset });
                offset += new_chunks[idx].chunk->currentSize();
            } else {
                FsID new_id = FsElt::setChunk(getNextId());
                std::shared_ptr<FsChunkLocal> new_chunk = std::shared_ptr<FsChunkLocal>{ 
                    new FsChunkLocal {new_id, commit_time, *this, new_chunks[idx].raw_data->available(), FsChunk::getHash(*new_chunks[idx].raw_data)} };
                if (commit_id == 0) { commit_id = new_id; }
                database[new_id] = new_chunk;
                commit.push_back({ 0, new_id });
                new_file_content.push_back(new_id);
                chunk2offset.push_back({ new_chunk.get(), offset });
                offset += new_chunks[idx].raw_data->available();
            }
        }
        if (commit_id == 0) { commit_id = FsElt::setNone(getNextId()); }
        //replace content (metadata & data)
        //TODO: copy the current file, and only modify the bits that aren't in the current commit.
        ByteBuff buff;
        for (auto item : new_chunks) {
            if (item.raw_data) {
                buff.put(*item.raw_data);
            } else {
                item.chunk->read(buff, 0, item.chunk->currentSize());
            }
        }
        file_ptr->replaceContent(new_file_content, commit, commit_id, commit_time, buff.flip());
        for (size_t idx = 0; idx < chunk2offset.size(); ++idx) {
            chunk2offset[idx].first->addParent(file->getId(), commit_id, chunk2offset[idx].second);
        }
    }

    FsFilePtr FsStorageLocal::createNewFile(FsDirPtr directory, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        FsID new_id = FsElt::setFile(getNextId());
        DateTime time = (DateTime)get_current_time_milis();
        std::shared_ptr<FsFileLocal> new_file = std::shared_ptr<FsFileLocal>{ new FsFileLocal {new_id, time, name, rights, directory->getId()} };
        std::filesystem::path new_file_path = ((FsDirectoryLocal*)directory.get())->getRealPath() / name;
        assert(!std::filesystem::exists(new_file_path));
        new_file->set(std::dynamic_pointer_cast<FsDirectoryLocal>(directory), new_file_path);
        modifyFile(new_file, chunks);
        database[new_file->getId()] = new_file;
        ((FsDirectoryLocal*)directory.get())->addFile(new_file);
        ((FsDirectoryLocal*)directory.get())->notifyModificationChained(new_file->getId(), time);
        return new_file;
    }

    FsFilePtr FsStorageLocal::modifyFile(FsFilePtr old_file, const std::string& name, std::vector<ChunkOrRawData> chunks, CUGA rights) {
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            FsObjectLocal* old_file_ptr = ((FsObjectLocal*)old_file.get());

            FsID new_id = FsElt::setFile(getNextId());
            DateTime time = (DateTime)get_current_time_milis();
            //copy old into "deleted" directory
            std::filesystem::path current_path = old_file_ptr->getRealPath();
            std::filesystem::path deleted_path = this->m_deleted_dir / std::to_string(old_file->getId());
            std::filesystem::rename(current_path, deleted_path);
            old_file_ptr->remove(time, new_id, deleted_path);
            //create new
            std::shared_ptr<FsFileLocal> new_file = std::shared_ptr<FsFileLocal>{ new FsFileLocal {new_id, time, name, rights, directory->getId()} };
            new_file->set(std::dynamic_pointer_cast<FsDirectoryLocal>(directory), current_path);

            if (!chunks.empty()) {
                modifyFile(new_file, chunks);
            } else {
                auto myfile = std::fstream(current_path, std::ios::out | std::ios::binary);
                myfile.close();
            }
            database[new_file->getId()] = new_file;
            ((FsDirectoryLocal*)directory.get())->addFile(new_file);
            ((FsDirectoryLocal*)directory.get())->notifyModificationChained(new_file->getId(), time);
            return new_file;
        }
        assert(false);
        return {};
    }

    FsDirPtr FsStorageLocal::createNewDirectory(FsDirPtr directory_parent, const std::string& name, std::vector<FsObjectPtr> data, CUGA rights) {
        FsID new_id = FsElt::setDirectory(getNextId());
        DateTime time = (DateTime)get_current_time_milis();
        std::shared_ptr<FsDirectoryLocal> new_dir = std::shared_ptr<FsDirectoryLocal>{ new FsDirectoryLocal {new_id, time, name, rights, directory_parent ? directory_parent->getId() : new_id} };
        if (!data.empty()) {
            std::vector<FsID> vect;
            for (FsObjectPtr& item : data) {
                vect.push_back(item->getId());
            }
            new_dir->setFiles(vect, new_dir->getId(), time);
        }
        database[new_dir->getId()] = new_dir;
        std::filesystem::path new_dir_path;
        if (directory_parent) {
            new_dir_path = ((FsDirectoryLocal*)directory_parent.get())->getRealPath() / name;
            assert(!std::filesystem::exists(new_dir_path));
            std::filesystem::create_directory(new_dir_path);
        } else {
            new_dir_path = this->m_root_dir;
            if (!std::filesystem::exists(new_dir_path)) {
                std::filesystem::create_directories(new_dir_path);
            }
        }
        new_dir->set(std::dynamic_pointer_cast<FsDirectoryLocal>(directory_parent), new_dir_path);
        if (directory_parent) {
            ((FsDirectoryLocal*)directory_parent.get())->addDir(new_dir);
            new_dir->notifyModificationChained(new_dir->getId(), time);
        }
        return new_dir;
    }

    std::string FsStorageLocal::remove_root_dir_prefix(const std::filesystem::path& full_path) {
        std::string old = full_path.string();
        std::string root = this->m_root_dir.string();
        size_t cut_pos = old.find_first_of(root);
        assert(cut_pos > 0 && cut_pos < old.size() && cut_pos != std::string::npos);
        return old.substr(cut_pos);
    }

    void FsStorageLocal::deleteObject(FsObjectPtr old_file) {
        FsEltPtr directory_elt = load(old_file->getParent());
        if (FsDirPtr directory = FsElt::toDirectory(directory_elt); directory) {
            assert(directory != old_file);
            FsID new_id = FsElt::setFile(getNextId());
            DateTime time = (DateTime)get_current_time_milis();
            //copy into "deleted" directory
            std::filesystem::path old_path = ((FsObjectLocal*)old_file.get())->getRealPath();
            std::filesystem::path deleted_path = this->m_deleted_dir / std::to_string(old_file->getId());
            //std::filesystem::copy(old_path, deleted_path, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
            std::filesystem::rename(old_path, deleted_path);
            ((FsObjectLocal*)old_file.get())->remove(time, 0, deleted_path);
            ((FsDirectoryLocal*)directory.get())->delFile(old_file->getId(), new_id, time);
            ((FsDirectoryLocal*)directory.get())->notifyModificationChained(new_id, time);
            return;
        }
        assert(false);
    }

    void FsStorageLocal::save_all(const std::filesystem::path& file) {

        ByteBuff buffer;
        buffer.putInt(this->m_cid);
        buffer.putULong(this->m_root_id);
        buffer.putULong(this->m_id_generator.load());
        buffer.putSize(database.size());
        for (auto& id_2_elt : database) {
            buffer.putULong(id_2_elt.first);
            FsStorageLocal::serialize(id_2_elt.second, buffer);
        }
        buffer.flip();
        auto myfile = std::fstream(file, std::ios::out | std::ios::binary);
        myfile.write((char*)buffer.raw_array() + buffer.position(), buffer.limit());
        myfile.close();
    }

    void FsStorageLocal::load_all(const std::filesystem::path& file) {
        std::ifstream infile(file, std::ios_base::binary);
        std::vector<char> vector_buffer{ std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>() };
        infile.close();

        ByteBuff buffer{ (uint8_t*)vector_buffer.data(), vector_buffer.size() };
        this->m_cid = buffer.getInt();
        this->m_root_id = buffer.getULong();
        this->m_id_generator.store(buffer.getULong());
        const size_t nb_elts = buffer.getSize();
        for (size_t idx = 0; idx < nb_elts; ++idx) {
            FsEltPtr elt = FsStorageLocal::deserialize(buffer, *this);
            database[elt->getId()] = elt;
        }
        //set loaded_parent property
        for (auto& it : database) {
            if (auto chunk = FsElt::toChunk(it.second); chunk) {
                // chunk have a pointer to me, so he can do that at will
            } else if (auto file = FsElt::toFile(it.second); chunk) {
                static_cast<FsFileLocal*>(file.get())->set(std::static_pointer_cast<FsDirectoryLocal>(database[file->getParent()]), ((FsFileLocal*)file.get())->getRealPath());
            } else if (auto dir = FsElt::toDirectory(it.second); chunk) {
                static_cast<FsDirectoryLocal*>(dir.get())->set(std::static_pointer_cast<FsDirectoryLocal>(database[file->getParent()]), ((FsDirectoryLocal*)dir.get())->getRealPath());
            }
        }

    }

    std::vector<FsDirPtr> FsStorageLocal::getDirs(FsDirPtr dir) {
        std::vector<FsDirPtr> childs;
        for (FsID child : dir->getCurrent()) {
            if (FsElt::isDirectory(child)) {
                childs.push_back(FsElt::toDirectory(this->load(child)));
            }
        }
        return childs;
    }

    std::vector<FsFilePtr> FsStorageLocal::getFiles(FsDirPtr dir) {
        std::vector<FsFilePtr> childs;
        for (FsID child : dir->getCurrent()) {
            if (FsElt::isFile(child)) {
                childs.push_back(FsElt::toFile(this->load(child)));
            }
        }
        return childs;
    }

}
