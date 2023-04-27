#pragma once

#include "FsObject.hpp"
#include "FsChunk.hpp"
#include "FsDirectory.hpp"

#include <numeric>

namespace supercloud {
    class FsFileFactory;

    // TODO: currently, the file size is stored as a "cached value of the size of all current chunks".
    //          Is it interresting to store the size of each current chunk?
	class FsFile : public FsObject {
	protected:
        // in bytes
        // same number of elt than m_current_state
        std::vector<size_t> m_current_size; // mutable in the lifecycle (and in stubs modtly)
        // should be the sum of the m_current_size (it's a cache)
        size_t m_size; // mutable in the lifecycle (and in stubs modtly)

	public:
        FsFile(FsID id, DateTime date_creation, const std::string& name, CUGA puga, uint32_t group_id, FsID parent, uint16_t depth, FsID renamed_from, //const data
            const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, const std::vector<size_t>& current_size) //mutable data
            : FsObject(id, date_creation, name, puga, group_id, parent, depth, renamed_from,
            current_state, commits, date_deleted, renamed_to), m_current_size(current_size), m_size(std::accumulate(current_size.begin(), current_size.end(), size_t(0))){
            assert(FsElt::isFile(id));
        }
        // update an old object
        FsFile(const FsFile& old_file, //const data
            const std::vector<FsID>& current_state, const std::vector<FsObjectCommit>& commits, DateTime date_deleted, FsID renamed_to, const std::vector<size_t>& current_size) //mutable data
            : FsObject(old_file,
            current_state, commits, date_deleted, renamed_to), m_current_size(current_size), m_size(std::accumulate(current_size.begin(), current_size.end(), size_t(0))) {}

        virtual bool isFile() const override { return true; }
        virtual bool isDir() const override { return false; }

        static inline FsFilePtr refresh(const FsFilePtr& src) {
            return src->isObsolete() ? std::static_pointer_cast<FsFile>(src->force_refresh()) : src;
        }


        virtual size_t size() const override {
            assert(m_size == std::accumulate(m_current_size.begin(), m_current_size.end(), size_t(0)));
            return m_size;
        }

        virtual const std::vector<size_t>& sizes() const override {
            assert(m_current_size.size() == m_current_state.size());
            return m_current_size;
        }

        friend FsFileFactory;
	};

    class FsFileFactory : public FsObjectFactory {
    public:
        std::vector<size_t> current_size;

        FsFileFactory(const FsFile& pattern) : FsObjectFactory(pattern),
            current_size(pattern.m_current_size) {}

        void replaceChunk(FsID old_chunk_id, FsID new_chunk_id, size_t new_size, DateTime time) {
            bool found = false;
            {
                assert(this->current_size.size() == current_state.size());
                //std::lock_guard lock{ m_modify_load_mutex };
                auto it = std::find(this->current_state.begin(), this->current_state.end(), old_chunk_id);
                if (it != this->current_state.end()) {
                    found = true;
                    this->commits.emplace_back();
                    this->commits.back().id = new_chunk_id;
                    this->commits.back().time = time;
                    *it = new_chunk_id;
                    this->current_size[it - this->current_state.begin()] = new_size;
                    this->commits.back().changes.push_back({ old_chunk_id, new_chunk_id });
                }
            }
        }
    };

    // a stub can modify the mutable fields, as it's not shared.
    class FsFileStub : public FsFile {
    public:
        FsFileStub(FsID id, DateTime date, std::string name, CUGA puga, uint32_t group, FsID parent, uint16_t depth, FsID old_deleted_object = 0) :
            FsFile(id, date, name, puga, group, parent, depth, old_deleted_object,
                {}, {}, 0, 0, {}) {}
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
