#pragma once

#include "FsObject.hpp"

namespace supercloud {

	class FsDirectoryFactory;
	
	class FsDirectory : public FsObject {
	protected:
		// because a modification is possible inside a file or a dir inside this one.
		const FsID m_last_modification_commit_id; // mutable in the lifecycle
		const DateTime m_last_modification_date; // mutable in the lifecycle

	public:
		FsDirectory(FsID id, DateTime date_creation, const std::string& name, CUGA puga, uint32_t group_id, FsID parent, uint16_t depth, FsID renamed_from, //const data
			const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, FsID last_modification_commit_id, DateTime last_modification_date) //mutable data
			: FsObject(id, date_creation, name, puga, group_id, parent, depth, renamed_from,
				current_state, commits, date_deleted, renamed_to), m_last_modification_commit_id(last_modification_commit_id), m_last_modification_date(last_modification_date) {
			assert(FsElt::isDirectory(id));
		}
		// update an old object
		FsDirectory(const FsDirectory& old_dir, //const data
			const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, FsID last_modification_commit_id, DateTime last_modification_date) //mutable data
			: FsObject(old_dir,
				current_state, commits, date_deleted, renamed_to), m_last_modification_commit_id(last_modification_commit_id), m_last_modification_date(last_modification_date) {}

		virtual bool isFile() const override { return false; }
		virtual bool isDir() const override { return true; }

		virtual std::tuple<FsID, DateTime> getLastModification() const override {
			return { this->m_last_modification_commit_id, this->m_last_modification_date };
		}

		static inline FsDirPtr refresh(const FsDirPtr& src) {
			return src->isObsolete() ? std::static_pointer_cast<FsDirectory>(src->force_refresh()) : src;
		}

		friend FsDirectoryFactory;
	};

	class FsDirectoryFactory : public FsObjectFactory {
	public:
		//TODO these two things are computation-intensive. Please ensure it's really useful.
		FsID last_modification_commit_id;
		DateTime last_modification_date;

		FsDirectoryFactory(const FsDirectory& pattern) : FsObjectFactory(pattern),
			last_modification_commit_id(pattern.m_last_modification_commit_id),
			last_modification_date(pattern.m_last_modification_commit_id) {}

		void notifyModification(FsID last_commit_id, DateTime date) {
			{
				if (this->last_modification_date < date) {
					this->last_modification_date = date;
					this->last_modification_commit_id = last_commit_id;
				}
			}
		}
	};

	// a stub can modify the mutable fields, as it's not shared.
	class FsDirectoryStub : public FsDirectory {
	public:
		FsDirectoryStub(FsID id, DateTime date, std::string name, CUGA puga, uint32_t group, FsID parent, uint16_t depth, FsID old_deleted_object = 0)
			: FsDirectory(id, date, name, puga, group, parent, depth, old_deleted_object,
				{}, {}, 0, 0, 0, 0) {}
		virtual std::tuple<FsID, DateTime> getLastModification() { return {}; }
		std::vector<FsObjectCommit>& commits() { return m_commits; }
		std::vector<FsID>& current() { return m_current_state; }
		/// <summary>
		/// Not used, not implemnted
		/// </summary>
		/// <returns></returns>
		virtual uint16_t getDepth() const override { assert(false); return 0; };
	};
}
