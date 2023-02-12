#pragma once

#include "fs/base/FsChunk.hpp"
#include "utils/ByteBuff.hpp"
#include "FsStorageInMemory.hpp"

namespace supercloud {
	class FsChunkInMemory : public FsChunk, public FsSerializableInMemory {
	protected:
		size_t m_max_size = 0;
		std::vector<uint8_t> m_buffer;
	public:
		FsChunkInMemory(FsID id, DateTime date, ByteBuff& data, size_t size);

		//data
		/**
		 * Read some bytes from this chunk.
		 * @param toAppend buffer where data are put (if the return value is true).
		 * @param offset from where to read.
		 * @param size size of things readed.
		 * @return true if possible. false if offset and size put us out of bounds.
		 */
		virtual bool read(ByteBuff& to_append, size_t offset, size_t size) override;

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

		//metadata
		/**
		 * Get the size of the chunk content. It can be >0 even if it's not stored locally.
		 * @return size in bytes.
		 */
		virtual size_t currentSize() { return m_buffer.size(); }
		//virtual void setCurrentSize(size_t new_size) { m_buffer.resize(new_size); }
		/**
		 * Get the max size this chunk can grow. It's almost not used.
		 * @return the max size attribute
		 */
		//virtual size_t getMaxSize() { return m_max_size; }
		//virtual void setMaxSize(size_t new_max_size) { assert(new_max_size >= currentSize()); m_max_size = new_max_size; }
		/**
		 * Ask if the chunk is stored locally or not.
		 * @return true if it can be grab from the local storage.
		 */
		virtual bool isLocal() { return true; }
		/**
		 * Set if the chunk is present localay or not. Used to remove it from the local storage.
		 * @param isPresentLocally if false, delete the local storage of this chunk
		 */
		virtual void setLocal(bool isPresentLocally) {}

		/**
		 *
		 * @return in ms
		 */
		virtual long getLastAccessDate() { return 0; }
		/**
 * Visitor function.
 * @param visitor the visitor who visit this element.
 */
		virtual void accept(void* visitor) {}

		/**
		 * Perform deletion operation : delete sub-thing, delete content, delete entry in fs.
		 * It's final and irrecoverable. If you want just mark this as "deleted", use removeDir/removeFile/removeChunk instead.
		 */
		virtual void remove() {}

		/**
		 * Call this when a changes occur on this element. It updates the ModifyDate.
		 */
		virtual void changes() {}

		/**
		 * Notify that modifications on this object are finished and now should be saved / transmitted.
		 */
		virtual void flush() {}
	};

}
