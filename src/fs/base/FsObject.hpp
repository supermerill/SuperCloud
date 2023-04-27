#pragma once

#include "FsElt.hpp"
#include "FsStorage.hpp"

#include <mutex>
#include <vector>

namespace supercloud {

	class FsObjectFactory;

	struct FsObjectCommit {
		//id of the commit. Can be the id of a FsElt, if it's for its creation
		FsCommitID id;
		//date of the commit. It's a local date: when this commit has been added to our filesystem.
		DateTime time;

		/// <summary>
		/// commit changes.
		/// if 0->id, then it's added at the back of the file
		/// if id->id, then a thing has been replaced
		/// if id->0, then a thing has been deleted.
		/// </summary>
		std::vector<std::pair<FsID, FsID>> changes;

		bool operator==(const FsObjectCommit& other) const {
			return id == other.id && time == other.time && changes == other.changes;
		}
	};

	/// immutable object of a group of things (folder, files or chunks) in the filesystem
	// interface for a folder or a file.
	// note: modifiable things can be modified in threads. There is nothing to take care of clients iterating over list and those who write over them.
	//   => please use this as completly immutable. IF someone want to write over it, he has to use the update() method and then change the ptr inside its database.
	// It's a bit cumbersome and sub-obtimal but it should ensure complete thread safety.
	// using a simple mutex for modifying the database/modifying objects from database should do the trick, but there is still some issues that can comes from deadlock (unlikely)
	//	and as many thread may keep a ptr for read-only purpose, it's mandatory to not modify the vector in the middle of a iteration.
	// mutable fields are not defined as const but should be treated as if. It's for stubs and local-only subclasses that it's kept without the const keyword.
	class FsObject : public FsElt {
	protected:
		//////  -- immutable: name, path, rights -- //////
		const CUGA m_puga;
		const std::string m_name;
		// the id of the group of this object
		const uint32_t m_group_id;
		//my unique parent.
		const FsID m_parent;
		const uint16_t m_depth;


		//////  -- mutable: content, deleted -- //////

		/// <summary>
		/// cache over the commits. Applying each commit must create this vector.
		/// </summary>
		std::vector<FsID> m_current_state; // mutable in the lifecycle (and in stubs modtly)
		// commits from the beginning.
		// these can be fusionned, to reduce the clutter if there is too many old ones. The id of the fusionned one is the id of the recent one, to ge thte same state with the id.
		std::vector<FsObjectCommit> m_commits; // mutable in the lifecycle (and in stubs modtly)

		// if currently deleted, the date of when it happened. else 0
		DateTime m_time_deleted = 0; // mutable in the lifecycle (and in stubs modtly)
		//if renamed, or have its rights changed (and not a real deletion).
		FsID m_renamed_to = 0; // mutable in the lifecycle (and in stubs modtly)
		const FsID m_renamed_from; //TODO


		// if I have to load commits, or chunks. To make other threads to wait the one that load has finished.
		//mutable std::mutex m_modify_load_mutex; // note: maybe use a higher order mutex. ie only one thread can modify/read the local fs?
		// ptr to the new version of this object (it has been modified by a thread). Can be set only one time and can't be removed (the static mutex takes care of that).
		FsObjectPtr m_new_version;
		static inline std::mutex m_new_version_mutex;

		FsObject(FsID id, DateTime date_creation, const std::string& name, CUGA puga, uint32_t group_id, FsID parent, uint16_t depth, FsID renamed_from, //const data
			const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to) //mutable data
			: FsElt(id, date_creation), m_name(name), m_puga(puga), m_group_id(group_id), m_parent(parent), m_renamed_from(renamed_from),
			m_current_state(current_state), m_commits(commits), m_time_deleted(date_deleted), m_depth(depth), m_renamed_to(renamed_to) {
			assert(FsElt::isObject(id));
		}
		// update an old object
		FsObject(const FsObject& old_object, //const data
			const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to) //mutable data
			: FsElt(old_object.m_id, old_object.m_creation_time), m_name(old_object.m_name), m_puga(old_object.m_puga), m_group_id(old_object.m_group_id),
			m_parent(old_object.m_parent), m_depth(old_object.m_depth), m_renamed_from(old_object.m_renamed_from),
			m_current_state(current_state), m_commits(commits), m_time_deleted(date_deleted), m_renamed_to(renamed_to) {}
	public:
		virtual bool isChunk() const override { return false; }
		virtual bool isFile() const override { return FsElt::isFile(m_id); }
		virtual bool isDir() const override { return FsElt::isDirectory(m_id); }

		CUGA getCUGA() const { return m_puga; }
		std::string getName() const { return m_name; }
		uint32_t getGroupId() const { return m_group_id; }
		/// my parent (a file for a chunk, a directory for a file or directory.
		FsID getParent() const { return m_parent; }

