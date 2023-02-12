#pragma once

#include "FsStorage.hpp"

#include <cstdint>
#include <memory>

namespace supercloud {
	enum class FSType : uint8_t {
		NONE = 0,
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
		FsElt(uint64_t seed_id, ComputerId owner, DateTime date) :m_id(createId(seed_id, owner)), m_creation_date(date) {
			assert(m_id);
			assert(m_id & 0x03);
		}
		FsElt(FsID id, DateTime date) : m_id(id), m_creation_date(date) {}

		/// in ms, the date for this element
		DateTime getDate() { return m_creation_date; }
		/// the creator of this element
		ComputerId getOwner() { return getComputerId(m_id); }
		FsID getId() { return m_id; }

		static inline FsID createId(uint64_t seed_id, ComputerId owner) {
			return (seed_id & SEED_MASK) | (uint64_t(owner) << 40);
		}
		static inline ComputerId getComputerId(FsID id) {
			return ComputerId(id >> 40) & COMPUTER_ID_MASK;
		}

		/**
		 * Visitor function.
		 * @param visitor the visitor who visit this element.
		 */
		virtual void accept(void* visitor) = 0;

		/**
		 * Perform deletion operation : delete sub-thing, delete content, delete entry in fs.
		 * It's final and irrecoverable. If you want just mark this as "deleted", use removeDir/removeFile/removeChunk instead.
		 */
		virtual void remove() = 0;

		/**
		 * Call this when a changes occur on this element. It updates the ModifyDate.
		 */
		virtual void changes() = 0;

		/**
		 * Notify that modifications on this object are finished and now should be saved / transmitted.
		 */
		virtual void flush() = 0;

		//the dynamic_pointer_cast do the same thing...
		static inline FsFilePtr toFile(FsEltPtr elt) {
			if ((elt->m_id & 0x03) == uint8_t(FSType::FILE)) {
				//return std::dynamic_pointer_cast<FsFile>(elt);
				return std::static_pointer_cast<FsFile>(elt);
			}
			return {};
		}
		static inline FsChunkPtr toChunk(FsEltPtr elt) {
			if ((elt->m_id & 0x03) == uint8_t(FSType::CHUNK)) {
				//return std::dynamic_pointer_cast<FsFile>(elt);
				return std::static_pointer_cast<FsChunk>(elt);
			}
			return {};
		}
		static inline FsDirPtr toDirectory(FsEltPtr elt) {
			if ((elt->m_id & 0x03) == uint8_t(FSType::DIRECTORY)) {
				//return std::dynamic_pointer_cast<FsFile>(elt);
				return std::static_pointer_cast<FsDirectory>(elt);
			}
			return {};
		}

		static inline bool isFile(FsID id) {
			return ((id & 0x03) == uint8_t(FSType::FILE));
		}
		static inline bool isChunk(FsID id) {
			return ((id & 0x03) == uint8_t(FSType::CHUNK));
		}
		static inline bool isDirectory(FsID id) {
			return ((id & 0x03) == uint8_t(FSType::DIRECTORY));
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