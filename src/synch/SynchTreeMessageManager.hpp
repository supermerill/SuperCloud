#pragma once

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsObject.hpp"
#include "AbstractFSMessageManager.hpp"
#include "SynchroDb.hpp"
#include "network/Peer.hpp"

#include <map>
#include <unordered_set>

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
			uint16_t m_depth;
			size_t m_size;
		public:
			FsObjectTreeAnswer(FsID id, uint16_t depth, size_t size, DateTime date, std::string name, CUGA puga, FsID parent, uint32_t group, std::vector<FsID> state) :
				FsObject(id, date, name, puga, parent), m_depth(depth), m_size(size) {
				m_current_state = state;
			}
			FsObjectTreeAnswer(FsID id, uint16_t depth, size_t size, std::vector<FsID> state) :
				FsObject(id, 0, "", 0, 0), m_depth(depth), m_size(size) {
				m_current_state = state;
			}
			FsObjectTreeAnswer& setCommit(FsID id_commit, DateTime date) {
				m_commits.emplace_back();
				m_commits.back().id = id_commit;
				m_commits.back().date = date;
				return *this;
			}
			FsObjectTreeAnswer& setDeleted(FsID renamed_to, DateTime date) {
				m_date_deleted = date;
				m_renamed_to = renamed_to;
				return *this;
			}
			//default -set
			FsObjectTreeAnswer(const FsObjectTreeAnswer& o) // can copy
				:FsObject(o.m_id, o.m_creation_date, o.m_name, o.m_puga, o.m_parent), m_depth(o.m_depth) {
				m_current_state = std::move(o.m_current_state);
			}
			FsObjectTreeAnswer(FsObjectTreeAnswer&& o) // can move
				:FsObject(o.m_id, o.m_creation_date, o.m_name, o.m_puga, o.m_parent), m_depth(o.m_depth) {
				m_current_state = std::move(o.m_current_state);
			}
			uint16_t getDepth() const override {
				return m_depth;
			}
			size_t size() const override {
				return m_size;
			}
			bool operator==(const FsObjectTreeAnswer& other) const {
				return m_depth == other.m_depth && m_size == other.m_size && m_id == other.m_id
					&& m_creation_date == other.m_creation_date && m_puga == other.m_puga && m_name == other.m_name
					&& m_group_id == other.m_group_id && m_parent == other.m_parent && m_current_state == other.m_current_state
					&& m_commits == other.m_commits && m_date_deleted == other.m_date_deleted && m_renamed_to == other.m_renamed_to
					&& m_renamed_from == other.m_renamed_from;
			}
		};

		struct TreeRequest {
			//what we want as information (the FsID we don't know about)
			std::vector<FsID> roots;
			//how many information
			size_t depth = uint16_t(-1); // 0=only these one, 1 = also their childs, etc... uint16_t(-1) (ie the max as it's unsigned) for evrything, but i guess 16k depth is enough
			//last commit we have for this element (if it's the same you have, then you can return a null answer)
			FsID last_commit_received = 0; // note: currently not used to crate the answer TODO: do something with it or erase?
			// last time we fetched you (please give all information that you received/modified since that moment)
			DateTime last_fetch_time = 0;
		};
		typedef std::shared_ptr<FsObjectTreeAnswer> FsObjectTreeAnswerPtr;
		struct TreeAnswer {
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
		};

		typedef std::shared_ptr<TreeAnswer> TreeAnswerPtr;

		struct InvalidateElementsMessage {
			ComputerId modifier;
			DateTime last_invalidation_time;
			std::vector<FsID> modified;
			bool operator==(const InvalidateElementsMessage& other) const {
				return modifier == other.modifier && modified == other.modified;
			}
		};

	protected:

		std::shared_ptr<FsStorage> m_filesystem;
		std::shared_ptr<SynchroDb> m_syncro;
		std::shared_ptr<ClusterManager> m_cluster_manager;

		struct TreeAnswerRequest {
			ComputerId id;
			std::vector<FsID> roots; // what we want
			//std::vector<FsID> unavailable; // what we can't have (shouldn't happen)
			// we only ask that at reachable peers
			TreeAnswer our_answer; // answer we create bit by bit.
			std::vector<std::function<void(TreeAnswer*)>> callbacks;
			bool finished = false;
			DateTime start = 0;
			DateTime finished_since = 0;
		};
		std::recursive_mutex m_mutex_incomplete_requests;
		std::unordered_map<ComputerId, TreeAnswerRequest> m_incomplete_requests;

		void register_listener();

		void fillTreeAnswer(TreeAnswer& answer, FsID elt_id, size_t depth, DateTime since);
	public:
		//factory
		[[nodiscard]] static std::shared_ptr<SynchTreeMessageManager> create(std::shared_ptr<ClusterManager> physical_server, std::shared_ptr<FsStorage> filesystem, std::shared_ptr<SynchroDb> syncro) {
			std::shared_ptr<SynchTreeMessageManager> pointer = std::shared_ptr<SynchTreeMessageManager>{ new SynchTreeMessageManager(/*TODO*/) };
			pointer->m_syncro = syncro;
			pointer->m_filesystem = filesystem;
			pointer->m_cluster_manager = physical_server;
			pointer->register_listener();
			return pointer;
		}

		std::shared_ptr<SynchTreeMessageManager> ptr() {
			return shared_from_this();
		}

		void receiveMessage(PeerPtr peer, uint8_t messageId, const ByteBuff& message) override;

		//future allow to block execution until there is a result.
		//callback avoid the need to fetch the result afterwards if it's not blocking.
		std::future<TreeAnswerPtr> fetchTree(/*ComputerId cid, */FsID root);
		void fetchTree(/*ComputerId cid, */FsID root, const std::function<void(TreeAnswerPtr)>& callback);
		SynchTreeMessageManager::TreeAnswerRequest& SynchTreeMessageManager::registerChunkReachableRequest(ComputerId cid);

		// GET/SEND_TREE
		ByteBuff writeTreeRequestMessage(const TreeRequest& request);
		TreeRequest readTreeRequestMessage(const ByteBuff& buffer);
		TreeAnswer answerTreeRequest(const PeerPtr sender, TreeRequest&& request);
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
