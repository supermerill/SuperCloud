#pragma once

#include "FsElt.hpp"
#include "FsStorage.hpp"

#include <mutex>
#include <vector>

namespace supercloud {


	/// immutable object of a group of things (folder, files or chunks) in the filesystem
	// interface for a folder or a file.
	class FsObject : public FsElt {
	public:
		struct Commit {
			//id of the commit. Can be the id of a chunk, if there is a new one.
			FsID id;
			//date of the commit. Same as the chunk's one if the id is a chunk id.
			DateTime date;

			/// <summary>
			/// commit changes.
			/// if 0->id, then it's added at the back of the file
			/// if id->id, then a thing has been replaced
			/// if id->0, then a thing has been deleted.
			/// </summary>
			std::vector<std::pair<FsID, FsID>> changes;
		};
	protected:
		const CUGA m_puga;
		const std::string m_name;

		// if I have to laod commits, or chunks. TO make other threads to wait the one that load has finished.
		std::mutex m_modify_load_mutex;

		std::vector<FsID> m_current_state;
		// commits from the beginning.
		// these can be fusionned, to reduce the clutter if there is too many old ones. The id of the fusionned one is the id of the recent one, to ge thte same state with the id.
		std::vector<Commit> m_commits;

		// if currently deleted, the date of when it happened. else 0
		DateTime m_date_deleted = 0;
		//if renamed, or have its rights changed (and not a real deletion).
		FsID m_renamed_to = 0;

		//my unique parent.
		const FsID m_parent;


		void addThing(FsID new_thing_id, DateTime time) {
			{
				std::lock_guard lock{ m_modify_load_mutex };
				this->m_commits.emplace_back();
				this->m_commits.back().id = new_thing_id;
				this->m_commits.back().date = time;
				this->m_commits.back().changes.push_back({ 0,new_thing_id });
				this->m_current_state.push_back(new_thing_id);
			}
		}
		void replaceContent(const std::vector<FsID>& new_content, const std::vector<std::pair<FsID, FsID>>& commit, FsID commit_id, DateTime commit_time) {
			{
				std::lock_guard lock{ m_modify_load_mutex };
				this->m_commits.emplace_back();
				this->m_commits.back().id = commit_id;
				this->m_commits.back().date = commit_time;
				this->m_commits.back().changes = commit;
				this->m_current_state = new_content;
			}
		}
	public:
		FsObject(FsID id, DateTime date, std::string name, CUGA puga, FsID parent) :FsElt(id, date), m_name(name), m_puga(puga), m_parent(parent) {}

		CUGA getPUGA() { return m_puga; }
		std::string getName() { return m_name; }
		/// my parent (a file for a chunk, a directory for a file or directory.
		FsID getParent() { return m_parent; }

		virtual std::tuple<FsID, DateTime> getLastModification() = 0;

		virtual std::vector<FsID> getCurrent() { return m_current_state; }
	};
}
