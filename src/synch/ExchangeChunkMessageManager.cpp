#include "ExchangeChunkMessageManager.hpp"

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsChunk.hpp"
#include "fs/base/FsObject.hpp"
#include "fs/base/FsFile.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"
#include "network/Peer.hpp"
#include "network/IdentityManager.hpp"

namespace supercloud {



    void ExchangeChunkMessageManager::register_listener() {
        m_clusterManager->registerListener(*SynchMessagetype::GET_SINGLE_CHUNK, this->ptr());
        m_clusterManager->registerListener(*SynchMessagetype::SEND_SINGLE_CHUNK, this->ptr());
        m_clusterManager->registerListener(*SynchMessagetype::GET_CHUNK_AVAILABILITY, this->ptr());
        m_clusterManager->registerListener(*SynchMessagetype::SEND_CHUNK_AVAILABILITY, this->ptr());
        m_clusterManager->registerListener(*SynchMessagetype::GET_CHUNK_REACHABLE, this->ptr());
        m_clusterManager->registerListener(*SynchMessagetype::SEND_CHUNK_REACHABLE, this->ptr());
    }


    void ExchangeChunkMessageManager::receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) {
        if (sender->getComputerId() == 0 || sender->getComputerId() == NO_COMPUTER_ID) {
            //error: not a estabished peer
            error(std::string("Error, peer ") + std::to_string(sender->getPeerId() % 100) + " ask us a chunk and he doens't have a computerid !");
            return;
        }
        if (messageId == *SynchMessagetype::GET_CHUNK_AVAILABILITY) {
            log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE GET_CHUNK_AVAILABILITY from " + sender->getPeerId());
            ChunkAvailabilityRequest request = readChunkAvailabilityRequest(message);
            ChunkAvailabilityAnswer answer = createChunkAvailabilityAnswer(request);
            sender->writeMessage(*SynchMessagetype::SEND_CHUNK_AVAILABILITY, writeChunkAvailabilityAnswer(answer));
        } else if (messageId == *SynchMessagetype::SEND_CHUNK_AVAILABILITY) {
            log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE SEND_CHUNK_AVAILABILITY from " + sender->getPeerId());
            ChunkAvailabilityAnswer result = readChunkAvailabilityAnswer(message);
            //update our availability
            this->useChunkAvailabilityAnswer(sender, result);
        } else if (messageId == *SynchMessagetype::GET_CHUNK_REACHABLE) {
            log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE GET_CHUNK_REACHABLE from " + sender->getPeerId());
            ChunkReachableRequest request = readChunkReachableRequest(message);
            this->answerChunkReachableRequest(sender, request);
        } else if (messageId == *SynchMessagetype::SEND_CHUNK_REACHABLE) {
            log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE SEND_CHUNK_REACHABLE from " + sender->getPeerId());
            ChunkReachableAnswer result = readChunkReachableAnswer(message);
            //update our path & call callbacks
            this->useChunkReachableAnswer(sender, result);
        } else if (messageId == *SynchMessagetype::GET_SINGLE_CHUNK) {
            log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE GET_CHUNK from " + sender->getPeerId());
            SingleChunkRequest request = readSingleChunkRequest(message);
            this->answerSingleChunkRequest(sender, request);
        } else if (messageId == *SynchMessagetype::SEND_SINGLE_CHUNK) {
            log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE SEND_CHUNK from " + sender->getPeerId());
            SingleChunkAnswer result = readSingleChunkAnswer(message);
            //update our fs & call callbacks
            this->useSingleChunkAnswer(sender, result);
        }

    }


    //simple fetch: a chunk
    //TODO: fetch a random part of a file.
    std::future<std::shared_ptr<ExchangeChunkMessageManager::FsChunkTempElt>> ExchangeChunkMessageManager::fetchChunk(FsID chunk_id) {
        std::shared_ptr<std::promise<FsChunkTempPtr>> notify_chunk_fetched{ new std::promise<FsChunkTempPtr> {} };
        std::future<FsChunkTempPtr> future = notify_chunk_fetched->get_future();

        //do i have the chunk?
        if (m_filesystem->hasLocally(chunk_id)) {
            //i have evrything -> give result
            FsChunkPtr my_chunk = m_filesystem->loadChunk(chunk_id);
            notify_chunk_fetched->set_value(std::shared_ptr<FsChunkTempElt>{ new FsChunkTempElt{ my_chunk } });
        } else {
            answerSingleChunkRequest(this->m_clusterManager->getIdentityManager().getSelfPeer(), SingleChunkRequest{ chunk_id , true },
                [this, notify_chunk_fetched](FsChunkTempPtr answer) {
                    notify_chunk_fetched->set_value(answer);
                });
        }
        return future;
    }

    void ExchangeChunkMessageManager::fetchChunk(FsID chunk_id, const std::function<void(FsChunkTempPtr)>& callback) {
        //do i have the chunk?
        if (m_filesystem->hasLocally(chunk_id)) {
            //i have evrything -> give result
            FsChunkPtr my_chunk = m_filesystem->loadChunk(chunk_id);
            callback(FsChunkTempPtr{ new FsChunkTempElt{my_chunk} });
        } else {
            answerSingleChunkRequest(this->m_clusterManager->getIdentityManager().getSelfPeer(), SingleChunkRequest{ chunk_id , true }, callback);
        }
    }

    ExchangeChunkMessageManager::ChunkAvailabilityRequest ExchangeChunkMessageManager::createChunkAvailabilityRequest(FsID root) const {
        return ChunkAvailabilityRequest{ root, false };
    }
    ByteBuff ExchangeChunkMessageManager::writeChunkAvailabilityRequest(const ChunkAvailabilityRequest& request) const {
        ByteBuff buff;
        buff.putULong(request.elt_root);
        buff.put(request.only_you ? 1 : 0);
        return buff.flip();
    }
    ExchangeChunkMessageManager::ChunkAvailabilityRequest ExchangeChunkMessageManager::readChunkAvailabilityRequest(ByteBuff& buffer) const {
        return ExchangeChunkMessageManager::ChunkAvailabilityRequest{ buffer.getULong(), buffer.get() == 1 };
    }

    // send my synch db
    void ExchangeChunkMessageManager::fillChunkAvailabilityAnswer(ChunkAvailabilityAnswer& answer, FsID id, bool only_us) const {
        ComputerId my_cid = m_clusterManager->getComputerId();
        if (FsElt::isObject(id)) {
            FsEltPtr elt = m_filesystem->load(id);
            if (elt) {
                for (FsID item : FsElt::toObject(elt)->getCurrent()) {
                    fillChunkAvailabilityAnswer(answer, item, only_us);
                }
            }
        } else if (FsElt::isChunk(id)) {
            bool has_chunk = false;
            const Availability& our_ava_info = m_syncro->getAvailability(id);
            for (auto& compid2time : our_ava_info.available) {
                if (compid2time.first == my_cid) {
                    has_chunk = true;
                    Date currDate = toDate(m_clusterManager->getCurrentTime());
                    std::vector<ChunkAvailabilityComputer>& vec = answer.availability[id];
                    vec.push_back(ChunkAvailabilityComputer{ my_cid, true, compid2time.second.date_state, currDate });
                } else if (!only_us) {
                    // we give first-hand cid and second-hand cid (but not those we have seen as not available)
                    std::vector<ChunkAvailabilityComputer>& vec = answer.availability[id];
                    vec.push_back(ChunkAvailabilityComputer{ compid2time.first,
                        (compid2time.second.state & ComputerAvailabilityState::HAS_CHUNK) ? true : false,
                        compid2time.second.date_state,
                        compid2time.second.date_information });
                }
            }
            if (!has_chunk) {
                //check again, to be sure
                has_chunk = m_filesystem->hasLocally(id);
                //assert(!has_chunk);
                if (has_chunk) {
                    std::vector<ChunkAvailabilityComputer>& vec = answer.availability[id];
                    Date currDate = toDate(m_clusterManager->getCurrentTime());
                    //TODO: store the 'date added' somewhere (currently, should be  stored inside m_syncro->getAvailability?
                    vec.push_back(ChunkAvailabilityComputer{ my_cid, true, currDate, currDate });
                    m_syncro->setAvailability(id, my_cid,
                        ComputerAvailability{ ComputerAvailabilityState(ComputerAvailabilityState::HAS_CHUNK | ComputerAvailabilityState::FETCH_FROM_SOURCE), currDate, currDate });
                }
            }
        }
        //else: nothing.. if it's a commit, it's too hard to find.
    }
    ExchangeChunkMessageManager::ChunkAvailabilityAnswer ExchangeChunkMessageManager::createChunkAvailabilityAnswer(const ChunkAvailabilityRequest& request) const {
        ChunkAvailabilityAnswer answer;
        fillChunkAvailabilityAnswer(answer, request.elt_root, request.only_you);
        return answer;
    }

    ByteBuff ExchangeChunkMessageManager::writeChunkAvailabilityAnswer(const ChunkAvailabilityAnswer& request) const {
        ByteBuff buff;
        buff.putSize(request.availability.size());
        for (const auto& id2computers : request.availability) {
            buff.putULong(id2computers.first);
            buff.putSize(id2computers.second.size());
            for (const ChunkAvailabilityComputer& comp_avail : id2computers.second) {
                buff.serializeComputerId(comp_avail.cid);
                buff.put(comp_avail.has_chunk ? 1 : 0);
                buff.putUInt(comp_avail.since);
                buff.putUInt(comp_avail.last_fetched);
            }
        }
        return buff.flip();
    }
    ExchangeChunkMessageManager::ChunkAvailabilityAnswer ExchangeChunkMessageManager::readChunkAvailabilityAnswer(ByteBuff& buffer) const {
        ChunkAvailabilityAnswer answer;
        size_t nb_chunks = buffer.getSize();
        for (size_t idx_chunk = 0; idx_chunk < nb_chunks; ++idx_chunk) {
            std::vector<ChunkAvailabilityComputer>& vec_cid = answer.availability[buffer.getULong()];
            size_t nb_cid = buffer.getSize();
            for (size_t idx_cid = 0; idx_cid < nb_cid; ++idx_cid) {
                ComputerId cid = buffer.deserializeComputerId();
                bool has_chunk = buffer.get() == 1;
                Date since = buffer.getUInt();
                Date last_fetch = buffer.getUInt();
                vec_cid.push_back(ChunkAvailabilityComputer{ cid , has_chunk, since, last_fetch });
            }
        }
        return answer;
    }
    void ExchangeChunkMessageManager::useChunkAvailabilityAnswer(PeerPtr sender, const ChunkAvailabilityAnswer& request) {
        for (const auto& fsid_2_availability : request.availability) {
            for (const auto& cid_2_avail : fsid_2_availability.second) {
                //only consider fresh information
                if (cid_2_avail.last_fetched < SynchroDb::MAX_RETENTION_DELETION_INFO) {
                    // get our version (if any)
                    std::optional<ComputerAvailability> our_cid_avail = m_syncro->getAvailability(fsid_2_availability.first, cid_2_avail.cid);
                    //replace our info if: no previous info
                    bool need_replace = !our_cid_avail.has_value();
                    //or first-hand info
                    need_replace = need_replace || cid_2_avail.cid == sender->getComputerId();
                    //or more recent
                    need_replace = need_replace || (our_cid_avail.value().date_information < cid_2_avail.last_fetched && our_cid_avail.value().date_state < cid_2_avail.since);
                    if (need_replace) {
                        ComputerAvailabilityState state = cid_2_avail.has_chunk ? ComputerAvailabilityState::HAS_CHUNK : ComputerAvailabilityState::EMPTY;
                        if (cid_2_avail.cid == sender->getComputerId()) {
                            state = ComputerAvailabilityState(state | ComputerAvailabilityState::FETCH_FROM_SOURCE);
                        }
                        m_syncro->setAvailability(fsid_2_availability.first, cid_2_avail.cid, ComputerAvailability{ state, cid_2_avail.since, cid_2_avail.last_fetched });
                    }
                }
            }
        }
    }

    //TODO: this is too low level, please add some intelligence.
    // 1- answer to minute-refresh: send a refresh about 1 something that hasn't been fetch since some days.
    // 2- dunno. quick-chunk access should be taken care of by GET_CHUNK_REACHABLE
    void ExchangeChunkMessageManager::requestFetchInformationFor(PeerPtr peer, FsID root) {
        ChunkAvailabilityRequest request = createChunkAvailabilityRequest(root);
        peer->writeMessage(*SynchMessagetype::GET_CHUNK_AVAILABILITY, writeChunkAvailabilityRequest(request));
    }


    //if you're calling that, don't forget to add an entry in the request table
    ExchangeChunkMessageManager::ChunkReachableRequest ExchangeChunkMessageManager::createChunkReachableRequest(FsID id) const {
        return ChunkReachableRequest{ id };
    }
    ByteBuff ExchangeChunkMessageManager::writeChunkReachableRequest(const ChunkReachableRequest& request) const {
        assert(m_reachable_requests.find(request.chunk_or_file_id) != m_reachable_requests.end());
        ByteBuff buff;
        buff.putULong(request.chunk_or_file_id);
        return buff.flip();
    }
    ExchangeChunkMessageManager::ChunkReachableRequest ExchangeChunkMessageManager::readChunkReachableRequest(ByteBuff& buffer) const {
        return ChunkReachableRequest{ buffer.getULong() };
    }
    ByteBuff ExchangeChunkMessageManager::writeChunkReachableAnswer(const ChunkReachableAnswer& answer) const {
        ByteBuff buff;
        buff.putULong(answer.chunk_or_file_id_request);
        buff.putSize(answer.paths.size());
        for (const auto& fsid_2_compid : answer.paths) {
            buff.putULong(fsid_2_compid.first);
            buff.serializeComputerId(fsid_2_compid.second.cid);
            buff.put(fsid_2_compid.second.hops);
            buff.put(fsid_2_compid.second.difficulty);
        }
        return buff.flip();
    }
    ExchangeChunkMessageManager::ChunkReachableAnswer ExchangeChunkMessageManager::readChunkReachableAnswer(ByteBuff& buffer) const {
        ChunkReachableAnswer answer;
        answer.chunk_or_file_id_request = buffer.getULong();
        const size_t nb_items = buffer.getSize();
        for (size_t i = 0; i < nb_items; ++i) {
            ChunkReachablePath& path = answer.paths[buffer.getULong()];
            path.cid = buffer.deserializeComputerId();
            path.hops = buffer.get();
            path.difficulty = buffer.get();
        }
        assert(buffer.available() == 0);
        return answer;
    }
    void ExchangeChunkMessageManager::answerChunkReachableRequest(PeerPtr sender, const ChunkReachableRequest& request) {
        std::lock_guard lock{ m_reachable_requests_mutex };
        //check if i have it in my waiting request list
        if (auto reachable_it = m_reachable_requests.find(request.chunk_or_file_id); reachable_it != m_reachable_requests.end()) {
            //is it finished?
            if (std::abs(reachable_it->second.start - m_clusterManager->getCurrentTime()) > 120000) {
                //if more htan 2 minutes, erase and retry
                m_reachable_requests.erase(reachable_it);
            } else {
                if (reachable_it->second.finished) {
                    //finished, emit our answer
                    sender->writeMessage(*SynchMessagetype::SEND_CHUNK_REACHABLE, writeChunkReachableAnswer(reachable_it->second.our_answer));
                } else {
                    // already in progress, don't re-emit it.
                    //add the peer in the list, so we can answer him.
                    reachable_it->second.requester.push_back(sender);
                }
                // don't rebuild a new anwser, stop here.
                return;
            }
        }
        // look at waht we have, and store result here
        std::vector<FsID> local;
        std::vector<FsID> notfound;
        if (FsElt::isFile(request.chunk_or_file_id)) {
            //do i have all of them?
            FsFilePtr file = m_filesystem->loadFile(request.chunk_or_file_id);
            for (const FsID& id : file->getCurrent()) {
                if (m_filesystem->hasLocally(id)) {
                    local.push_back(id);
                } else {
                    notfound.push_back(id);
                }
            }
        } else if (FsElt::isChunk(request.chunk_or_file_id)) {
            //do i have it?
            if (m_filesystem->hasLocally(request.chunk_or_file_id)) {
                local.push_back(request.chunk_or_file_id);
            } else {
                notfound.push_back(request.chunk_or_file_id);
            }
        }
        if (notfound.empty()) {
            //i have evrything -> send result directly
            ChunkReachableAnswer answer;
            answer.chunk_or_file_id_request = request.chunk_or_file_id;
            for (const FsID& id : local) {
                ChunkReachablePath& path = answer.paths[id];
                path.cid = m_clusterManager->getComputerId();
                path.hops = 0;
                path.difficulty = 50; //TODO
            }
            sender->writeMessage(*SynchMessagetype::SEND_CHUNK_REACHABLE, writeChunkReachableAnswer(answer));
        } else {
            //get all connected peers
            PeerList peers = m_clusterManager->getPeersCopy();
            //remove our sender
            foreach(it, peers) { if (it->get() == sender.get()) it.erase(); }
            //is there enough peers?
            if (peers.empty()) {
                ChunkReachableAnswer answer;
                answer.chunk_or_file_id_request = request.chunk_or_file_id;
                //no chunks -> can't find
                // still, if it's a file, add the chunks we have
                for (const FsID& id : local) {
                    if (m_filesystem->hasLocally(id)) {
                        ChunkReachablePath& path = answer.paths[id];
                        path.cid = m_clusterManager->getComputerId();
                        path.hops = 0;
                        path.difficulty = 50; //TODO
                    }
                }
                sender->writeMessage(*SynchMessagetype::SEND_CHUNK_REACHABLE, writeChunkReachableAnswer(answer));
            } else {
                //write the request
                WaitingReachableRequest& saved_flood_request = m_reachable_requests[request.chunk_or_file_id];
                saved_flood_request.id = request.chunk_or_file_id;
                saved_flood_request.start = m_clusterManager->getCurrentTime();
                saved_flood_request.requester.push_back(sender);
                //ask for the others
                for (PeerPtr& peer : peers) {
                    sender->writeMessage(*SynchMessagetype::GET_CHUNK_REACHABLE, writeChunkReachableRequest(request));
                    saved_flood_request.sent_to.push_back(peer);
                }
            }

        }
    }

    void ExchangeChunkMessageManager::useChunkReachableAnswer(PeerPtr sender, const ChunkReachableAnswer& answer) {
        std::lock_guard lock{ m_reachable_requests_mutex };
        //get the request (if not, ignore)
        if (auto our_answer_it = m_reachable_requests.find(answer.chunk_or_file_id_request); our_answer_it != m_reachable_requests.end()) {
            //add the answer to our answer
            ChunkReachableAnswer& our_answer = our_answer_it->second.our_answer;
            ChunkReachablePath* path;
            for (const auto& fsid_2_path : answer.paths) {
                auto our_path_it = our_answer.paths.find(fsid_2_path.first);
                if (our_path_it == our_answer.paths.end()) {
                    path = &our_answer.paths[fsid_2_path.first];
                    *path = fsid_2_path.second;
                } else if (our_path_it->second.hops > fsid_2_path.second.hops ||
                    (our_path_it->second.hops == fsid_2_path.second.hops && our_path_it->second.difficulty > fsid_2_path.second.difficulty)) {
                    path = &our_path_it->second;
                    *path = fsid_2_path.second;
                }
                //add us as gateway
                path->hops++;
                path->difficulty += 10; //TODO
            }
            //remove the sender peer from the waiting list
            foreach(itpeer, our_answer_it->second.sent_to) if (*itpeer == sender) itpeer.erase();
            //if there is no more waiting peers, emit the anwser to the requesters.
            if (our_answer_it->second.sent_to.empty()) {
                //update the best path of our availability repo
                for (const auto& fsid_2_path : our_answer.paths) {
                    // if not local availability
                    if (fsid_2_path.second.hops > 0) {
                        const Availability& our_avail = m_syncro->getAvailability(fsid_2_path.first);
                        if (our_avail.current_best_path != fsid_2_path.second.cid) {
                            Availability modified_avail = our_avail;
                            modified_avail.current_best_path = fsid_2_path.second.cid;
                            m_syncro->setAvailabilityMetadata(fsid_2_path.first, modified_avail);
                        }
                    }
                }
                our_answer_it->second.finished = true;
                //emit the anwser to the requesters.
                for (PeerPtr& requester : our_answer_it->second.requester) {
                    requester->writeMessage(*SynchMessagetype::SEND_CHUNK_REACHABLE, writeChunkReachableAnswer(our_answer_it->second.our_answer));
                }
                //call the callbacks
                foreach(callback_it, our_answer_it->second.callbacks) {
                    callback_it.get()(our_answer_it->second.our_answer);
                    callback_it.erase();
                }
                // let the request lie in memory for some time
            }
        } else {
            assert(false); // how?
        }
    }


    //if you're calling that, don't forget to add an entry in the request table
    ExchangeChunkMessageManager::SingleChunkRequest ExchangeChunkMessageManager::createSingleChunkRequest(FsID chunk_or_file_to_get, bool gateway) {
        assert(FsElt::isFile(chunk_or_file_to_get) || FsElt::isChunk(chunk_or_file_to_get));
        return SingleChunkRequest{ chunk_or_file_to_get , gateway };
    }
    ByteBuff ExchangeChunkMessageManager::writeSingleChunkRequest(const SingleChunkRequest& request) const {
        assert(m_waiting_requests.find(request.chunk_id) != m_waiting_requests.end());
        ByteBuff buff;
        buff.putULong(request.chunk_id);
        buff.put(request.need_gateway ? 1 : 0);
        return buff.flip();
    }
    ExchangeChunkMessageManager::SingleChunkRequest ExchangeChunkMessageManager::readSingleChunkRequest(ByteBuff& buffer) const {
        return SingleChunkRequest{ buffer.getULong(), buffer.get() == 1 };
    }
    ExchangeChunkMessageManager::SingleChunkAnswer ExchangeChunkMessageManager::createSingleChunkAnswer(const SingleChunkRequest& request, FsChunkTempPtr stub) const {
        return SingleChunkAnswer{ request.chunk_id , stub, 0 };
    }
    ByteBuff ExchangeChunkMessageManager::writeSingleChunkAnswer(const SingleChunkAnswer& answer) const {
        ByteBuff buff;
        buff.putULong(answer.chunk_id_requested);
        buff.put(answer.hash_algo);
        buff.put(answer.chunk?1:0);
        if (answer.chunk) {
            buff.putULong(answer.chunk->getId());
            buff.putLong(answer.chunk->getDate());
            buff.putULong(answer.chunk->getHash());
            buff.putSize(answer.chunk->size());
            answer.chunk->read(buff, 0, answer.chunk->size());
        }
        return buff.flip();
    }
    ExchangeChunkMessageManager::SingleChunkAnswer ExchangeChunkMessageManager::readSingleChunkAnswer(ByteBuff& buffer) const {
        SingleChunkAnswer answer;
        answer.chunk_id_requested = buffer.getULong();
        answer.hash_algo = buffer.get();
        bool has_chunk = buffer.get() == 1;
        if(has_chunk){
            FsID id = buffer.getULong();
            DateTime time = buffer.getLong();
            uint64_t hash = buffer.getULong();
            size_t buff_size = buffer.getSize();
            ByteBuff chunk_data;
            buffer.get(chunk_data, buff_size);
            answer.chunk = FsChunkTempPtr{ new FsChunkTempElt{ id, time, hash,
                std::move(chunk_data), 0, hash, 0, buff_size } };
        }
        assert(buffer.available() == 0);
        return answer;
    }

    void ExchangeChunkMessageManager::answerSingleChunkRequest(
        PeerPtr& sender,
        const SingleChunkRequest& request,
        const std::function<void(FsChunkTempPtr)>& callback)
    {
        std::lock_guard lock{ m_waiting_requests_mutex };
        assert(FsElt::isChunk(request.chunk_id));
        //do i have the chunk?
        if (m_filesystem->hasLocally(request.chunk_id)) {
            //i have evrything -> give result
            FsChunkPtr my_chunk = m_filesystem->loadChunk(request.chunk_id);
            //ByteBuff data;
            //my_chunk->read(data, 0, my_chunk->size());
            //FsChunkTempElt stub{ my_chunk->getId(), my_chunk->getDate(), my_chunk->getHash(),
            //    std::move(data), 0, my_chunk->getHash(), 0, my_chunk->size() };
            callback(FsChunkTempPtr(new FsChunkTempElt{my_chunk}));
            return;
        } else {
            //get all connected peers
            PeerList peers = m_clusterManager->getPeersCopy();
            //remove our sender
            foreach(it, peers) { if (it->get() == sender.get() || !(*it)->isConnected()) it.erase(); }
            //the callback to our caller callback (to avoid duplication, as it's used two time below)
            auto call_chunkcallback = [callback](SingleChunkAnswer& answer) {
                callback(answer.chunk);
            };
            //is there enough peers?
            if (peers.empty()) {
                callback(FsChunkTempPtr{});
                return;
            } else {
                //create the request
                if (auto it_get_chunk = m_waiting_requests.find(request.chunk_id); it_get_chunk != m_waiting_requests.end()) {
                    //request already exist, use it
                    if (it_get_chunk->second.finished) {
                        callback(it_get_chunk->second.our_answer.chunk);
                        return;
                    } else {
                        //note: callback by copy, to be sure it still exists.
                        it_get_chunk->second.callbacks.push_back(call_chunkcallback);
                        return;
                    }
                } else {
                    //request not here, create it.
                    WaitingSingleChunkRequest& saved_chunk_request = m_waiting_requests[request.chunk_id];
                    saved_chunk_request.id = request.chunk_id;
                    saved_chunk_request.start = m_clusterManager->getCurrentTime();
                    saved_chunk_request.callbacks.push_back(call_chunkcallback);
                    bool request_sent = false;
                    // get the best peer
                    Availability available = m_syncro->getAvailability(request.chunk_id);
                    if (available.current_best_path != 0 && available.current_best_path != NO_COMPUTER_ID) {
                        //get peer
                        for (PeerPtr peer : peers) {
                            if (peer->isConnected() && peer->getComputerId() == available.current_best_path) {
                                peer->writeMessage(*SynchMessagetype::GET_SINGLE_CHUNK, writeSingleChunkRequest(createSingleChunkRequest(request.chunk_id, true)));
                                saved_chunk_request.sent_to.push_back(peer);
                                request_sent = true;
                                break;
                            }
                        }
                    }
                    if (!request_sent) {
                        //i don't have a current_best_path for this id, we have to sent a ChunkReachableRequest to get one.
                        // to avoid duplication, define the reachable callback here.
                        auto call_chunkcallback = [this, request](ChunkReachableAnswer reach) {
                            std::lock_guard lock{ m_waiting_requests_mutex };
                            WaitingSingleChunkRequest& saved_chunk_request = m_waiting_requests[request.chunk_id];
                            bool request_sent = false;
                            //if one path for our one chunk, then follow it
                            if (auto it = reach.paths.find(reach.chunk_or_file_id_request); it != reach.paths.end()) {
                                //get peer
                                for (PeerPtr peer : m_clusterManager->getPeersCopy()) {
                                    if (peer->isConnected() && peer->getComputerId() == it->second.cid) {
                                        peer->writeMessage(*SynchMessagetype::GET_SINGLE_CHUNK, writeSingleChunkRequest(createSingleChunkRequest(request.chunk_id, true)));
                                        saved_chunk_request.sent_to.push_back(peer);
                                        request_sent = true;
                                        break;
                                    }
                                }
                            }
                            if (!request_sent) {
                                //can't find a best path, abord
                                saved_chunk_request.finished = true;
                                //call the callbacks
                                foreach(callback_it, saved_chunk_request.callbacks) {
                                    callback_it.get()(saved_chunk_request.our_answer);
                                    callback_it.erase();
                                }
                            }
                        };
                        //broadcast a reachable request.
                        //note: double mutex!!! be careful!!!
                        std::lock_guard loack2{ m_reachable_requests_mutex };
                        if (auto it_reachable = m_reachable_requests.find(request.chunk_id);  it_reachable != m_reachable_requests.end()) {
                            //test if ended.
                            if (it_reachable->second.finished) {
                                // weird. we don't have any nice peer, and we already fetch each peer.
                                // so, we are going to flood the world.
                                for (PeerPtr& peer : peers) {
                                    if (peer != sender) {
                                        saved_chunk_request.sent_to.push_back(peer);
                                        peer->writeMessage(*SynchMessagetype::GET_SINGLE_CHUNK, writeSingleChunkRequest(createSingleChunkRequest(request.chunk_id, true)));
                                    }
                                }
                                return;
                            } else {
                                //add us to the list
                                it_reachable->second.callbacks.push_back(call_chunkcallback);
                                return;
                            }
                        } else {
                            WaitingReachableRequest& saved_flood_request = m_reachable_requests[request.chunk_id];
                            saved_flood_request.id = request.chunk_id;
                            saved_flood_request.start = m_clusterManager->getCurrentTime();
                            saved_flood_request.callbacks.push_back(call_chunkcallback);
                            for (PeerPtr peer : peers) {
                                if (peer->isConnected()) {
                                    peer->writeMessage(*SynchMessagetype::GET_CHUNK_REACHABLE, writeChunkReachableRequest(createChunkReachableRequest(request.chunk_id)));
                                    saved_flood_request.sent_to.push_back(peer);
                                    request_sent = true;
                                    break;
                                }
                            }
                            return;
                        }
                    }
                }
            }
        }
        assert(false);
    }

    void ExchangeChunkMessageManager::answerSingleChunkRequest(PeerPtr& sender, const SingleChunkRequest& request) {
        if (FsElt::isChunk(request.chunk_id)) {
            answerSingleChunkRequest(sender, request, [this, sender, request](FsChunkTempPtr answer) {
                //forward the answer
                if (answer) {
                    sender->writeMessage(*SynchMessagetype::SEND_SINGLE_CHUNK, writeSingleChunkAnswer(createSingleChunkAnswer(request, answer)));
                } else {
                    sender->writeMessage(*SynchMessagetype::SEND_SINGLE_CHUNK, writeSingleChunkAnswer(createSingleChunkAnswer(request, {} )));
                }
                });

        } else if (FsElt::isFile(request.chunk_id)) {
            error(std::string("Error, cannot answer a single chunk request on a file"));
            //create a thing that agglomerate all chunks of the file.
            //when all chunks are gathered, give the answer.
            //TODO
        }

    }

    void ExchangeChunkMessageManager::useSingleChunkAnswer(PeerPtr& sender, const SingleChunkAnswer& answer) {
        std::lock_guard lock{ m_waiting_requests_mutex };
        //TODO: get the waiting request
        if (auto it_get_chunk = m_waiting_requests.find(answer.chunk_id_requested); it_get_chunk != m_waiting_requests.end()) {
            if (it_get_chunk->second.finished) {
                //already finished.. weird
                if (answer.chunk) {
                    // can receive multiple time the same chunk, if unlucky
                    msg("Warning: receive answer to an aready-finished request (useSingleChunkAnswer)");
                }
                return;
            }
            //'fusion'
            assert(!it_get_chunk->second.our_answer.chunk);
            if (answer.chunk) {
                it_get_chunk->second.our_answer.chunk = answer.chunk;
            }
            // remove sender from the 'sent_to' list (also if the peer is disconnected in the meantime)
            foreach(peer_it, it_get_chunk->second.sent_to) {
                if (peer_it->get() == sender.get() || !(*peer_it)->isConnected()) {
                    peer_it.erase();
                }
            }
            // do we have exhausted the options or we have the requested chunk?
            if (it_get_chunk->second.sent_to.empty() || it_get_chunk->second.our_answer.chunk) {
                //finished: send result
                it_get_chunk->second.finished = true;
                //emit the anwser to the requesters.
                foreach(requester_it, it_get_chunk->second.requester) {
                    requester_it.get()->writeMessage(*SynchMessagetype::SEND_CHUNK_REACHABLE, writeSingleChunkAnswer(it_get_chunk->second.our_answer));
                    requester_it.erase();
                }
                //call the callbacks
                foreach(callback_it, it_get_chunk->second.callbacks) {
                    callback_it.get()(it_get_chunk->second.our_answer);
                    callback_it.erase();
                }
            }
        } else {
            error("Error: receive answer to a not-existent request (useSingleChunkAnswer)");
        }

    }

}
