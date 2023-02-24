#pragma once

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsChunk.hpp"
#include "fs/base/FsObject.hpp"
#include "fs/base/FsFile.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"
#include "network/Peer.hpp"

#include <map>
#include <unordered_set>

namespace supercloud {


	class ExchangeChunkMessageManager : public AbstractMessageManager, public std::enable_shared_from_this<ExchangeChunkMessageManager> {
	public:
		struct ChunkAvailabilityRequest {
			//chunk, file or directory. request for all chunks under that.
			FsID elt_root;
			bool only_you;
		};
		struct ChunkAvailabilityComputer {
			ComputerId cid; // who?
			bool has_chunk; // false if it's a 'deletion' (deletion storage has a timeout)
			Date since; //when this chunk has been added/deleted from the computer
			Date last_fetched; //when the informatio has been refreshed in the peer that send this ChunkAvailabilityChunk .
		};
		struct ChunkAvailabilityAnswer {
			std::unordered_map<FsID, std::vector<ChunkAvailabilityComputer>> availability;
		};

		struct ChunkReachableRequest {
			FsID chunk_or_file_id;
		};
		struct ChunkReachablePath {
			ComputerId cid; // who has it
			uint8_t hops; // how many gateways
			uint8_t difficulty; // a difficulty estimate (0 = Gb/s with low latency and unused computer, 200: slow connention on a busy computer(s))
		};
		struct ChunkReachableAnswer {
			FsID chunk_or_file_id_request; // the  request file/chunk
			std::unordered_map<FsID, ChunkReachablePath> paths; // best path for each chunk
		};

		struct ChunkRequest {
			//chunk requested, or all chunks if it's the file id.
			FsID chunk_or_file_id;
			//offset inside the file/chunk data, default: 0
			size_t offset;
			//size of the data requested, or 0 to grab all the data.
			size_t size;
			//say that it can't be fetch directly, and the request need to use the receiver of this request as a gateway.
			bool need_gateway;
		};
		//note: currently, we only use full chunk
		//TODO: store in hard DD and not in memory if chunk is big
		class FsChunkStub : public FsChunk {
		protected:
			ByteBuff m_partial_data;
			//size_t m_partial_offset; // offset in the chunk, if the answer isn't full (partial data)
			//uint64_t m_partial_hash; // hash of the partial data
			size_t m_real_size; // size
		public:
			FsChunkStub(FsID id, DateTime date, uint64_t hash,
				ByteBuff&& partial_data, size_t partial_offset, uint64_t partial_hash,
				size_t file_offset, size_t real_size)
				: FsChunk(id, date, hash),
				m_partial_data(std::move(partial_data)), /*m_partial_offset(partial_offset), m_partial_hash(partial_hash),*/
				m_real_size(real_size), real_file_offset(file_offset) {}
			size_t real_file_offset; // offset in file
			virtual bool read(ByteBuff& to_append, size_t offset, size_t size) const override {
				assert(m_real_size >= size + offset);
				//check if it's inside our data
				/*if (offset < m_partial_offset) {
					return false;
				}*/
				if (size > /*m_partial_offset +*/ m_partial_data.limit()) {
					return false;
				}
				to_append.put(m_partial_data.raw_array() + offset /*- m_partial_offset*/, size);
				return true;
			}
			virtual DateTime getLastAccessDate() override { return 0; }
			virtual size_t size() const override { return m_real_size; }
			bool check_hash() { return compute_naive_hash(m_partial_data.raw_array(), m_partial_data.limit()); }

		};
		struct ChunkAnswer {
			FsID chunk_or_file_id_request; // the  request file/chunk
			std::vector<FsChunkStub> chunks;
			uint8_t hash_algo; // 0 = naive
		};


