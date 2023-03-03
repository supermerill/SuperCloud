#pragma once

//note: don't work, as test use the lib, and not directly compile that.
#ifndef TEST_VISIBILITY
#define protected_but_for_test public
#else
#define protected_but_for_test public
#endif

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsChunk.hpp"
#include "fs/base/FsObject.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"

#include <map>
#include <unordered_set>

namespace supercloud {


	class ExchangeChunkMessageManager : public AbstractMessageManager, public std::enable_shared_from_this<ExchangeChunkMessageManager> {
	public:
		struct ChunkAvailabilityRequest {
			//chunk, file or directory. request for all chunks under that.
			FsID elt_root;
			bool only_you;
			bool operator==(const ChunkAvailabilityRequest& other) const {
				return elt_root == other.elt_root && only_you == other.only_you;
			}
		};
		struct ChunkAvailabilityComputer {
			ComputerId cid; // who?
			bool has_chunk; // false if it's a 'deletion' (deletion storage has a timeout)
			Date since; //when this chunk has been added/deleted from the computer
			Date last_fetched; //when the informatio has been refreshed in the peer that send this ChunkAvailabilityChunk .
			bool operator==(const ChunkAvailabilityComputer& other) const {
				return cid == other.cid && has_chunk == other.has_chunk && since == other.since && last_fetched == other.last_fetched;
			}
		};
		struct ChunkAvailabilityAnswer {
			std::unordered_map<FsID, std::vector<ChunkAvailabilityComputer>> availability;
			bool operator==(const ChunkAvailabilityAnswer& other) const { return availability == other.availability; }
		};

		struct ChunkReachableRequest {
			FsID chunk_or_file_id;
			bool operator==(const ChunkReachableRequest& other) const { return chunk_or_file_id == other.chunk_or_file_id; }
		};
		struct ChunkReachablePath {
			ComputerId cid; // who has it
			uint8_t hops; // how many gateways
			uint8_t difficulty; // a difficulty estimate (0 = Gb/s with low latency and unused computer, 200: slow connention on a busy computer(s))
			bool operator==(const ChunkReachablePath& other) const { return cid == other.cid && hops == other.hops && difficulty == other.difficulty; }
		};
		struct ChunkReachableAnswer {
			FsID chunk_or_file_id_request; // the  request file/chunk
			std::unordered_map<FsID, ChunkReachablePath> paths; // best path for each chunk
			bool operator==(const ChunkReachableAnswer& other) const { return chunk_or_file_id_request == other.chunk_or_file_id_request && paths == other.paths; }
		};