		DateTime getDeletedDate() const { return m_time_deleted; }
		FsID getRenamedTo() const { return m_renamed_to; }
		FsID getRenamedFrom() const { return m_renamed_from; }

		const std::vector<FsObjectCommit>& getCommits() const { return m_commits; }

		/// <summary>
		/// Depth of the object in the filesystem tree.
		/// 0 is the root directory.
		/// </summary>
		/// <returns></returns>
		virtual uint16_t getDepth() const { return m_depth; }

		/// <summary>
		/// size in bytes for a file.
		/// theses are only for file, but crrently it's easier to also do the same thing for all objects.
		/// can be relmoved from here with many more work.
		/// </summary>
		/// <returns></returns>
		virtual size_t size() const { return m_current_state.size(); }
		virtual const std::vector<size_t>& sizes() const { return m_current_state; /*easy placeholder for dirs.*/ }

		/// <summary>
		/// last modification for this object or inside the childs of this object.
		/// </summary>
		/// <returns> [id of the commit, date of modification]</returns>
		virtual std::tuple<FsID, DateTime> getLastModification() const {
			if (m_commits.empty()) {
				return { m_id, this->m_creation_time };
			}
			return { m_commits.back().id, m_commits.back().time };
		}

		virtual const std::vector<FsID>& getCurrent() const { return m_current_state; }

		bool isObsolete() const { return (bool)m_new_version; }
		/// <summary>
		/// Return the pointer to the new current object of this.
		/// If called while not isObsolete(), return an empty pointer.
		/// An fsObject can't loose its isObsolete() tag.
		/// </summary>
		/// <returns></returns>
		FsObjectPtr force_refresh() const {
			assert(m_new_version);
			FsObjectPtr refreshed_version = m_new_version;
			if (!refreshed_version) {
				return refreshed_version;
			}
			while (refreshed_version->m_new_version) {
				refreshed_version = refreshed_version->m_new_version;
			}
			return refreshed_version;
		}
		/// <summary>
		/// Return the last good version of the object 'src' (can be 'src' if not obsolete).
		/// Note that someone may modify it just right after the return of this call.
		/// </summary>
		/// <param name="src"></param>
		/// <returns></returns>
		static inline FsObjectPtr refresh(const FsObjectPtr& src) {
			return src->isObsolete() ? src->force_refresh() : src;
		}
		/// <summary>
		/// Try to 'modify' this object concurently, after checking that this object is not obsolete.
		/// It only allow other thread to fetch the change as this object is now obsolete.
		/// You have to also update the repositories where this object lies.
		/// </summary>
		/// <param name="new_version">The new version to set.</param>
		/// <returns>true if the new version is set. False if this object is already obsolete.</returns>
		bool modify(FsObjectPtr new_version) {
			bool is_obsolete = isObsolete();
			if(!is_obsolete){
				std::lock_guard lock{ m_new_version_mutex };
				is_obsolete = (bool)m_new_version;
				if (!is_obsolete) {
					m_new_version = new_version;
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Force the modification of this object, erasing all other modifications that has happened in-between.
		/// </summary>
		/// <param name="new_version"></param>
		/// <returns>true if it had to refresh (force)</returns>
		bool force_modify(FsObjectPtr new_version) {
			bool force = !modify(new_version);
			if (force) {
				bool retry = force;
				FsObjectPtr refreshed = this->force_refresh();
				retry = refreshed->modify(new_version);
				while (retry) {
					refreshed = refreshed->force_refresh();
					retry = refreshed->modify(new_version);
				}
			}
			return force;
		}

		friend FsObjectFactory;
	};

	class FsObjectFactory {
	public:
		std::vector<FsID> current_state;
		std::vector<FsObjectCommit> commits;
		DateTime date_deleted;
		FsID renamed_to;
		FsObjectFactory(const FsObject& pattern) :
			current_state(pattern.m_current_state),
			commits(pattern.m_commits),
			date_deleted(pattern.m_time_deleted),
			renamed_to(pattern.m_renamed_to) {}

		void addThing(FsID new_thing_id, DateTime time) {
			{
				//std::lock_guard lock{ m_modify_load_mutex };
				this->commits.emplace_back();
				this->commits.back().id = new_thing_id;
				this->commits.back().time = time;
				this->commits.back().changes.push_back({ 0,new_thing_id });
				this->current_state.push_back(new_thing_id);
			}
		}
		void replaceContent(const std::vector<FsID>& new_content, const FsObjectCommit& commit) {
			{
				//std::lock_guard lock{ m_modify_load_mutex };
				this->commits.emplace_back();
				this->commits.back().id = commit.id;
				this->commits.back().time = commit.time;
				this->commits.back().changes = commit.changes;
				this->current_state = new_content;
			}
		}
	};
}
