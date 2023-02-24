#pragma once

#include "FsChunkLocal.hpp"
#include "FsStorageLocal.hpp"
#include "utils/ByteBuff.hpp"

#include <stdio.h>

namespace supercloud {

	FsChunkLocal::FsChunkLocal(FsID id, DateTime date, FsStorageLocal& database, size_t size, uint64_t hash) : FsChunk(id, date, hash) ,
		m_size(size), m_database(database), m_data_holder() {
	}

	std::filesystem::path FsChunkLocal::getDataHolder() {
		std::filesystem::path path = m_data_holder?m_data_holder->computePathCommitId(m_data_commit):"";
		bool okay = m_data_holder && std::filesystem::exists(path);
		size_t idx = 0;
		while (!okay) {
			if (idx >= m_parents.size()) {
				throw std::exception((std::string("Error, can't fetch a good parent for chunk ") + std::to_string(m_id)).c_str());
			}
			m_data_holder = std::static_pointer_cast<FsFileLocal>(m_database.load(m_parents[idx]));
			m_data_offset = m_parent_offset[idx];
			m_data_commit = m_parent_commit[idx];
			path = m_data_holder->computePathCommitId(m_data_commit);
			okay = m_data_holder && std::filesystem::exists(path);
			++idx;
		}
		return path;
	}

	void FsChunkLocal::addParent(FsID id, FsID commit, size_t offset) {
		if (!contains(m_parents, id)) {
			m_parents.push_back(id);
			m_parent_commit.push_back(commit);
			m_parent_offset.push_back(offset);
		}
	}

	size_t FsChunkLocal::getOffset(FsID parent, FsID commit) const {
		assert(m_parents.size() == m_parent_commit.size());
		assert(m_parents.size() == m_parent_offset.size());
		for (size_t idx = 0; idx < m_parents.size(); ++idx) {
			if (m_parents[idx] == parent && m_parent_commit[idx] == commit) {
				return m_parent_offset[idx];
			}
		}
		throw std::exception((std::string("Error: cannot find chunk for file ") + std::to_string(parent) + std::string(" and commit ") + std::to_string(commit)).c_str());
	}

	bool FsChunkLocal::read(ByteBuff& to_append, size_t offset, size_t size) {
		std::filesystem::path file_path = getDataHolder();
		//TODO: use fseek64 in 32bit-based system like windows.
		FILE* file = fopen(file_path.string().c_str(), "r");
		int result = fseek(file, this->m_data_offset + offset, SEEK_SET);
		assert(0 == result);
		to_append.expand(size);
		size_t size_read = fread(to_append.raw_array() + to_append.position(), sizeof(uint8_t), size, file);
		assert(size_read == size);
		fclose(file);
		to_append.position(to_append.position() + size_read);
		return (size_read == size);
	}

	ByteBuff FsChunkLocal::readAll() {
		std::filesystem::path file_path = getDataHolder();
		//TODO: use fseek64 in 32bit-based system like windows.
		FILE* file = fopen(file_path.string().c_str(), "r");
		int result = fseek(file, this->m_data_offset, SEEK_SET);
		assert(0 == result);
		ByteBuff buffer{ this->m_size };
		size_t size_read = fread(buffer.raw_array(), sizeof(uint8_t), this->m_size, file);
		assert(size_read == this->m_size);
		fclose(file);
		buffer.limit(this->m_size); 
		return buffer;
	}

	bool FsChunkLocal::write(ByteBuff& to_write, size_t offset, size_t size) {
		return false;
	}

	void FsChunkLocal::serialize(FsChunkLocal* t, ByteBuff& buffer) {
		std::cout << "CHUNK id=" << t->getId() << "@" << buffer.position() << "\n";
		buffer.putLong(t->m_creation_date);
		buffer.putSize(t->m_size);
		buffer.putSize(t->m_parents.size());
		for (size_t idx = 0; idx < t->m_parents.size(); ++idx) {
			buffer.putULong(t->m_parents[idx]);
			buffer.putULong(t->m_parent_commit[idx]);
			buffer.putSize(t->m_parent_offset[idx]);
		}
	}

	std::shared_ptr<FsChunkLocal> FsChunkLocal::deserialize(FsID id, ByteBuff& buffer, FsStorageLocal& database) {
		std::cout << "id=" << id << "@" << buffer.position() << "\n";
		DateTime creation_date = buffer.getLong();
		size_t size = buffer.getSize();
		std::shared_ptr<FsChunkLocal> chunk = std::shared_ptr<FsChunkLocal>{ new FsChunkLocal {id, creation_date, database, size} };
		size_t parents_count = buffer.getSize();
		for (size_t i = 0; i < parents_count; i++) {
			chunk->m_parents.push_back(buffer.getULong());
			chunk->m_parent_commit.push_back(buffer.getULong());
			chunk->m_parent_offset.push_back(buffer.getSize());
		}
		std::cout << "t->m_buffer.size=" << size << "@" << buffer.position() << "\n";
		return chunk;
	}

}