	protected:
		struct WaitingChunkRequest {
			FsID id;
			DateTime start; // remove the entry after a minute or two.
			std::vector<PeerPtr> requester; // the peers that request it to us
			std::vector<PeerPtr> sent_to; // the peers we ask it for
			ChunkAnswer our_answer; // answer we create bit by bit.
			std::vector<std::function<void(ChunkAnswer&)>> callbacks; // if it's me who want to use it (mostly 0 or 1, low chance of multiple functions)
			bool finished = fase;
		};

		//the requests, with the async promise.
		std::mutex m_waiting_requests_mutex;
		std::unordered_map<FsID, WaitingChunkRequest> m_waiting_requests;

		//reachable
		struct  WaitingReachableRequest {
			FsID id;
			DateTime start; // remove the entry after a minute or two.
			std::vector<PeerPtr> requester; // the peers that request it to us
			std::vector<PeerPtr> sent_to; // the peers we ask it for
			ChunkReachableAnswer our_answer; // answer we create bit by bit.
			std::vector<std::function<void(ChunkReachableAnswer&)>> callbacks; // if it's me who want to use it (mostly 0 or 1, low chance of multiple functions)
			bool finished = fase;
		};
		std::mutex m_reachable_requests_mutex;
		std::unordered_map<FsID, WaitingReachableRequest> m_reachable_requests; //TODO: remove old entries via timer

		std::shared_ptr<FsStorage> m_filesystem;
		std::shared_ptr<SynchroDb> m_syncro;
		std::shared_ptr<ClusterManager> m_clusterManager;

		inline void register_listener() {
			m_clusterManager->registerListener(*SynchMessagetype::GET_CHUNK, this->ptr());
			m_clusterManager->registerListener(*SynchMessagetype::SEND_CHUNK, this->ptr());
			m_clusterManager->registerListener(*SynchMessagetype::GET_CHUNK_AVAILABILITY, this->ptr());
			m_clusterManager->registerListener(*SynchMessagetype::SEND_CHUNK_AVAILABILITY, this->ptr());
		}
	public:

		//factory
		[[nodiscard]] static std::shared_ptr<ExchangeChunkMessageManager> create(ClusterManager& physicalServer) {
			std::shared_ptr<ExchangeChunkMessageManager> pointer = std::shared_ptr<ExchangeChunkMessageManager>{ new ExchangeChunkMessageManager(/*TODO*/) };
			pointer->register_listener();
			return pointer;
		}

		std::shared_ptr<ExchangeChunkMessageManager> ptr() {
			return shared_from_this();
		}

		//simple fetch: a chunk
		//TODO: fetch a random part of a file.
		std::future<ChunkAnswer> fetchChunk(FsID chunkId) {

		}

		ChunkAvailabilityRequest createChunkAvailabilityRequest(FsID root) {
			return ChunkAvailabilityRequest{ root, false };
		}
		ByteBuff writeChunkAvailabilityRequest(ChunkAvailabilityRequest request) {
			ByteBuff buff;
			buff.putULong(request.elt_root);
			buff.put(request.only_you ? 1 : 0);
			return buff.flip();
		}
		ChunkAvailabilityRequest readChunkAvailabilityRequest(ByteBuff& buffer) {
			return ChunkAvailabilityRequest{ buffer.getULong(), buffer.get() == 1 };
		}

