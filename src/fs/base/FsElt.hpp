#pragma once

#include "FsStorage.hpp"

#include <cstdint>
#include <memory>

namespace supercloud {
	enum class FSType : uint8_t {
		NONE = 0, //used for commit when it's not a creation
		CHUNK = 1,
		FILE = 2,
		DIRECTORY = 3
	};
	constexpr uint64_t NO_TYPE_MASK = uint64_t(-4);
	constexpr uint64_t SEED_MASK = uint64_t(0x000000FFFFFFFFFF);
	class FsElt {
	public:
	protected:
		/// a hash, to identify this element
		// first two bits of the id is used to get the type. please don't erase them
		// really 64 - 24  = 40 bits.
		const FsID m_id;
		const DateTime m_creation_date;
	public:
		//FsElt(uint64_t seed_id, ComputerId owner, DateTime date) :m_id(createId(seed_id, owner)), m_creation_date(date) {
		//	assert(m_id);
		//	assert(m_id & 0x03);
		//}
		FsElt(FsID id, DateTime date) : m_id(id), m_creation_date(date) {
			assert(m_id);
			assert(m_id & 0x03);
			assert(m_id & COMPUTER_ID_MASK);
		}

		/// in ms, the date for this element
		DateTime getDate() const { return m_creation_date; }
		/// the creator of this element
		ComputerId getOwner() const { return getComputerId(m_id); }
		FsID getId() const { return m_id; }

		static inline FsID createId(uint64_t seed_id, ComputerId owner) {
			return (seed_id & SEED_MASK) | (uint64_t(owner) << 40);
		}
		static inline ComputerId getComputerId(FsID id) {
			return ComputerId(id >> 40) & COMPUTER_ID_MASK;
		}

		///**
		// * Visitor function.
		// * @param visitor the visitor who visit this element.
		// */
		//virtual void accept(void* visitor) = 0;

		///**
		// * Perform deletion operation : delete sub-thing, delete content, delete entry in fs.
		// * It's final and irrecoverable. If you want just mark this as "deleted", use removeDir/removeFile/removeChunk instead.
		// */
		//virtual void remove() = 0;

		///**
		// * Call this when a changes occur on this element. It updates the ModifyDate.
		// */
		//virtual void changes() = 0;

		///**
		// * Notify that modifications on this object are finished and now should be saved / transmitted.
		// */
		//virtual void flush() = 0;

		//the dynamic_pointer_cast do the same thing...
		static FsFilePtr toFile(FsEltPtr elt);
		static FsChunkPtr toChunk(FsEltPtr elt);
		static FsDirPtr toDirectory(FsEltPtr elt);
		static FsObjectPtr toObject(FsEltPtr elt);

		static inline bool isFile(FsID id) {
			return ((id & 0x03) == uint8_t(FSType::FILE));
		}
		static inline bool isChunk(FsID id) {
			return ((id & 0x03) == uint8_t(FSType::CHUNK));
		}
		static inline bool isDirectory(FsID id) {
			return ((id & 0x03) == uint8_t(FSType::DIRECTORY));
		}
		static inline bool isObject(FsID id) {
			return ((id & uint8_t(FSType::FILE)) == uint8_t(FSType::FILE));
		}

		static inline FsID setNone(FsID id) {
			return ((id & NO_TYPE_MASK));
		}
		static inline FsID setFile(FsID id) {
			return ((id & NO_TYPE_MASK) | FsID(FSType::FILE));
		}
		static inline FsID setChunk(FsID id) {
			return ((id & NO_TYPE_MASK) | FsID(FSType::CHUNK));
		}
		static inline FsID setDirectory(FsID id) {
			return ((id & NO_TYPE_MASK) | FsID(FSType::DIRECTORY));
		}
	};


}