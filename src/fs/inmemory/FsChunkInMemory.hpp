#pragma once

#include "fs/base/FsChunk.hpp"
#include "utils/ByteBuff.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {
	class FsChunkInMemory : public FsChunk, public FsSerializableInMemory {
	protected:
		size_t m_size = 0;
		std::vector<uint8_t> m_buffer;
	public:
		FsChunkInMemory(FsID id, DateTime date, const uint8_t* data, uint64_t hash, size_t size);
		FsChunkInMemory(FsID id, DateTime date, uint64_t hash, size_t size);

		//data
		/**
		 * Read some bytes from this chunk.
		 * @param toAppend buffer where data are put (if the return value is true).
		 * @param offset from where to read.
		 * @param size size of things readed.
		 * @return true if possible. false if offset and size put us out of bounds.
		 */
		virtual bool read(uint8_t* to_append, size_t offset, size_t size) const override;

		static ByteBuff readAll(FsChunk& chunk);

		/**
		 * Write some bytes to this chunk.
		 * <br>If this chunk is a "last chunk", we can write more data than his size (up to a max size limit).
		 * @param toAppend buffer where data are read (if the return value is true).
		 * @param offset from where to write.
		 * @param size size of things to write.
		 * @return true if success. false if offset and size put us out of bounds.
		 */
		virtual bool write(ByteBuff& to_write, size_t offset, size_t size);

		static void serialize(FsChunkInMemory* to_serialize, ByteBuff& buffer);
		static std::shared_ptr<FsChunkInMemory> deserialize(FsID id, ByteBuff& buffer);


		virtual size_t size() const { return m_buffer.size(); }
		/**
		 * Set if the chunk is present localay or not. Used to remove it from the local storage.
		 * @param isPresentLocally if false, delete the local storage of this chunk
		 */
		virtual void setLocal(bool isPresentLocally) {}

		/**
		 *
		 * @return in ms
		 */
		virtual DateTime getLastAccessDate() { assert(false); return 0; }
	};

}