		// send my synch db
		void fillChunkAvailabilityAnswer(ChunkAvailabilityAnswer& answer, FsID id, bool only_us) {
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
		ChunkAvailabilityAnswer createChunkAvailabilityAnswer(ChunkAvailabilityRequest request) {
			ChunkAvailabilityAnswer answer;
			fillChunkAvailabilityAnswer(answer, request.elt_root, request.only_you);
			return answer;
		}

		ByteBuff writeChunkAvailabilityAnswer(ChunkAvailabilityAnswer request) {
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
		ChunkAvailabilityAnswer readChunkAvailabilityAnswer(ByteBuff& buffer) {
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
		void useChunkAvailabilityAnswer(PeerPtr sender, ChunkAvailabilityAnswer request) {
			for (const auto& fsid_2_availability : request.availability) {
				for (const auto& cid_2_avail : fsid_2_availability.second) {
					//only consider fresh information
					if (cid_2_avail.last_fetched < SynchroDb::MAX_RETENTION_DELETION_INFO) {
						// get our version (if any)
						std::optional<const ComputerAvailability&> our_cid_avail = m_syncro->getAvailability(fsid_2_availability.first, cid_2_avail.cid);
						//replace our info if: no previous info
						bool need_replace = !our_cid_avail.has_value();
						//or first-hand info
						need_replace = need_replace || cid_2_avail.cid == sender->getComputerId();
						//or more recent
						need_replace = need_replace || (our_cid_avail->date_information < cid_2_avail.last_fetched&& our_cid_avail->date_state < cid_2_avail.since);
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


		virtual void receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) override {
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
				useChunkAvailabilityAnswer(sender, result);
			} else if (messageId == *SynchMessagetype::GET_CHUNK) {
				log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE GET_FILE_CHUNK from " + sender->getPeerId());
			} else if (messageId == *SynchMessagetype::SEND_CHUNK) {
				log(std::to_string(m_clusterManager->getComputerId()) + "$ RECEIVE SEND_FILE_CHUNK from " + sender->getPeerId());
				readChunk(sender, message);
			}

		}

		//TODO: this is too low level, please add some intelligence.
		// 1- answer to minute-refresh: send a refresh about 1 something that hasn't been fetch since some days.
		// 2- dunno. quick-chunk access should be taken care of by GET_CHUNK_REACHABLE
		void requestFetchInformationFor(PeerPtr peer, FsID root) {
			ChunkAvailabilityRequest request = createChunkAvailabilityRequest(root);
			peer->writeMessage(*SynchMessagetype::GET_CHUNK_AVAILABILITY, writeChunkAvailabilityRequest(request));
		}

		void requestChunk() {
			//find a connected peer that has it.
			//if not, flood the connected peers with a GET_CHUNK_REACHABLE request to find the best path(s).

			std::lock_guard lock{ m_waiting_requests_mutex };
			ChunkRequest request = createChunkRequest(0);
			m_waiting_requests[]

				std::unordered_map<ComputerId, std::vector<WaitingRequest>> m_waiting_requests;

		}

		//if you're calling that, don't forget to add an entry in the request table
		ChunkReachableRequest createChunkReachableRequest(FsID id) {
			return ChunkReachableRequest{ id };
		}
		ByteBuff writeChunkReachableRequest(ChunkReachableRequest request) {
			assert(m_reachable_requests.find(request.chunk_or_file_id) != m_reachable_requests.end());
			ByteBuff buff;
			buff.putULong(request.chunk_or_file_id);
			return buff.flip();
		}
		ChunkReachableRequest readChunkReachableRequest(ByteBuff& buffer) {
			return ChunkReachableRequest{ buffer.getULong() };
		}
		ByteBuff writeChunkReachableAnswer(ChunkReachableAnswer answer) {
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
		ChunkReachableAnswer readChunkReachableAnswer(ByteBuff& buffer) {
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
		void answerChunkReachableRequest(PeerPtr sender, ChunkReachableRequest& request) {
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

		void useChunkReachableAnswer(PeerPtr sender, const ChunkReachableAnswer& answer) {
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
		ChunkRequest createChunkRequest(FsID chunk_or_file_to_get, bool gateway = true, size_t offset = 0, size_t size = 0) {
			assert(FsElt::isFile(chunk_or_file_to_get) || FsElt::isChunk(chunk_or_file_to_get));
			return ChunkRequest{ chunk_or_file_to_get , offset, size, gateway };
		}
		ByteBuff writeChunkRequest(const ChunkRequest& request) {
			assert(m_waiting_requests.find(request.chunk_or_file_id) != m_waiting_requests.end());
			ByteBuff buff;
			buff.putULong(request.chunk_or_file_id);
			buff.putSize(request.offset);
			buff.putSize(request.size);
			buff.put(request.need_gateway ? 1 : 0);
			return buff.flip();
		}
		ChunkRequest readChunkRequest(ByteBuff& buffer) {
			return ChunkRequest{ buffer.getULong(), buffer.getSize(), buffer.getSize(), buffer.get() == 1 };
		}
		ChunkAnswer createChunkAnswer(const ChunkRequest& request, std::vector<FsChunkStub>&& stubs) {
			return ChunkAnswer{ request.chunk_or_file_id , stubs, 0 };
		}
		ByteBuff writeChunkAnswer(const ChunkAnswer& answer) {
			ByteBuff buff;
			buff.putULong(answer.chunk_or_file_id_request);
			buff.put(answer.hash_algo);
			buff.putSize(answer.chunks.size());
			for (const FsChunkStub& chunk : answer.chunks) {
				buff.putULong(chunk.getId());
				buff.putLong(chunk.getDate());
				buff.putULong(chunk.getHash());
				buff.putSize(chunk.size());
				chunk.read(buff, 0, chunk.size());
			}
			return buff.flip();
		}
		ChunkAnswer readChunkAnswer(ByteBuff& buffer) {
			ChunkAnswer answer;
			answer.chunk_or_file_id_request = buffer.getULong();
			answer.hash_algo = buffer.get();
			size_t nb_chunks = buffer.getSize();
			for (size_t i = 0; i < nb_chunks; ++i) {
				FsID id = buffer.getULong();
				DateTime time = buffer.getLong();
				uint64_t hash = buffer.getULong();
				size_t buff_size = buffer.getSize();
				ByteBuff chunk_data;
				buffer.get(chunk_data, buff_size);
				answer.chunks.push_back(FsChunkStub{ id, time, hash,
					std::move(chunk_data), 0, hash, 0, buff_size });
			}
			assert(buffer.available() == 0);
			return answer;
		}

		void answerSingleChunkRequest(PeerPtr& sender, const ChunkRequest& request, const std::function<void(std::optional<FsChunkStub&>)>& callback) {
			std::lock_guard lock{ m_waiting_requests_mutex };
			assert(FsElt::isChunk(request.chunk_or_file_id));
			//do i have the chunk?
			if (m_filesystem->hasLocally(request.chunk_or_file_id)) {
				//i have evrything -> give result
				FsChunkPtr my_chunk = m_filesystem->loadChunk(request.chunk_or_file_id);
				ByteBuff data;
				my_chunk->read(data, 0, my_chunk->size());
				callback(std::optional(FsChunkStub{ my_chunk->getId(), my_chunk->getDate(), my_chunk->getHash(),
					std::move(data), 0, my_chunk->getHash(), 0, my_chunk->size() }));
				return;
			} else {
				//get all connected peers
				PeerList peers = m_clusterManager->getPeersCopy();
				//remove our sender
				foreach(it, peers) { if (it->get() == sender.get() || !(*it)->isConnected()) it.erase(); }
				//the callback to our caller callback (to avoid duplication, as it's used two time below)
				auto call_chunkcallback = [callback](ChunkAnswer& answer) {
					if (answer.chunks.size() == 1) {
						callback(std::optional(answer.chunks.front()));
						return;
					} else {
						assert(answer.chunks.empty());
						callback(std::nullopt);
						return;
					}
				};
				//is there enough peers?
				if (peers.empty()) {
					callback(std::nullopt);
					return;
				} else {
					//create the request
					if (auto it_get_chunk = m_waiting_requests.find(request.chunk_or_file_id); it_get_chunk != m_waiting_requests.end()) {
						//request already exist, use it
						if (it_get_chunk->second.finished) {
							if (it_get_chunk->second.our_answer.chunks.size() == 1) {
								callback(std::optional(it_get_chunk->second.our_answer.chunks.front()));
								return;
							} else {
								assert(it_get_chunk->second.our_answer.chunks.empty());
								callback(std::nullopt);
								return;
							}
						} else {
							//note: callback by copy, to be sure it still exists.
							it_get_chunk->second.callbacks.push_back(call_chunkcallback);
							return;
						}
					} else {
						//request not here, create it.
						WaitingChunkRequest& saved_chunk_request = m_waiting_requests[request.chunk_or_file_id];
						saved_chunk_request.id = request.chunk_or_file_id;
						saved_chunk_request.start = m_clusterManager->getCurrentTime();
						saved_chunk_request.callbacks.push_back(call_chunkcallback);
						bool request_sent = false;
						// get the best peer
						Availability available = m_syncro->getAvailability(request.chunk_or_file_id);
						if (available.current_best_path != 0 && available.current_best_path != NO_COMPUTER_ID) {
							//get peer
							for (PeerPtr peer : peers) {
								if (peer->isConnected() && peer->getComputerId() == available.current_best_path) {
									peer->writeMessage(*SynchMessagetype::GET_CHUNK, writeChunkRequest(createChunkRequest(request.chunk_or_file_id, true, request.offset, request.size)));
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
								WaitingChunkRequest& saved_chunk_request = m_waiting_requests[request.chunk_or_file_id];
								bool request_sent = false;
								//if one path for our one chunk, then follow it
								if (auto it = reach.paths.find(reach.chunk_or_file_id_request); it != reach.paths.end()) {
									//get peer
									for (PeerPtr peer : m_clusterManager->getPeersCopy()) {
										if (peer->isConnected() && peer->getComputerId() == it->second.cid) {
											peer->writeMessage(*SynchMessagetype::GET_CHUNK, writeChunkRequest(createChunkRequest(request.chunk_or_file_id, true, request.offset, request.size)));
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
							if (auto it_reachable = m_reachable_requests.find(request.chunk_or_file_id);  it_reachable != m_reachable_requests.end()) {
								//test if ended.
								if (it_reachable->second.finished) {
									// weird. we don't have any nice peer, and we already fetch each peer.
									// so, we are going to flood the world.
									for (PeerPtr& peer : peers) {
										if (peer != sender) {
											saved_chunk_request.sent_to.push_back(peer);
											peer->writeMessage(*SynchMessagetype::GET_CHUNK, writeChunkRequest(createChunkRequest(request.chunk_or_file_id, true, request.offset, request.size)));
										}
									}
									return;
								} else {
									//add us to the list
									it_reachable->second.callbacks.push_back(call_chunkcallback);
									return;
								}
							} else {
								WaitingReachableRequest& saved_flood_request = m_reachable_requests[request.chunk_or_file_id];
								saved_flood_request.id = request.chunk_or_file_id;
								saved_flood_request.start = m_clusterManager->getCurrentTime();
								saved_flood_request.callbacks.push_back(call_chunkcallback);
								for (PeerPtr peer : peers) {
									if (peer->isConnected()) {
										peer->writeMessage(*SynchMessagetype::GET_CHUNK_REACHABLE, writeChunkReachableRequest(createChunkReachableRequest(request.chunk_or_file_id)));
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

		void answerChunkRequest(PeerPtr& sender, const ChunkRequest& request) {
			if (FsElt::isChunk(request.chunk_or_file_id)) {
				answerSingleChunkRequest(sender, request, [this, sender, request](std::optional<FsChunkStub&> answer) {
					//forward the answer
					if (answer.has_value()) {
						sender->writeMessage(*SynchMessagetype::SEND_CHUNK, writeChunkAnswer(createChunkAnswer(request, { *answer })));
					} else {
						sender->writeMessage(*SynchMessagetype::SEND_CHUNK, writeChunkAnswer(createChunkAnswer(request, {})));
					}
					});

			} else if (FsElt::isFile(request.chunk_or_file_id)) {
				//create a thing that agglomerate all chunks of the file.
				//when all chunks are gathered, give the answer.
				//TODO
			}

		}

		void useChunkAnswer(PeerPtr& sender, const ChunkAnswer& answer) {
			//TODO: get the waiting request
			//if finished, call the callbacks.

		}

	};

}
