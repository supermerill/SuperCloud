#pragma once

#include "FsObject.hpp"

namespace supercloud {
	
	class FsDirectory : public FsObject {
	protected:
		DateTime m_last_modification_date;
		FsID m_last_modification_commit_id;

		void notifyModification(FsID last_commit_id, DateTime date) {
			{
				//std::lock_guard lock{ this->m_modify_load_mutex };
				if (this->m_last_modification_date < date) {
					this->m_last_modification_date = date;
					this->m_last_modification_commit_id = last_commit_id;
				}
			}
		}
	public:
		FsDirectory(FsID id, DateTime date, std::string name, CUGA puga, FsID parent) : FsObject(id, date, name, puga, parent) {}
		virtual std::tuple<FsID, DateTime> getLastModification() const override {
			return { this->m_last_modification_commit_id, this->m_last_modification_date };
		}
	};

	class FsDirectoryStub : public FsDirectory {
		FsDirectoryStub(FsID id, DateTime date, std::string name, CUGA puga, FsID parent) : FsDirectory(id, date, name, puga, parent) {}
		virtual std::tuple<FsID, DateTime> getLastModification() { return {}; }
		std::vector<Commit>& commits() { return m_commits; }
		std::vector<FsID>& current() { return m_current_state; }
	};
}
