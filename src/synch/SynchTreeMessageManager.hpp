#pragma once

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsObject.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"
#include "network/Peer.hpp"

#include <map>
#include <unordered_set>
#include <numeric>

namespace supercloud {

	class SynchTreeMessageManager : public AbstractMessageManager, public std::enable_shared_from_this<SynchTreeMessageManager> {

	public:

		struct TreeAnswerElt {
			FsID elt_id;
			uint16_t elt_depth;
			size_t elt_size;
			FsID last_commit_id;
			DateTime last_commit_time;
			bool operator==(const TreeAnswerElt& other) const {
				return elt_id == other.elt_id && elt_depth == other.elt_depth && elt_size == other.elt_size
					&& last_commit_id == other.last_commit_id && last_commit_time == other.last_commit_time;
			}
		};
		struct TreeAnswerEltChange : TreeAnswerElt {
			std::vector<FsID> state;
			std::vector<size_t> sizes;
			bool operator==(const TreeAnswerEltChange& other) const {
				return TreeAnswerElt::operator==(other) && state == other.state;
			}
		};
		struct TreeAnswerEltDeleted : TreeAnswerElt {
			FsID renamed_to; //can be 0 if just deleted
			bool operator==(const TreeAnswerEltDeleted& other) const {
				return TreeAnswerElt::operator==(other) && renamed_to == other.renamed_to;
			}
		};
		//a fake object, to have all information about the last state of the object (with id & date of the last commit, but no data inside)
		class FsObjectTreeAnswer : public FsObject {
			std::vector<size_t> m_sizes;
			size_t m_size;
		public:
			FsObjectTreeAnswer(FsID obj_id, uint16_t depth, DateTime creation_time, std::string name, CUGA puga, uint32_t group, FsID parent) :
				FsObject(obj_id, creation_time, name, puga, group, parent, depth, 0, {}, {}, 0, 0), m_sizes(), m_size(0) {
			}
			FsObjectTreeAnswer(FsID id, uint16_t depth) :
				FsObject(id, 0 /*time*/, "", 0, 0, 0/*parent*/, depth, 0, {}, { }, 0, 0), m_sizes(), m_size(0) {
			}
			FsObjectTreeAnswer& setCommit(std::vector<FsID> state, FsID id_commit, DateTime time, std::vector<size_t> sizes) {
				m_current_state = state;
				m_commits.emplace_back();
				m_commits.back().id = id_commit;
				m_commits.back().time = time;
				m_sizes = sizes;
				m_size = std::accumulate(m_sizes.begin(), m_sizes.end(), size_t(0));
				return *this;
			}
			FsObjectTreeAnswer& setDeleted(FsID renamed_to, DateTime time) {
				m_time_deleted = time;
				m_renamed_to = renamed_to;
				return *this;
			}
			//default -set
			FsObjectTreeAnswer(const FsObjectTreeAnswer& o) // can copy
				:FsObject(o.m_id, o.m_creation_time, o.m_name, o.m_puga, o.m_group_id, o.m_parent, o.m_depth, o.m_renamed_from, 
					o.m_current_state, o.m_commits, o.m_time_deleted, o.m_renamed_to), m_sizes(o.m_sizes){
				m_size = std::accumulate(m_sizes.begin(), m_sizes.end(), size_t(0));
			}
			//FsObjectTreeAnswer(FsObjectTreeAnswer&& o) // can move
			//	:FsObject(o), m_depth(o.m_depth), m_sizes(std::move(o.m_sizes)) {
			//	m_size = std::accumulate(m_sizes.begin(), m_sizes.end(), size_t(0));
			//}
			size_t size() const override {
				assert(FsElt::isDirectory(m_id) || std::accumulate(m_sizes.begin(), m_sizes.end(), size_t(0)) == m_size);
				return m_size;
			}
			const std::vector<size_t>& sizes() const override {
				assert(FsElt::isDirectory(m_id) || std::accumulate(m_sizes.begin(), m_sizes.end(), size_t(0)) == m_size);
				assert(m_sizes.size() == m_current_state.size());
				return m_sizes;
			}
			bool operator==(const FsObjectTreeAnswer& other) const {
				return m_depth == other.m_depth && m_size == other.m_size && m_sizes == other.m_sizes && m_id == other.m_id
					&& m_creation_time == other.m_creation_time && m_puga == other.m_puga && m_name == other.m_name
					&& m_group_id == other.m_group_id && m_parent == other.m_parent && m_current_state == other.m_current_state
					&& m_commits == other.m_commits && m_time_deleted == other.m_time_deleted && m_renamed_to == other.m_renamed_to
					&& m_renamed_from == other.m_renamed_from;
			}
		};

