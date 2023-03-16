#include "FsExternalInterface.hpp"

#include "SynchroDb.hpp"
#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"

#include <fstream>

namespace supercloud {


    ComputerId FsExternalInterface::getOurComputerId() { return m_db->getComputerId(); }
    uint32_t FsExternalInterface::getUserId(ComputerId cid) { return m_db->getComputerState(cid).user_id; }

    /// <summary>
    /// request a fs object at a path.
    /// </summary>
    /// <param name="path"></param>
    /// <returns></returns>
    std::future<FsExternalInterface::ObjectRequestAnswer> FsExternalInterface::get(const std::filesystem::path& path) {
        std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr{ new std::promise<FsExternalInterface::ObjectRequestAnswer>() };
        std::future<FsExternalInterface::ObjectRequestAnswer> future = promise_ptr->get_future();
        this->get(path, promise_ptr);
        return future;
    }

    void FsExternalInterface::get(const std::filesystem::path& path, std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr) {
        std::lock_guard lock{ m_db->m_file_storage_mutex };
        //check if path is correct
        if (path.empty() || !path.has_root_path()) {
            assert(false);
            promise_ptr->set_value(ObjectRequestAnswer{ "bad path", 9 });
            return;
        }
        //get id from path
        FsStorage& fs = *m_db->m_file_storage;
        //get root
        std::string dir_name = "";
        std::string path_next = path.u8string();
        assert(path_next[0] == path.preferred_separator);
        path_next = path_next.substr(1);
        FsDirPtr dir = fs.loadDirectory(fs.getRoot());
        size_t next_sep = path_next.find_first_of(path.preferred_separator);

        //check if in invalidated id
        if (m_db->isInvalidated(dir->getId())) {
            m_db->askForFsSynch(dir->getId(), [path, promise_ptr, this](FsEltPtr result) { this->get(path, promise_ptr); });
            return;
        }
        while (next_sep != std::string::npos) {
            dir_name = path_next.substr(0, next_sep);
            path_next = path_next.substr(next_sep + 1);
            //get elt from dir
            bool found = false;
            for (const FsID& id_child : dir->getCurrent()) {
                FsDirPtr test_dir = fs.loadDirectory(id_child);
                if (test_dir && test_dir->getName() == dir_name) {
                    dir = test_dir;
                    found = true;
                }
            }
            if (!found) {
                assert(false);
                promise_ptr->set_value(ObjectRequestAnswer{ "not found", 2 });
                return;
            }
            if (m_db->isInvalidated(dir->getId())) {
                m_db->askForFsSynch(dir->getId(), [path, promise_ptr, this](FsEltPtr result) { this->get(path, promise_ptr); });
                return;
            }
            //relance
            next_sep = path_next.find_first_of(path.preferred_separator);
        }
        //found our dir, now get the object
        FsObjectPtr obj;
        if (path_next.empty()) { // was a directory with a path ending with '/' : "/my/dir/"
            obj = dir;
        } else {
            for (const FsID& id_child : dir->getCurrent()) {
                FsObjectPtr test_obj = FsElt::toObject(fs.load(id_child));
                if (test_obj && test_obj->getName() == path_next) {
                    obj = test_obj;
                }
            }
            if (!obj) {
                assert(false);
                promise_ptr->set_value(ObjectRequestAnswer{ "not found", 2 });
                return;
            }
            if (m_db->isInvalidated(dir->getId())) {
                m_db->askForFsSynch(dir->getId(), [path, promise_ptr, this](FsEltPtr result) { this->get(path, promise_ptr); });
                return;
            }
        }
        promise_ptr->set_value(ObjectRequestAnswer{ FsElt::isFile(obj->getId()), obj });
    }
    std::future<FsExternalInterface::ObjectRequestAnswer> FsExternalInterface::get(FsID id) {
        std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr{ new std::promise<FsExternalInterface::ObjectRequestAnswer>() };
        std::future<FsExternalInterface::ObjectRequestAnswer> future = promise_ptr->get_future();
        this->get(id, promise_ptr);
        return future;
    }
    void FsExternalInterface::get(FsID id, std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr) {
        if (!m_db->m_invalidated_2_modifier_notifier.empty()) {
            // maybe we can check for the root to avoid that, but better safe than sorry.
            m_db->askForFsSynch(id, [id, promise_ptr, this](FsEltPtr result) { this->get(id, promise_ptr); });
        } else {
            std::lock_guard lock{ m_db->m_file_storage_mutex };
            FsObjectPtr obj = FsElt::toObject(m_db->m_file_storage->load(id));
            promise_ptr->set_value(ObjectRequestAnswer{ FsElt::isFile(id), obj });
        }

    }
    std::future<FsExternalInterface::ObjectRequestAnswer> FsExternalInterface::contains(FsDirPtr dir, const std::string& name) {
        std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr{ new std::promise<FsExternalInterface::ObjectRequestAnswer>() };
        std::future<FsExternalInterface::ObjectRequestAnswer> future = promise_ptr->get_future();
        this->contains(dir, name, promise_ptr);
        return future;
    }
    void FsExternalInterface::contains(FsDirPtr dir, const std::string& name, std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr) {
        if (!m_db->m_invalidated_2_modifier_notifier.empty()) {
            // maybe we can check for the root to avoid that, but better safe than sorry.
            m_db->askForFsSynch(dir->getId(), [dir, name, promise_ptr, this](FsEltPtr result) { this->contains(dir, name, promise_ptr); });
        } else {
            std::lock_guard lock{ m_db->m_file_storage_mutex };
            FsObjectPtr obj;
            for (const FsID& id_child : dir->getCurrent()) {
                FsObjectPtr test_obj = FsElt::toObject(m_db->m_file_storage->load(id_child));
                if (test_obj && test_obj->getName() == name) {
                    obj = test_obj;
                }
            }
            if (obj) {
                promise_ptr->set_value(ObjectRequestAnswer{ FsElt::isFile(obj->getId()), obj });
            } else {
                promise_ptr->set_value(ObjectRequestAnswer{ "not found", 2 });
            }
        }
    }

