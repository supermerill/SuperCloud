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
		//my parents... maybe not necessary. Can be used as counter for destruction. The managment can be tedious?
		std::vector<FsID> m_parents;
		// a hash of the data, to check for integrity (xor each 64b of the data). xor the remaining bytes in the first byte.
		const uint64_t m_hash;
		bool m_is_local = false;
	public:
		FsChunk(FsID id, DateTime date, uint64_t hash) :FsElt(id, date), m_hash(hash) {}

		/// <summary>
		/// use the hash to see if the data we have are in sync with it.
		/// This impl read the data in a buffer, so you may want to reimplement it if you have a better way to compute a hash, at least to avoid a copy.
		/// </summary>
		/// <returns></returns>
		virtual bool checkIntegrity() {
			ByteBuff buff;
			this->read(buff, 0, size());
			uint64_t hash = compute_naive_hash(buff.raw_array(), buff.limit());
			return hash == m_hash;
		}

		uint64_t getHash() const { return m_hash; }

		/// my parents (file ids)
		std::vector<FsID> getParents() { return m_parents; }
		// add/remove parent?

		//data
		/**
		 * Read some bytes from this chunk. The data is just put inside the buffer.
		 * @param toAppend buffer where data are put (if the return value is true).
		 * @param offset from where to read.
		 * @param size size of things readed.
		 * @return true if possible. false if offset and size put us out of bounds.
		 */
		virtual bool read(ByteBuff& toAppend, size_t offset, size_t size) const = 0;

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
		virtual size_t size() const = 0;
		/**
		 * Ask if the chunk is stored locally or not.
		 * @return true if it can be grab from the local storage.
		 */
		virtual bool isLocal() { return m_is_local; }
		/**
		 * Set if the chunk is present localay or not. Used to remove it from the local storage.
		 * @param isPresentLocally if false, delete the local storage of this chunk
		 */
		//virtual void setLocal(bool is_present_locally) { m_is_local = is_present_locally; }

		/**
		 *
		 * @return in ms
		 */
		virtual DateTime getLastAccessDate() = 0;


	};

	class FsChunkStub : public FsChunk {
		FsChunkStub(FsID id, DateTime date, uint64_t hash) : FsChunk(id, date, hash) { m_is_local = false; }

		virtual bool read(ByteBuff& toAppend, size_t offset, size_t size) const override {};
		virtual size_t size() { return 0; }
		virtual DateTime getLastAccessDate() { return getDate(); }
	};

}
