#pragma once

#include "utils/ByteBuff.hpp"
#include "FsElt.hpp"
#include "network/clusterManager.hpp"

namespace supercloud {
	/// <summary>
	/// A chunk is a piece of raw data. Contrary to file and directory who have "commits", a file chunk is immutable.
	/// As it's immutable, it can be shared by many files (mostly the same but renamed, or copies)
	/// most of its properties are stored in the file, so it depends of which parent you consider.
	/// </summary>
	class FsChunk : public FsElt {
	protected:
		//my unique parent.
		std::vector<FsID> m_parents;
	public:
		FsChunk(FsID id, DateTime date) :FsElt(id, date) {}

		/// my parents (file ids)
		std::vector<FsID> getParents() { return m_parents; }

		//data
		/**
		 * Read some bytes from this chunk.
		 * @param toAppend buffer where data are put (if the return value is true).
		 * @param offset from where to read.
		 * @param size size of things readed.
		 * @return true if possible. false if offset and size put us out of bounds.
		 */
		virtual bool read(ByteBuff& toAppend, size_t offset, size_t size) = 0;

		/**
		 * Write some bytes to this chunk.
		 * <br>If this chunk is a "last chunk", we can write more data than his size (up to a max size limit).
		 * @param toAppend buffer where data are read (if the return value is true).
		 * @param offset from where to write.
		 * @param size size of things to write.
		 * @return true if success. false if offset and size put us out of bounds.
		 */
		//virtual bool write(ByteBuff& toWrite, size_t offset, size_t size) = 0;


		//metadata
		/**
		 * Get the size of the chunk content. It can be >0 even if it's not stored locally.
		 * @return size in bytes.
		 */
		virtual size_t currentSize() = 0;
		//virtual void setCurrentSize(size_t newSize) = 0;
		/**
		 * Get the max size this chunk can grow. It's almost not used.
		 * @return the max size attribute
		 */
		//virtual size_t getMaxSize() = 0;
		//virtual void setMaxSize(size_t newMaxSize) = 0;
		/**
		 * Ask if the chunk is stored locally or not.
		 * @return true if it can be grab from the local storage.
		 */
		virtual bool isLocal() = 0;
		/**
		 * Set if the chunk is present localay or not. Used to remove it from the local storage.
		 * @param isPresentLocally if false, delete the local storage of this chunk
		 */
		virtual void setLocal(bool isPresentLocally) = 0;

		/**
		 *
		 * @return in ms
		 */
		virtual long getLastAccessDate() = 0;


	};

}
