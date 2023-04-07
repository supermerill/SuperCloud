#pragma once

#include "FsElt.hpp"
#include "FsStorage.hpp"

#include <mutex>
#include <vector>

namespace supercloud {

	struct FsObjectCommit {
		//id of the commit. Can be the id of a FsElt, if it's for its creation
		FsID id;
		//date of the commit. It's a local date: when this commit has been added to our filesystem.
		DateTime date;

		/// <summary>
		/// commit changes.
		/// if 0->id, then it's added at the back of the file
		/// if id->id, then a thing has been replaced
		/// if id->0, then a thing has been deleted.
		/// </summary>
		std::vector<std::pair<FsID, FsID>> changes;

		bool operator==(const FsObjectCommit& other) const {
			return id == other.id && date == other.date && changes == other.changes;
		}
	};

	/// immutable object of a group of things (folder, files or chunks) in the filesystem
	// interface for a folder or a file.
	class FsObject : public FsElt {
	protected:
		//////  -- immutable: name, path, rights -- //////
		const CUGA m_puga;
		const std::string m_name;
		// the id of the group of this object
		const uint32_t m_group_id;
		//my unique parent.
		const FsID m_parent;


		//////  -- mutable: content, deleted -- //////

		/// <summary>
		/// cache over the commits. Applying each commit must create this vector.
		/// </summary>
		std::vector<FsID> m_current_state;
		// commits from the beginning.
		// these can be fusionned, to reduce the clutter if there is too many old ones. The id of the fusionned one is the id of the recent one, to ge thte same state with the id.
		std::vector<FsObjectCommit> m_commits;

		// if currently deleted, the date of when it happened. else 0
		DateTime m_date_deleted = 0;
		//if renamed, or have its rights changed (and not a real deletion).
		FsID m_renamed_to = 0;
		const FsID m_renamed_from; //TODO


		// if I have to load commits, or chunks. To make other threads to wait the one that load has finished.
		//mutable std::mutex m_modify_load_mutex; // note: maybe use a higher order mutex. ie only one thread can modify/read the local fs?


		void addThing(FsID new_thing_id, DateTime time) {
			{
				//std::lock_guard lock{ m_modify_load_mutex };
				this->m_commits.emplace_back();
				this->m_commits.back().id = new_thing_id;
				this->m_commits.back().date = time;
				this->m_commits.back().changes.push_back({ 0,new_thing_id });
				this->m_current_state.push_back(new_thing_id);
			}
		}
		void replaceContent(const std::vector<FsID>& new_content, const FsObjectCommit& commit) {
			{
				//std::lock_guard lock{ m_modify_load_mutex };
				this->m_commits.emplace_back();
				this->m_commits.back().id = commit.id;
				this->m_commits.back().date = commit.date;
				this->m_commits.back().changes = commit.changes;
				this->m_current_state = new_content;
			}
		}
	public:
		FsObject(FsID id, DateTime date_creation, std::string name, CUGA puga, FsID parent)
			: FsElt(id, date_creation), m_name(name), m_puga(puga), m_parent(parent), m_group_id(0) /*TODO*/, m_renamed_from(0) { assert(FsElt::isObject(id));}
		FsObject(FsID id, DateTime date_creation, std::string name, CUGA puga, FsID parent, FsID renamed_from)
			: FsElt(id, date_creation), m_name(name), m_puga(puga), m_parent(parent), m_group_id(0) /*TODO*/, m_renamed_from(renamed_from) { assert(FsElt::isObject(id));}

		CUGA getCUGA() const { return m_puga; }
		std::string getName() const { return m_name; }
		uint32_t getGroupId() const { return m_group_id; }
		/// my parent (a file for a chunk, a directory for a file or directory.
		FsID getParent() const { return m_parent; }

		DateTime getDeletedDate() const { return m_date_deleted; }
		FsID getRenamedTo() const { return m_renamed_to; }
		FsID getRenamedFrom() const { return m_renamed_from; }

		size_t getCommitsSize() const { return m_commits.size(); }
		const FsObjectCommit& getCommit(size_t idx) const { return m_commits[idx]; }
		const FsObjectCommit& backCommit() const { return m_commits.back(); }

		/// <summary>
		/// Depth of the object in the filesystem tree.
		/// 0 is the root directory.
		/// </summary>
		/// <returns></returns>
		virtual uint16_t getDepth() const = 0;

		/// <summary>
		/// size in bytes for a file.
		/// theses are only for file, but crrently it's easier to also do the same thing for all objects.
		/// can be relmoved from here with many more work.
		/// </summary>
		/// <returns></returns>
		virtual size_t size() const { return m_current_state.size(); }
		virtual std::vector<size_t> sizes() const { return m_current_state; /*easy placeholder for dirs.*/ }

		//TODO: rework that method, maybe put it only in directory?
		virtual std::tuple<FsID, DateTime> getLastModification() const { return {}; } //FIXME

		virtual std::vector<FsID> getCurrent() const { return m_current_state; }
	};
}
