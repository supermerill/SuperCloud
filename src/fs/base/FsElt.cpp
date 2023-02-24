#pragma once

#include "FsElt.hpp"
#include "FsFile.hpp"
#include "FsDirectory.hpp"
#include "FsChunk.hpp"

#include <memory>

namespace supercloud {
	//the dynamic_pointer_cast do the same thing...
	FsFilePtr FsElt::toFile(FsEltPtr elt) {
		if ((elt->m_id & 0x03) == uint8_t(FSType::FILE)) {
			//return std::dynamic_pointer_cast<FsFile>(elt);
			return std::static_pointer_cast<FsFile>(elt);
		}
		return {};
	}
	FsChunkPtr FsElt:: toChunk(FsEltPtr elt) {
		if ((elt->m_id & 0x03) == uint8_t(FSType::CHUNK)) {
			//return std::dynamic_pointer_cast<FsFile>(elt);
			return std::static_pointer_cast<FsChunk>(elt);
		}
		return {};
	}
	FsDirPtr FsElt::toDirectory(FsEltPtr elt) {
		if ((elt->m_id & 0x03) == uint8_t(FSType::DIRECTORY)) {
			//return std::dynamic_pointer_cast<FsFile>(elt);
			return std::static_pointer_cast<FsDirectory>(elt);
		}
		return {};
	}
	FsObjectPtr FsElt::toObject(FsEltPtr elt) {
		// file & directory have the bit 0x02
		if ((elt->m_id & uint8_t(FSType::FILE)) == uint8_t(FSType::FILE)) {
			//return std::dynamic_pointer_cast<FsFile>(elt);
			return std::static_pointer_cast<FsObject>(elt);
		}
		return {};
	}


}