    //curently, it's just a sequential call to get(), TODO: improve it by using batchs. (or delete if not used)
    std::future<FsExternalInterface::MultiObjectRequestAnswer> FsExternalInterface::getAll(std::vector<std::filesystem::path> paths) {
        std::shared_ptr<std::promise<FsExternalInterface::MultiObjectRequestAnswer>> promise_ptr{ new std::promise<FsExternalInterface::MultiObjectRequestAnswer>() };
        std::future<FsExternalInterface::MultiObjectRequestAnswer> future = promise_ptr->get_future();
        this->getAll(paths, promise_ptr);
        return future;
    }
    std::future<FsExternalInterface::MultiObjectRequestAnswer> FsExternalInterface::getAll(std::vector<FsID> ids) {
        std::shared_ptr<std::promise<FsExternalInterface::MultiObjectRequestAnswer>> promise_ptr{ new std::promise<FsExternalInterface::MultiObjectRequestAnswer>() };
        std::future<FsExternalInterface::MultiObjectRequestAnswer> future = promise_ptr->get_future();
        this->getAll(ids, promise_ptr);
        return future;
    }
    void FsExternalInterface::getAll(std::vector<std::filesystem::path> paths, std::shared_ptr<std::promise<FsExternalInterface::MultiObjectRequestAnswer>> promise_ptr) {
        std::lock_guard lock{ m_db->m_file_storage_mutex };
        std::vector<FsObjectPtr> ret_list;
        for (const std::filesystem::path& path : paths) {
            std::future<ObjectRequestAnswer> get_answer = this->get(path);
            std::future_status future_status = get_answer.wait_for(std::chrono::seconds(10));
            if (future_status == std::future_status::ready) {
                ret_list.push_back(get_answer.get().object);
            } else {
                promise_ptr->set_value(MultiObjectRequestAnswer{ "Error while waiting for an object", 121 });
                return;
            }
        }
        promise_ptr->set_value(MultiObjectRequestAnswer{ ret_list });
    }
    void FsExternalInterface::getAll(std::vector<FsID> ids, std::shared_ptr<std::promise<FsExternalInterface::MultiObjectRequestAnswer>> promise_ptr){
        std::lock_guard lock{ m_db->m_file_storage_mutex };
        std::vector<FsObjectPtr> ret_list;
        for (const FsID& id : ids) {
            std::future<ObjectRequestAnswer> get_answer = this->get(id);
            std::future_status future_status = get_answer.wait_for(std::chrono::seconds(10));
            if (future_status == std::future_status::ready) {
                ret_list.push_back(get_answer.get().object);
            } else {
                promise_ptr->set_value(MultiObjectRequestAnswer{ "Error while waiting for an object", 121 });
                return;
            }
        }
        promise_ptr->set_value(MultiObjectRequestAnswer{ ret_list });
    }

