#include "FsExternalInterface.hpp"

#include "SynchroDb.hpp"
#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"

#include <fstream>
#include <map>

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

    void FsExternalInterface::get(const std::filesystem::path& path, std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr, size_t max_retries) {
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
        if (!fs.hasLocally(fs.getRoot())) {
            m_db->askForFsSynch(fs.getRoot(), [path, promise_ptr, this](FsEltPtr result) { this->get(path, promise_ptr); });
            return;
        }
        FsDirPtr dir = fs.loadDirectory(fs.getRoot());
        size_t next_sep = path_next.find_first_of(path.preferred_separator);

        //check if in invalidated id
        if (m_db->isInvalidated(dir->getId()) && max_retries > 0) {
            m_db->askForFsSynch(dir->getId(), [path, promise_ptr, max_retries, this](FsEltPtr result) { 
                this->get(path, promise_ptr, max_retries-1); 
                });
            return;
        }if (m_db->isInvalidated(dir->getId())) {
            assert(false);
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
                //assert(false);
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
        if (m_db->hasInvalidation()) {
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
        if (m_db->hasInvalidation()) {
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

    void FsExternalInterface::getData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size, std::shared_ptr<std::promise<FsExternalInterface::RequestAnswer>> promise_ptr
        , size_t max_retries, std::map<FsID, FsChunkPtr> cache) {
        // don't refresh for chunks, use normal refresh for them.
        //if (!m_db->m_invalidated.empty()) {
        //    // maybe we can check for the root to avoid that, but better safe than sorry.
        //    m_db->askForFsSynch(dir->getId(), [dir, name, promise_ptr, this]() { this->contains(dir, name, promise_ptr); });
        //}
        size_t file_pos = 0;
        size_t buffer_pos = 0;
        if (file->size() < offset + size) {
            error("warning; reading more bytes than the file possess");
        }
        size_t first_chunk_idx = 0;
        {
            size_t temp_offset = 0;
            assert(file->getCurrent().size() == file->sizes().size());
            for (first_chunk_idx = 0; first_chunk_idx < file->getCurrent().size(); ++first_chunk_idx) {
                temp_offset += file->sizes()[first_chunk_idx];
                if (temp_offset > offset) {
                    break;
                }
            }
        }
        std::lock_guard lock{ m_db->fsSynchronize() };
        for (size_t chunk_idx = first_chunk_idx; chunk_idx < file->getCurrent().size(); ++chunk_idx) {
            const FsID id_chunk = file->getCurrent()[chunk_idx];
            FsChunkPtr chunk;
            if (auto it = cache.find(id_chunk); it != cache.end()) {
                chunk = it->second;
            } else {
                chunk = m_db->m_file_storage->loadChunk(id_chunk);
            }
            if (!chunk) {
                if (max_retries == 0) {
                    promise_ptr->set_value(RequestAnswer{ "Error when fetching the data: chunk not available", 5 });
                    return;
                }
                //ask to load all needed chunks
                const size_t first_unknown_chunk_idx = chunk_idx;
                std::vector<FsID> current = file->getCurrent();
                std::vector<FsID> chunks_to_get = { id_chunk };
                size_t next_file_pos = file_pos;
                for (size_t next_chunk_idx = chunk_idx + 1; next_chunk_idx < file->getCurrent().size(); ++next_chunk_idx) {
                    if (next_file_pos < offset + size) {
                        //we can ask for chunks that are already here, they will just be linked into the "cache"
                        chunks_to_get.push_back(file->getCurrent()[next_chunk_idx]);
                        next_file_pos += file->sizes()[next_chunk_idx];
                    } else {
                        break;
                    }
                }
                m_db->retreiveChunks(file, chunks_to_get, [this, current, promise_ptr, file, buffer, offset, size, max_retries, cache, first_unknown_chunk_idx, file_pos, buffer_pos]
                        (const std::vector<FsChunkPtr>& chunks) {
                    std::map<FsID, FsChunkPtr> cache_copy = cache;
                    for (FsChunkPtr temp_chunk : chunks) {
                        cache_copy[temp_chunk->getId()] = temp_chunk;
                    }
                    this->getData(file, buffer, offset, size, promise_ptr, max_retries - 1, cache_copy);
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
                };
            }
            file_pos += chunk->size();
            assert(file_pos > 0);
        }
        //shouldn't be possible, only happening when the file is shorter that the caller think
        promise_ptr->set_value(RequestAnswer{ "File is smaller", 5 });
        return;
    }

    void FsExternalInterface::writeData(FsFilePtr file, const uint8_t* buffer, const size_t offset, const size_t size, std::shared_ptr<std::promise<FsExternalInterface::RequestAnswer>> promise_ptr
        , size_t max_retries, std::map<FsID, FsChunkPtr> cache) {
        {std::lock_guard lock{ m_db->m_file_storage_mutex };
        //check if the file still exists
        if (file->getDeletedDate() > 0) {
            promise_ptr->set_value(RequestAnswer{ "File deleted, can't set data to it anymore", 2 });
            return;
        }
        //check that we have the chunks needed for it
        std::vector<size_t> new_chunk_sizes;
        std::vector<ChunkOrRawData> new_content;
        FsID first_part_id = 0;
        FsChunkPtr first_part;
        size_t first_part_file_pos = 0;
        FsID last_part_id = 0;
        FsChunkPtr last_part;
        size_t last_part_file_pos = 0;
        size_t first_chunkidx_after_write = 0;
        const std::vector<FsID>& old_chunks_id = file->getCurrent();
        const std::vector<size_t>& old_chunks_size = file->sizes();
        assert(old_chunks_id.size() == old_chunks_size.size());
        const size_t old_file_size = file->size();
        const size_t new_file_size = std::max(offset + size, old_file_size);
        {
            size_t current_file_pos;
            for (size_t chunk_idx = 0; chunk_idx < old_chunks_id.size(); ++chunk_idx) {
                FsID chunk_id = old_chunks_id[chunk_idx];
                size_t chunk_size = old_chunks_size[chunk_idx];
                if (current_file_pos + chunk_size <= offset) {
                    // can reuse (before the change)
                    new_chunk_sizes.push_back(chunk_size);
                    new_content.push_back(ChunkOrRawData{ chunk_id, chunk_size, nullptr, 0 });
                } else if (current_file_pos < offset) {
                    // has to be split (write start inside it ; and maybe also end inside)
                    first_part_file_pos = current_file_pos;
                    first_part_id = chunk_id;
                } else if (current_file_pos < offset + size && current_file_pos + chunk_size > offset + size) {
                    // has to be split (write end inside it, and does not start inside)
                    last_part_file_pos = current_file_pos;
                    last_part_id = chunk_id;
                    first_chunkidx_after_write = chunk_idx + 1;
                } else if (current_file_pos + chunk_size == offset + size) {
                    //just at the right position: no extra data to reuse (very lucky)
                    first_chunkidx_after_write = chunk_idx + 1;
                }
                current_file_pos += chunk_size;
                if (current_file_pos >= offset + size) {
                    if (first_chunkidx_after_write == 0) {
                        assert(false); //shouldn't be possible
                        if (current_file_pos == offset + size) {
                            first_chunkidx_after_write = chunk_idx + 1;
                        } else {
                            first_chunkidx_after_write = chunk_idx;
                        }
                    }
                    break;
                }
            }
        }
        //get chunk data that is needed
        if (first_part_id != 0) {
            if (auto it = cache.find(first_part_id); it != cache.end()) {
                first_part = it->second;
            } else {
                first_part = m_db->m_file_storage->loadChunk(first_part_id);
            }
        }
        if (last_part_id != 0) {
            if (auto it = cache.find(last_part_id); it != cache.end()) {
                last_part = it->second;
            } else {
                last_part = m_db->m_file_storage->loadChunk(last_part_id);
            }
        }
        if ((first_part_id != 0 && !first_part) || (last_part_id != 0 && !last_part)) {
            if (max_retries == 0) {
                promise_ptr->set_value(RequestAnswer{ "Error when fetching the data: chunk not available", 5 });
                return;
            }
            std::vector<FsID> chunks_to_get;
            if (first_part_id != 0 && !first_part) chunks_to_get.push_back(first_part_id);
            if (last_part_id != 0 && !last_part) chunks_to_get.push_back(last_part_id);
            m_db->retreiveChunks(file, chunks_to_get, [this, promise_ptr, file, buffer, offset, size, max_retries, cache](const std::vector<FsChunkPtr>& chunks) {
                std::map<FsID, FsChunkPtr> cache_copy = cache;
                for (FsChunkPtr temp_chunk : chunks) {
                    cache_copy[temp_chunk->getId()] = temp_chunk;
                }
                this->writeData(file, buffer, offset, size, promise_ptr, max_retries - 1, cache_copy);
            });
            return;
        }


        // change chunks
        size_t buffer_pos = 0;
        //size_t file_pos = first_part_file_pos;
        std::vector<uint8_t> temp_buffer_first; // for first 'new' chunk if it's a mix
        std::vector<uint8_t> temp_buffer_last; // for first 'new' chunk if it's a mix
        if (first_part) {
            //choose chunk size
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, new_chunk_sizes, first_part_file_pos, new_file_size, new_chunk_sizes.size());
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
            new_chunk_sizes.push_back(temp_buffer_first.size());
            new_content.push_back(ChunkOrRawData{ 0, 0, &temp_buffer_first[0], temp_buffer_first.size() });
        }
        //new chunks
        while (buffer_pos < size) {
            assert(buffer_pos < size);
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, new_chunk_sizes, first_part_file_pos, new_file_size, new_chunk_sizes.size());
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
        for (size_t idx = first_chunkidx_after_write; idx < old_chunks_id.size(); ++idx) {
            new_content.push_back(ChunkOrRawData{ old_chunks_id[idx], old_chunks_size[idx], nullptr, 0 });
            size_remaining_after_write = 0;
        }

        //create the commit
        m_db->m_file_storage->modifyFile(file, new_content);
        assert(file->size() == new_file_size);

        }//mutex
        //notify the synch (& maybe peers)
        m_db->notifyObjectChanged(file->getId());
    }

    void FsExternalInterface::resize(FsFilePtr file, size_t new_size, std::shared_ptr<std::promise<FsExternalInterface::ObjectRequestAnswer>> promise_ptr
        , size_t max_retries, std::map<FsID, FsChunkPtr> cache) {
        {std::lock_guard lock{ m_db->fsSynchronize() };
        //check if the file still exists
        if (file->getDeletedDate() > 0) {
            promise_ptr->set_value(ObjectRequestAnswer{ "File deleted, can't set data to it anymore", 2 });
            return;
        }
        //check that we have the chunks needed for it
        const size_t old_file_size = file->size();
        std::vector<ChunkOrRawData> new_content;
        FsChunkPtr chunk_to_split;
        size_t chunk_to_split_idx = 0;
        size_t chunk_to_split_start_pos = 0;
        const std::vector<FsID>& old_chunks_id = file->getCurrent();
        const std::vector<size_t>& old_sizes = file->sizes();
        std::vector<size_t> new_sizes;
        //keep okay first chunks
        size_t file_pos = 0;
        {
            for (size_t chunk_idx = 0; chunk_idx < old_chunks_id.size(); chunk_idx++) {
                if (file_pos + old_sizes[chunk_idx] <= new_size) {
                    new_content.push_back(ChunkOrRawData{ old_chunks_id[chunk_idx], old_sizes[chunk_idx], nullptr, 0 });
                    file_pos += old_sizes[chunk_idx];
                    new_sizes.push_back(old_sizes[chunk_idx]);
                } else if (file_pos == new_size) {
                    break;
                } else if (file_pos < new_size) {
                    chunk_to_split_start_pos = file_pos;
                    chunk_to_split_idx = chunk_idx;
                    if (auto it = cache.find(old_chunks_id[chunk_idx]); it != cache.end()) {
                        chunk_to_split = it->second;
                    } else {
                        chunk_to_split = m_db->m_file_storage->loadChunk(old_chunks_id[chunk_idx]);
                        if (!chunk_to_split) {
                            if (max_retries == 0) {
                                promise_ptr->set_value(ObjectRequestAnswer{ "Error when fetching the data: impossible to retreive a chunk to split", 5 });
                            }
                            //ask to load all needed chunks (for their size)
                            m_db->retreiveChunks(file, { old_chunks_id[chunk_idx] }, [this, promise_ptr, file, new_size, max_retries, cache](const std::vector<FsChunkPtr>& result) {
                                std::map<FsID, FsChunkPtr> cache_copy = cache;
                                for (FsChunkPtr temp_chunk : result) {
                                    cache_copy[temp_chunk->getId()] = temp_chunk;
                                }
                                this->resize(file, new_size, promise_ptr, max_retries-1, cache_copy);
                            });
                            return;
                        }
                    }
                    break;
                } else {
                    assert(false);
                }
            }
        }
        // size reduced: we have to remove some chunks
        std::vector<uint8_t> chunk_to_split_data; // for first 'new' chunk if it's a mix
        if (chunk_to_split) {
            assert(new_size < old_file_size);
            //choose chunk size of the reduced last chunk
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, old_sizes, old_file_size, new_size, chunk_to_split_idx);
            assert(chunk_to_split_start_pos + current_chunk_size == new_size);
            //have to get the first part of the buffer (have to copy the buffer instead of just linking to it because we don't have access to it (maybe still on the hard drive)
            //TODO: allow to reuse the stored array if available (need something to keep immutable data array over different part of the software)
            chunk_to_split_data.resize(current_chunk_size);
            //add first part
            assert(chunk_to_split->size() > current_chunk_size);
            chunk_to_split->read(&chunk_to_split_data[0], 0, current_chunk_size);
            //create chunk
            new_sizes.push_back(current_chunk_size);
            new_content.push_back(ChunkOrRawData{ 0, 0, &chunk_to_split_data[0], current_chunk_size });
            file_pos += current_chunk_size;
            assert(file_pos == new_size);
        }
        //new chunks
        std::vector<std::vector<uint8_t>> buffers; // for others chunks
        size_t size_remaining_after_chunks = new_size - old_file_size;
        while (file_pos < new_size) {
            assert(!chunk_to_split);
            size_t current_chunk_size = m_db->m_chunk_sizer->chooseChunkSize(file, new_sizes, file_pos, new_size, new_sizes.size());
            if (current_chunk_size + file_pos > new_size) {
                assert(false);
                current_chunk_size = new_size - file_pos;
            }
            std::vector<uint8_t>& curr_buff = buffers.emplace_back(current_chunk_size, uint8_t(0));
            new_content.push_back(ChunkOrRawData{ 0, 0 , &curr_buff[0], current_chunk_size });
            file_pos += current_chunk_size;
        }
        assert(file_pos == new_size);

        //create the commit
        m_db->m_file_storage->modifyFile(file, new_content);
        // now, all temporary storage can be destroyed.
        assert(file->size() == new_size);

        }//fs mutex
        //notify the synch (& maybe peers)
        m_db->notifyObjectChanged(file->getId());


        promise_ptr->set_value(ObjectRequestAnswer{ true, file });
        return;
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

