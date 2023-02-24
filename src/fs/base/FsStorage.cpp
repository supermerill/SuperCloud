#include "FsStorage.hpp"

#include "FsElt.hpp"
#include "FsChunk.hpp"
#include "FsFile.hpp"
#include "FsDirectory.hpp"

#include <filesystem>

//TODO: time manager to get it from it.

namespace supercloud {
        FsChunkPtr FsStorage::loadChunk(FsID id) { return FsElt::toChunk(load(id)); }
        FsFilePtr FsStorage::loadFile(FsID id) { return FsElt::toFile(load(id)); }
        FsDirPtr FsStorage::loadDirectory(FsID id) { return FsElt::toDirectory(load(id)); }
}
