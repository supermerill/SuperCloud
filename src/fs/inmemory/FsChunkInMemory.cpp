#pragma once

#include "FsChunkInMemory.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {

	FsChunkInMemory::FsChunkInMemory(FsID id, DateTime date, ByteBuff& data, size_t size) : FsChunk(id, date) {
		assert(data.available() >= size);
		m_buffer.resize(size);
		std::copy(data.raw_array() + data.position(), data.raw_array() + data.position() + size, m_buffer.data());
		data.position(data.position() + size);
	}

	bool FsChunkInMemory::read(ByteBuff& to_append, size_t offset, size_t size) {
		if (offset + size < m_buffer.size()) {
			to_append.put(m_buffer.data() + offset, size);
			return true;
		}
		return false;
	}

	ByteBuff FsChunkInMemory::readAll(FsChunk& chunk) {
		ByteBuff buffer;
		buffer.put(((FsChunkInMemory*)&chunk)->m_buffer);
		return buffer.flip();

	}

	bool FsChunkInMemory::write(ByteBuff& to_write, size_t offset, size_t size) {
		if (offset + size < m_max_size) {
			if (offset + size > m_buffer.size()) {
				m_buffer.resize(offset + size);
			}
			std::copy(to_write.raw_array() + to_write.position(), to_write.raw_array() + to_write.position() + size, m_buffer.data() + offset);
			return true;
		}
		return false;
	}

	void FsChunkInMemory::serialize(FsChunkInMemory* t, ByteBuff& buffer) {
		std::cout << "CHUNK id=" << t->getId() << "@" << buffer.position() << "\n";
		buffer.putLong(t->m_creation_date);
		buffer.putSize(t->m_max_size);
		buffer.putSize(t->m_parents.size());
		for (FsID id : t->m_parents) {
			buffer.putULong(id);
		}
		buffer.putSize(t->m_buffer.size());
		std::cout << "t->m_buffer.size=" << t->m_buffer.size() << "@" << buffer.position() << "\n";
		buffer.put(t->m_buffer);
	}

	std::shared_ptr<FsChunkInMemory> FsChunkInMemory::deserialize(FsID id, ByteBuff& buffer) {
		std::cout << "id=" << id << "@" << buffer.position() << "\n";
		DateTime creation_date = buffer.getLong();
		size_t max_size = buffer.getSize();
		size_t parents_count = buffer.getSize();
		std::vector<FsID> parents;
		for (size_t i = 0; i < parents_count; i++) {
			parents.push_back(buffer.getULong());
		}
		size_t data_size = buffer.getSize();
		std::cout << "t->m_buffer.size=" << data_size << "@" << buffer.position() << "\n";
		std::shared_ptr<FsChunkInMemory> chunk = std::shared_ptr<FsChunkInMemory>{ new FsChunkInMemory {id, creation_date, buffer, data_size} };
		return chunk;
	}

}
