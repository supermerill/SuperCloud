#pragma once

#include "FsChunkInMemory.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {

	FsChunkInMemory::FsChunkInMemory(FsID id, DateTime date, ByteBuff& data, uint64_t hash, size_t size) : FsChunk(id, date, hash), m_size(size) {
		assert(data.available() >= size);
		m_buffer.resize(size);
		std::copy(data.raw_array() + data.position(), data.raw_array() + data.position() + size, m_buffer.data());
		data.position(data.position() + size);
		m_is_local = true;
	}
	FsChunkInMemory::FsChunkInMemory(FsID id, DateTime date, uint64_t hash, size_t size) : FsChunk(id, date, hash) {
		m_is_local = false;
	}

	void FsChunkInMemory::addParent(FsID id) {
		if (!contains(m_parents, id)) {
			m_parents.push_back(id);
		}
	}

	bool FsChunkInMemory::read(ByteBuff& to_append, size_t offset, size_t size) const {
		if (!m_is_local) {
			throw std::exception("error, cannot read a chunk not local");
		}
		if (offset + size < m_buffer.size()) {
			to_append.put(m_buffer.data() + offset, size);
			return true;
		}
		return false;
	}

	ByteBuff FsChunkInMemory::readAll(FsChunk& chunk) {
		if (!chunk.isLocal()) {
			throw std::exception("error, cannot read a chunk not local");
		}
		ByteBuff buffer;
		buffer.put(((FsChunkInMemory*)&chunk)->m_buffer);
		return buffer.flip();

	}

	bool FsChunkInMemory::write(ByteBuff& to_write, size_t offset, size_t size) {
		if (offset + size < m_size) {
			if (offset + size > m_buffer.size()) {
				m_buffer.resize(offset + size);
			}
			std::copy(to_write.raw_array() + to_write.position(), to_write.raw_array() + to_write.position() + size, m_buffer.data() + offset);
			return true;
		}
		return false;
	}

	void FsChunkInMemory::serialize(FsChunkInMemory* t, ByteBuff& buffer) {
		buffer.putLong(t->m_creation_date);
		buffer.putSize(t->m_size);
		buffer.put(t->m_is_local ? 1 : 0);
		buffer.putULong(t->m_hash);
		buffer.putSize(t->m_parents.size());
		for (FsID id : t->m_parents) {
			buffer.putULong(id);
		}
		if (t->m_is_local) {
			assert(t->m_size == t->m_buffer.size());
			buffer.put(t->m_buffer);
		}
	}

	std::shared_ptr<FsChunkInMemory> FsChunkInMemory::deserialize(FsID id, ByteBuff& buffer) {
		DateTime creation_date = buffer.getLong();
		size_t size = buffer.getSize();
		bool is_local = buffer.get() == 1;
		uint64_t hash = buffer.getULong();
		size_t parents_count = buffer.getSize();
		std::vector<FsID> parents;
		for (size_t i = 0; i < parents_count; i++) {
			parents.push_back(buffer.getULong());
		}
		if (is_local) {
			std::shared_ptr<FsChunkInMemory> chunk = std::shared_ptr<FsChunkInMemory>{ 
				new FsChunkInMemory {id, creation_date, buffer, hash, size} };
			return chunk;
		} else {
			std::shared_ptr<FsChunkInMemory> chunk = std::shared_ptr<FsChunkInMemory>{
				new FsChunkInMemory {id, creation_date, hash, size} };
			return chunk;
		}
	}

}