		struct SingleChunkRequest {
			//chunk requested
			FsID chunk_id;
			//say that it can't be fetch directly, and the request need to use the receiver of this request as a gateway.
			bool need_gateway;
			bool operator==(const SingleChunkRequest& other) const { return chunk_id == other.chunk_id && need_gateway == other.need_gateway; }
		};
		//note: currently, we only use full chunk
		//TODO: store in hard DD and not in memory if chunk is big
		class FsChunkTempElt : public FsChunk {
		protected:
			ByteBuff m_partial_data;
			//size_t m_partial_offset; // offset in the chunk, if the answer isn't full (partial data)
			//uint64_t m_partial_hash; // hash of the partial data
			size_t m_real_size; // size
			FsChunkPtr m_real_chunk;
		public:
			FsChunkTempElt(FsID id, DateTime date, uint64_t hash,
				ByteBuff&& partial_data, size_t partial_offset, uint64_t partial_hash,
				size_t file_offset, size_t real_size)
				: FsChunk(id, date, hash),
				m_partial_data(std::move(partial_data)), /*m_partial_offset(partial_offset), m_partial_hash(partial_hash),*/
				m_real_size(real_size), real_file_offset(file_offset) {}
			FsChunkTempElt(FsChunkPtr real) : FsChunk(real->getId(), real->getDate(), real->getHash()),
				m_partial_data(), /*m_partial_offset(partial_offset), m_partial_hash(partial_hash),*/
				m_real_size(real->size()), real_file_offset(0), m_real_chunk(real) {}
			//FsChunkTempElt(FsChunkTempElt&& other) : FsChunk(other.getId(), other.getDate(), other.getHash()),
			//	m_partial_data(other.m_partial_data), /*m_partial_offset(partial_offset), m_partial_hash(partial_hash),*/
			//	m_real_size(other.m_real_size), real_file_offset(other.real_file_offset), m_real_chunk(other.m_real_chunk) {}
			FsChunkTempElt(const FsChunkTempElt&) = delete; // can't copy
			FsChunkTempElt& operator=(const FsChunkTempElt&) = delete; // can't copy
			bool operator==(const FsChunkTempElt& other) const { //for test, it's not an efficient compare.
				bool ok = size() == other.size();
				ok &= m_id == other.m_id && m_creation_date == other.m_creation_date && m_parents == other.m_parents && m_hash == other.m_hash && m_is_local == other.m_is_local;
				if (ok) {
					ByteBuff read1;
					ByteBuff read2;
					read(read1, 0, size());
					other.read(read2, 0, size());
					ok = read1 == read2;
				}
				return ok;
			}
			size_t real_file_offset; // offset in file
			virtual bool read(ByteBuff& to_append, size_t offset, size_t size) const override {
				if (m_real_chunk) {
					return m_real_chunk->read(to_append, offset, size);
				} else {
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
			}
			virtual DateTime getLastAccessDate() override {
				if (m_real_chunk)
					return m_real_chunk->getLastAccessDate();
				else
					return 0;
			}
			virtual size_t size() const override { return m_real_size; }
			bool checkHash() {
				if (m_real_chunk)
					return m_real_chunk->checkIntegrity();
				else
					return compute_naive_hash(m_partial_data.raw_array(), m_partial_data.limit());
			}

		};
		typedef std::shared_ptr<FsChunkTempElt> FsChunkTempPtr;
		struct SingleChunkAnswer {
			FsID chunk_id_requested; // the  request file/chunk
			FsChunkTempPtr chunk; // std::optional, but easier to use
			uint8_t hash_algo; // 0 = naive
			bool operator==(const SingleChunkAnswer& other) const { return chunk_id_requested == other.chunk_id_requested && chunk == other.chunk && hash_algo == other.hash_algo; }
		};


	protected:
		struct WaitingSingleChunkRequest {
			FsID id; // id oof the chunk to transmit
			DateTime start; // remove the entry after a minute or two.
			std::vector<PeerPtr> requester; // the peers that request it to us
			std::vector<PeerPtr> sent_to; // the peers we ask it for
			SingleChunkAnswer our_answer; // answer we create bit by bit.
			std::vector<std::function<void(SingleChunkAnswer&)>> callbacks; // if it's me who want to use it (mostly 0 or 1, low chance of multiple functions)
			bool finished = false;
		};

		//the requests, with the async promise.
		std::mutex m_waiting_requests_mutex;
		std::unordered_map<FsID, WaitingSingleChunkRequest> m_waiting_requests;

		//reachable
		struct  WaitingReachableRequest {
			FsID id;
			DateTime start; // remove the entry after a minute or two.
			std::vector<PeerPtr> requester; // the peers that request it to us
			std::vector<PeerPtr> sent_to; // the peers we ask it for
			ChunkReachableAnswer our_answer; // answer we create bit by bit.
			std::vector<std::function<void(ChunkReachableAnswer&)>> callbacks; // if it's me who want to use it (mostly 0 or 1, low chance of multiple functions)
			bool finished = false;
		};
		std::mutex m_reachable_requests_mutex;
		std::unordered_map<FsID, WaitingReachableRequest> m_reachable_requests; //TODO: remove old entries via timer

		std::shared_ptr<FsStorage> m_filesystem;
		std::shared_ptr<SynchroDb> m_syncro;
		std::shared_ptr<ClusterManager> m_cluster_manager;

		void register_listener();
	public:

		//factory
		[[nodiscard]] static std::shared_ptr<ExchangeChunkMessageManager> create(std::shared_ptr<ClusterManager> physical_server, std::shared_ptr<FsStorage> filesystem, std::shared_ptr<SynchroDb> syncro) {
			std::shared_ptr<ExchangeChunkMessageManager> pointer = std::shared_ptr<ExchangeChunkMessageManager>{ new ExchangeChunkMessageManager(/*TODO*/) };
			pointer->m_syncro = syncro;
			pointer->m_filesystem = filesystem;
			pointer->m_cluster_manager = physical_server;
			pointer->register_listener();
			return pointer;
		}