    /// <summary>
    /// request data from a file oebjct
    /// </summary>
    /// <param name="path"></param>
    /// <returns></returns>
    std::future<FsExternalInterface::RequestAnswer> FsExternalInterface::getData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size) {
        std::shared_ptr<std::promise<RequestAnswer>> promise_ptr{ new std::promise<RequestAnswer>() };
        std::future<RequestAnswer> future = promise_ptr->get_future();
        this->getData(file, buffer, offset, size, promise_ptr);
        return future;
    }
    std::future<FsExternalInterface::RequestAnswer> FsExternalInterface::writeData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size) {
        std::shared_ptr<std::promise<RequestAnswer>> promise_ptr{ new std::promise<RequestAnswer>() };
        std::future<RequestAnswer> future = promise_ptr->get_future();
        this->writeData(file, buffer, offset, size, promise_ptr);
        return future;
    }
    std::future<FsExternalInterface::ObjectRequestAnswer> FsExternalInterface::resize(FsFilePtr file, size_t new_size) {
        std::shared_ptr<std::promise<ObjectRequestAnswer>> promise_ptr{ new std::promise<ObjectRequestAnswer>() };
        std::future<ObjectRequestAnswer> future = promise_ptr->get_future();
        this->resize(file, new_size, promise_ptr);
        return future;
    }

    void FsExternalInterface::getData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size, std::shared_ptr<std::promise<FsExternalInterface::RequestAnswer>> promise_ptr) {
        // don't refresh for chunks, use normal refresh for them.
        //if (!m_db->m_invalidated.empty()) {
        //    // maybe we can check for the root to avoid that, but better safe than sorry.
        //    m_db->askForFsSynch(dir->getId(), [dir, name, promise_ptr, this]() { this->contains(dir, name, promise_ptr); });
        //}
        std::lock_guard lock{ m_db->m_file_storage_mutex };
        size_t file_pos = 0;
        size_t buffer_pos = 0;
        if (file->size() < offset + size) {
            error("warning; reading more bytes than the file possess");
        }
        for (const FsID& id_chunk : file->getCurrent()) {
            FsChunkPtr chunk = m_db->m_file_storage->loadChunk(id_chunk);
            if (!chunk) {
                //ask to load all needed chunks
                m_db->retreiveChunks(file, offset + size, [this, promise_ptr, file, buffer, offset, size](bool success) {
                    assert(success);
                    this->getData(file, buffer, offset, size, promise_ptr);
                });
                return;
            }
            if (file_pos + chunk->size() > offset) {
                size_t read_from_chunk = std::min(chunk->size(), size - buffer_pos);
                assert(read_from_chunk > 0);
                assert(read_from_chunk <= chunk->size());
                size_t chunk_offset = buffer_pos > 0 ? 0 : offset - file_pos;
                assert(chunk_offset >= 0);
                assert(chunk_offset < chunk->size() - 1);
                assert(buffer_pos == 0 || chunk_offset == 0);
                chunk->read(buffer + buffer_pos, chunk_offset, read_from_chunk);
                buffer_pos += read_from_chunk;
                assert(buffer_pos > 0);
                if (buffer_pos >= size) {
                    assert(buffer_pos == size);
                    assert(file_pos + chunk->size() <= offset + size);
                    promise_ptr->set_value(RequestAnswer{ "",0 });
                    return;
                }
            }
            file_pos += chunk->size();
            assert(file_pos > 0);
        }
        //shouldn't be possible, only happening when the file is shorter that the caller think
        promise_ptr->set_value(RequestAnswer{ "File is smaller", 5 });
    }
    void FsExternalInterface::writeData(FsFilePtr file, const uint8_t* buffer, const size_t offset, const size_t size, std::shared_ptr<std::promise<FsExternalInterface::RequestAnswer>> promise_ptr) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
        //check if the file still exists
        if (file->getDeletedDate() > 0) {
            promise_ptr->set_value(RequestAnswer{ "File deleted, can't set data to it anymore", 2 });
            return;
        }
        //check that we have the chunks needed for it
        const size_t old_file_size = file->size();
        const size_t new_file_size = std::max(offset + size, old_file_size);
        std::vector<size_t> chunk_sizes;
        std::vector<ChunkOrRawData> new_content;
        FsChunkPtr first_part;
        size_t first_part_file_pos = 0;
        FsChunkPtr last_part;
        size_t last_part_file_pos = 0;
        size_t first_chunkidx_after_write = 0;
        const std::vector<FsID>& current = file->getCurrent();
        {
            size_t current_file_pos;
            for (size_t idx = 0; idx < current.size(); ++idx) {
                FsChunkPtr chunk = m_db->m_file_storage->loadChunk(current[idx]);
                if (!chunk) {
                    //ask to load all needed chunks (for their size)
                    m_db->retreiveChunks(file, offset + size, [this, promise_ptr, file, buffer, offset, size](bool success) {
                        assert(success); 
                        this->writeData(file, buffer, offset, size, promise_ptr);
                        });
                    return;
                }
                if (current_file_pos + chunk->size() <= offset) {
                    chunk_sizes.push_back(chunk->size());
                    new_content.push_back(ChunkOrRawData{ chunk->getId(), chunk->size(), nullptr, 0 });
                } else if (current_file_pos < offset) {
                    first_part_file_pos = current_file_pos;
                    first_part = chunk;
                } else if (current_file_pos < offset + size && current_file_pos + chunk->size() > offset + size) {
                    last_part_file_pos = current_file_pos;
                    last_part = chunk;
                    first_chunkidx_after_write = idx + 1;
                } else if (current_file_pos + chunk->size() == offset + size) {
                    //just at the right position: no extra data to reuse
                    first_chunkidx_after_write = idx + 1;
                }
                current_file_pos += chunk->size();
                if (current_file_pos >= offset + size) {
                    if (first_chunkidx_after_write == 0) {
                        assert(false); //shouldn't be possible
                        if (current_file_pos == offset + size) {
                            first_chunkidx_after_write = idx + 1;
                        } else {
                            first_chunkidx_after_write = idx;
                        }
                    }
                    break;
                }
            }
        }
        // change chunks
        size_t buffer_pos = 0;
        //size_t file_pos = first_part_file_pos;
        std::vector<uint8_t> temp_buffer_first; // for first 'new' chunk if it's a mix
        std::vector<uint8_t> temp_buffer_last; // for first 'new' chunk if it's a mix
        if (first_part) {
            //choose chunk size
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, chunk_sizes, first_part_file_pos, new_file_size, chunk_sizes.size());
            if (current_chunk_size < offset - first_part_file_pos) {
                //min chunk size: at least what there is in the last chunk (for algo simplification)
                current_chunk_size = offset - first_part_file_pos;
            }
            //have to get the first part of the buffer
            temp_buffer_first.resize(current_chunk_size);
            assert(offset - first_part_file_pos <= current_chunk_size);
            //add first part
            size_t nb_read = offset - first_part_file_pos;
            first_part->read(&temp_buffer_first[0], 0, nb_read);
            //add second part (if any)
            if (nb_read < current_chunk_size) {
                size_t nb_read_new_buffer = current_chunk_size - nb_read;
                std::copy(buffer, buffer + nb_read_new_buffer, &temp_buffer_first[0]);
                buffer_pos += nb_read_new_buffer;
            }
            //create chunk
            chunk_sizes.push_back(temp_buffer_first.size());
            new_content.push_back(ChunkOrRawData{ 0, 0, &temp_buffer_first[0], temp_buffer_first.size() });
        }
        //new chunks
        while (buffer_pos < size) {
            assert(buffer_pos < size);
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, chunk_sizes, first_part_file_pos, new_file_size, chunk_sizes.size());
            if (last_part && current_chunk_size + buffer_pos >= size) {
                //add last part if any
                //compute size
                size_t bytes_from_last = (last_part_file_pos + last_part->size()) - (offset + size);
                assert(bytes_from_last < last_part->size());
                current_chunk_size = bytes_from_last + (size - buffer_pos);
                temp_buffer_last.resize(current_chunk_size);//copy&read
                std::copy(buffer, buffer + (size - buffer_pos), &temp_buffer_last[0]);
                first_part->read(&temp_buffer_last[size - buffer_pos], 0, bytes_from_last);
                new_content.push_back(ChunkOrRawData{ 0, 0, &temp_buffer_last[0], temp_buffer_last.size() });
            } else {
                if (current_chunk_size + buffer_pos > size) {
                    current_chunk_size = size - buffer_pos;
                }
                assert(current_chunk_size < size);
                new_content.push_back(ChunkOrRawData{ 0, 0, buffer + buffer_pos, current_chunk_size });
            }
            buffer_pos += current_chunk_size;
        }
        //add all chunks after the changed part
        size_t size_remaining_after_write = new_file_size - (offset + size);
        for (size_t idx = first_chunkidx_after_write; idx < current.size(); ++idx) {
            new_content.push_back(ChunkOrRawData{ current[idx], size_remaining_after_write, nullptr, 0 });
            size_remaining_after_write = 0;
        }

        //create the commit
        m_db->m_file_storage->modifyFile(file, new_content);
        assert(file->size() == new_file_size);

        }//mutex
        //notify the synch (& maybe peers)
        m_db->notifyObjectChanged(file->getId());
    }

    void FsExternalInterface::resize(FsFilePtr file, size_t new_size, std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
        //check if the file still exists
        if (file->getDeletedDate() > 0) {
            promise_ptr->set_value(ObjectRequestAnswer{ "File deleted, can't set data to it anymore", 2 });
            return;
        }
        //check that we have the chunks needed for it
        const size_t old_file_size = file->size();
        std::vector<size_t> chunk_sizes;
        std::vector<ChunkOrRawData> new_content;
        FsChunkPtr first_part;
        size_t first_part_file_pos = 0;
        const std::vector<FsID>& current = file->getCurrent();
        {
            size_t current_file_pos;
            for (size_t i_id = 0; i_id < current.size(); ++i_id) {
                FsChunkPtr chunk = m_db->m_file_storage->loadChunk(current[i_id]);
                if (!chunk) {
                    //ask to load all needed chunks (for their size)
                    m_db->retreiveChunks(file, std::min(old_file_size, new_size), [this, promise_ptr, file, new_size](bool success) {
                        assert(success);
                        this->resize(file, new_size, promise_ptr);
                    });
                    return;
                }
                if (current_file_pos + chunk->size() <= new_size) {
                    chunk_sizes.push_back(chunk->size());
                    new_content.push_back(ChunkOrRawData{ chunk->getId(), chunk->size(), nullptr, 0 });
                } else if (current_file_pos < new_size) {
                    first_part_file_pos = current_file_pos;
                    first_part = chunk;
                } else {
                    assert(false);
                }
                current_file_pos += chunk->size();
                if (current_file_pos >= new_size) {
                    break;
                }
            }
        }
        // change chunks
        size_t file_pos = first_part_file_pos;
        std::vector<uint8_t> temp_buffer_first; // for first 'new' chunk if it's a mix
        if (first_part) {
            //choose chunk size
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, chunk_sizes, first_part_file_pos, new_size, chunk_sizes.size());
            if (current_chunk_size < first_part->size()) {
                current_chunk_size = first_part->size();
            }
            if (current_chunk_size > new_size - first_part_file_pos) {
                current_chunk_size = new_size - first_part_file_pos;
            }
            //have to get the first part of the buffer
            temp_buffer_first.resize(current_chunk_size);
            //add first part
            size_t nb_read = std::min(first_part->size(), current_chunk_size);
            first_part->read(&temp_buffer_first[0], 0, nb_read);
            if (nb_read < current_chunk_size) {
                //fill the rest with 0 (to avoid memory attacks/leak)
                std::fill(&temp_buffer_first[nb_read], &temp_buffer_first[current_chunk_size], uint8_t(0));
            }
            //create chunk
            chunk_sizes.push_back(temp_buffer_first.size());
            new_content.push_back(ChunkOrRawData{ 0, 0, &temp_buffer_first[0], temp_buffer_first.size() });
            file_pos += current_chunk_size;
        }
        //new chunks
        std::vector<std::vector<uint8_t>> buffers; // for others chunks
        size_t size_remaining_after_chunks = new_size - old_file_size;
        while (file_pos < new_size) {
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, chunk_sizes, first_part_file_pos, new_size, chunk_sizes.size());
            if (current_chunk_size + file_pos > new_size) {
                current_chunk_size = new_size - file_pos;
            }
            std::vector<uint8_t>& curr_buff = buffers.emplace_back(current_chunk_size, uint8_t(0));
            new_content.push_back(ChunkOrRawData{ 0, 0 , &curr_buff[0], current_chunk_size });
            file_pos += current_chunk_size;
        }
        assert(file_pos == new_size);

        //create the commit
        m_db->m_file_storage->modifyFile(file, new_content);
        assert(file->size() == new_size);

        }//mutex
        //notify the synch (& maybe peers)
        m_db->notifyObjectChanged(file->getId());
    }

    /// <summary>
    /// no future yet? (it's not needed?) Maybe it's more future-proof to use them anyway...
    /// </summary>
    /// <param name="obj"></param>
    /// <returns></returns>
    int FsExternalInterface::createFile(FsDirPtr parent_dir, const std::string& name, CUGA rights) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
            m_db->m_file_storage->createNewFile(parent_dir, name, {}, rights);
        }
        m_db->notifyObjectChanged(parent_dir->getId());
        return 0;
    }

    int FsExternalInterface::createDirectory(FsDirPtr parent_dir, const std::string& name, CUGA rights) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
            m_db->m_file_storage->createNewDirectory(parent_dir, name, std::vector<FsID>{}, rights);
        }
        m_db->notifyObjectChanged(parent_dir->getId());
        return 0;
    }

    int FsExternalInterface::removeObject(FsObjectPtr obj) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
        m_db->m_file_storage->deleteObject(obj);
        }
        m_db->notifyObjectChanged(obj->getId());
        m_db->notifyObjectChanged(obj->getParent());
        return 0;
    }

    int FsExternalInterface::moveObject(FsObjectPtr obj, const std::filesystem::path& new_path) {
        FsID new_dir_parent_id = 0;
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
        // === get new parent directory
        //check if path is correct
        if (new_path.empty() || !new_path.has_root_path()) {
            assert(false);
            return 2;
        }
        //get id from path
        FsStorage& fs = *m_db->m_file_storage;
        //get root
        std::string dir_name = "";
        std::string path_next = new_path.u8string();
        //remove first & last '/'
        assert(path_next[0] == new_path.preferred_separator);
        path_next = path_next.substr(1);
        if (path_next.back() == new_path.preferred_separator) {
            path_next = path_next.substr(0, path_next.size() - 1);
        }
        //iterations
        FsDirPtr dir = fs.loadDirectory(fs.getRoot());
        size_t next_sep = path_next.find_first_of(new_path.preferred_separator);
        while (next_sep != std::string::npos) {
            dir_name = path_next.substr(0, next_sep);
            path_next = path_next.substr(next_sep + 1);
            // get elt from dir
            bool found = false;
            for (const FsID& id_child : dir->getCurrent()) {
                FsDirPtr test_dir = fs.loadDirectory(id_child);
                if (test_dir && test_dir->getName() == dir_name) {
                    dir = test_dir;
                    found = true;
                }
            }
            if (!found) {
                assert(false);
                return 2;
            }
            //relance
            next_sep = path_next.find_first_of(new_path.preferred_separator);
        }
        // === file or directory?
        assert(path_next.size() >= 1);
        if (FsElt::isFile(obj->getId())) {
            std::vector<ChunkOrRawData> chunks;
            size_t size = obj->size();
            for (FsID chunk : obj->getCurrent()) {
                chunks.push_back(ChunkOrRawData{ chunk, size, nullptr, 0 });
                size = 0; // fake size for file size computation
            }
            m_db->m_file_storage->createNewFile(dir, path_next, chunks, obj->getCUGA(), FsElt::toFile(obj));
            m_db->m_file_storage->deleteObject(obj);
        } else {
            assert(FsElt::isDirectory(obj->getId()));
            m_db->m_file_storage->createNewDirectory(dir, path_next, obj->getCurrent(), obj->getCUGA(), FsElt::toDirectory(obj));
            m_db->m_file_storage->deleteObject(obj);
        }
        new_dir_parent_id = dir->getId();
        }//mutex
        m_db->notifyObjectChanged(obj->getId());
        m_db->notifyObjectChanged(obj->getParent());
        m_db->notifyObjectChanged(new_dir_parent_id);
        return 0;
    }

    int FsExternalInterface::setCUGA(FsObjectPtr obj, CUGA rights) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
        if (FsElt::isFile(obj->getId())) {
            std::vector<ChunkOrRawData> chunks;
            size_t size = obj->size();
            for (FsID chunk : obj->getCurrent()) {
                chunks.push_back(ChunkOrRawData{ chunk, size, nullptr, 0 });
                size = 0; // fake size for file size computation
            }
            m_db->m_file_storage->createNewFile(m_db->m_file_storage->loadDirectory(obj->getParent()), obj->getName(), chunks, rights, FsElt::toFile(obj));
            m_db->m_file_storage->deleteObject(obj);
        } else {
            assert(FsElt::isDirectory(obj->getId()));
            m_db->m_file_storage->createNewDirectory(m_db->m_file_storage->loadDirectory(obj->getParent()), obj->getName(), obj->getCurrent(), rights, FsElt::toDirectory(obj));
            m_db->m_file_storage->deleteObject(obj);
        }
        }//mutex
        m_db->notifyObjectChanged(obj->getId());
        m_db->notifyObjectChanged(obj->getParent());
        return 0;
    }

}