		struct TreeRequest {
			// used to identify the request
			uint64_t request_id = 0;
			//what we want as information (the FsID we don't know about)
			std::vector<FsID> roots;
			//how many information
			size_t depth = uint16_t(-1); // 0=only these one, 1 = also their childs, etc... uint16_t(-1) (ie the max as it's unsigned) for evrything, but i guess 16k depth is enough
			//last commit we have for this element (if it's the same you have, then you can return a null answer)
			FsID last_commit_received = 0; // note: currently not used to create the answer TODO: do something with it or erase?
			// last time we fetched you (please give all information that you received/modified since that moment). You can set it to 0 to force resend.
			DateTime last_fetch_time = 0;
		};
		typedef std::shared_ptr<FsObjectTreeAnswer> FsObjectTreeAnswerPtr;
		struct TreeAnswer {
			uint64_t request_id;
			// the computer that answer the request
			ComputerId from;
			// time of the fetch
			DateTime answer_time = 0;
			// all the element in the tree requested that have been modified / created.
			std::vector<TreeAnswerEltChange> modified;
			std::vector<FsObjectTreeAnswerPtr> created; //ptr because it's immutable and vector can't store immutable things...
			// 1 here are the elements that are renamed to something else
			// 2 the deleted element may not be listed. You have to infer them (all childs of a modified dir that are not in its current state anymore are deleted)
			// for renamed element, check the renamed_from from the created elements.
			// 1 we need the delete date & commit id
			// if a directory is deleted, all the content is also deleted (using the same commit id is okay-ish, but it should be marked as "unknown deletion id")
			std::vector<TreeAnswerEltDeleted> deleted;
			// when somethign requested doesn't have any changes
			std::vector<FsID> unchanged;
			//other requested id not here are unreachable.
		};

		typedef std::shared_ptr<TreeAnswer> TreeAnswerPtr;

		struct InvalidateElementsMessage {
			DateTime last_invalidation_time;
			// elt that are invalidated
			std::vector<FsID> modified;
			// commits that invalidated the elements (computer-modifier is the cid inside it)
			std::vector<FsCommitID> commits;
			bool operator==(const InvalidateElementsMessage& other) const {
				return commits == other.commits && modified == other.modified;
			}
		};

	protected:

		std::shared_ptr<FsStorage> m_filesystem;
		std::shared_ptr<SynchroDb> m_syncro;
		std::shared_ptr<ClusterManager> m_network; //PhysicalServer becasue i need getPeerPtr

		struct TreeAnswerRequest {
			ComputerId request_to;
			uint64_t request_id; // the id of the request sent by the requester, to help him to find it back.
			std::vector<FsID> roots; // what we want
			//std::vector<FsID> unavailable; // what we can't have (shouldn't happen)
			// we only ask that at reachable peers
			TreeAnswer our_answer; // answer we create bit by bit.
			std::vector<std::function<void(TreeAnswer*)>> callbacks;
			bool finished = false;
			DateTime start = 0;
			DateTime finished_since = 0;
		};
		std::atomic<uint64_t> m_request_id_generator;
		std::recursive_mutex m_mutex_incomplete_requests;
		std::unordered_map<uint64_t, TreeAnswerRequest> m_incomplete_requests;
		static const inline DateTime MAX_MILISECONDS_FOR_MSG_ABANDON = 3000;

		void register_listener();

		void fillTreeAnswer(ComputerId cid_requester, TreeAnswer& answer, FsID elt_id, size_t depth, DateTime since, std::unordered_set<FsID>& already_seen);
	public:
		//factory
		[[nodiscard]] static std::shared_ptr<SynchTreeMessageManager> create(std::shared_ptr<ClusterManager> physical_server, std::shared_ptr<FsStorage> filesystem, std::shared_ptr<SynchroDb> syncro) {
			std::shared_ptr<SynchTreeMessageManager> pointer = std::shared_ptr<SynchTreeMessageManager>{ new SynchTreeMessageManager(/*TODO*/) };
			pointer->m_syncro = syncro;
			pointer->m_filesystem = filesystem;
			pointer->m_network = physical_server;
			pointer->register_listener();
			return pointer;
		}

		std::shared_ptr<SynchTreeMessageManager> ptr() {
			return shared_from_this();
		}

		void receiveMessage(PeerPtr peer, uint8_t messageId, const ByteBuff& message) override;

		//future allow to block execution until there is a result.
		//callback avoid the need to fetch the result afterwards if it's not blocking.
		std::future<TreeAnswerPtr> fetchTree(FsID root, ComputerId cid = NO_COMPUTER_ID);
		void fetchTree(/*ComputerId cid, */FsID root, const std::function<void(TreeAnswerPtr)>& callback, ComputerId cid = NO_COMPUTER_ID);
		// it will create the registration, and put the request id inside the 'unregistered_request' object
		SynchTreeMessageManager::TreeAnswerRequest& SynchTreeMessageManager::registerRequest(TreeRequest& unregistered_request, ComputerId cid);

		// GET/SEND_TREE
		ByteBuff writeTreeRequestMessage(const TreeRequest& request);
		TreeRequest readTreeRequestMessage(const ByteBuff& buffer);
		// retun true if the answer is emmited, false if not.
		bool checkAndAnswerTreeRequest(const PeerPtr sender, TreeRequest& request);
		TreeAnswer answerTreeRequest(const PeerPtr sender, TreeRequest& request);
		ByteBuff writeTreeAnswerMessage(const TreeAnswer& request);
		TreeAnswer readTreeAnswerMessage(const ByteBuff& buffer);
		void useTreeRequestAnswer(const PeerPtr sender, TreeAnswer&& answer);

		// SEND_INVALIDATE_ELT
		void emitModificationsNotification(std::unordered_map<FsID, Invalidation> modified);
		ByteBuff writeInvalidateEltsMessage(const std::vector<InvalidateElementsMessage>& request);
		std::vector<InvalidateElementsMessage> readInvalidateEltsMessage(const ByteBuff& buffer);
		void useInvalidateEltsAnswer(const PeerPtr sender, const InvalidateElementsMessage& answer);
	};
}