		std::shared_ptr<ExchangeChunkMessageManager> ptr() {
			return shared_from_this();
		}

		//simple fetch: a chunk
		//TODO: fetch a random part of a file.
		//TODO: see what's the best way to use async: future or functions.
		//future allow to block execution until there is a result.
		//callback avoid the need to fetch the result afterwards if it's not blocking.
		std::future<FsChunkTempPtr> fetchChunk(FsID chunk_id);
		void fetchChunk(FsID chunk_id, const std::function<void(FsChunkTempPtr)>& callback);

		// request my synch db
	protected_but_for_test:
		ChunkAvailabilityRequest createChunkAvailabilityRequest(FsID root) const;
		ByteBuff writeChunkAvailabilityRequest(const ChunkAvailabilityRequest& request) const;
		ChunkAvailabilityRequest readChunkAvailabilityRequest(const ByteBuff& buffer) const;
		// send my synch db
		void fillChunkAvailabilityAnswer(ChunkAvailabilityAnswer& answer, FsID id, bool only_us) const;
		ChunkAvailabilityAnswer createChunkAvailabilityAnswer(const ChunkAvailabilityRequest& request) const;
		ByteBuff writeChunkAvailabilityAnswer(const ChunkAvailabilityAnswer& request) const;
		ChunkAvailabilityAnswer readChunkAvailabilityAnswer(const ByteBuff& buffer) const;
		void useChunkAvailabilityAnswer(PeerPtr sender, const ChunkAvailabilityAnswer& request);
	public:

		virtual void receiveMessage(PeerPtr sender, uint8_t messageId, const ByteBuff& message) override;

		//TODO: this is too low level, please add some intelligence.
		// 1- answer to minute-refresh: send a refresh about 1 something that hasn't been fetch since some days.
		// 2- dunno. quick-chunk access should be taken care of by GET_CHUNK_REACHABLE
		void requestFetchInformationFor(PeerPtr peer, FsID root);

		//doto: crate the use-case first, to be sure we create the right signature, with future and everything
		//void requestChunk();

		// request & answer for the current peer path to chunks
	protected_but_for_test:
		//if you're calling that, don't forget to add an entry in the request table
		ChunkReachableRequest createChunkReachableRequest(FsID id) const;
		WaitingReachableRequest& registerChunkReachableRequest(FsID elt_id);
		ByteBuff writeChunkReachableRequest(const ChunkReachableRequest& request) const;
		ChunkReachableRequest readChunkReachableRequest(const ByteBuff& buffer) const;
		ByteBuff writeChunkReachableAnswer(const ChunkReachableAnswer& answer) const;
		ChunkReachableAnswer readChunkReachableAnswer(const ByteBuff& buffer) const;
		void answerChunkReachableRequest(PeerPtr sender, const ChunkReachableRequest& request);
		void useChunkReachableAnswer(PeerPtr sender, const ChunkReachableAnswer& answer);


		// request & answer for a chunk data
	protected_but_for_test:
		//if you're calling that, don't forget to add an entry in the request table
		SingleChunkRequest createSingleChunkRequest(FsID chunk_or_file_to_get, bool gateway = true);
		WaitingSingleChunkRequest& registerSingleChunkRequest(FsID elt_id);
		ByteBuff writeSingleChunkRequest(const SingleChunkRequest& request) const;
		SingleChunkRequest readSingleChunkRequest(const ByteBuff& buffer) const;
		SingleChunkAnswer createSingleChunkAnswer(const SingleChunkRequest& request, FsChunkTempPtr stub) const;
		ByteBuff writeSingleChunkAnswer(const SingleChunkAnswer& answer) const;
		SingleChunkAnswer readSingleChunkAnswer(const ByteBuff& buffer) const;
		void answerSingleChunkRequest(PeerPtr& sender, const SingleChunkRequest& request, const std::function<void(FsChunkTempPtr)>& callback);
		void answerSingleChunkRequest(PeerPtr& sender, const SingleChunkRequest& request);
		void useSingleChunkAnswer(PeerPtr& sender, const SingleChunkAnswer& answer);

	};

}
