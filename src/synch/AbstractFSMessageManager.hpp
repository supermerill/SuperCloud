#pragma once

#include "network/ClusterManager.hpp"

namespace supercloud {

	enum class SynchMessagetype : uint8_t {
		// request the last commit for some computers (or all).
		GET_STATE,
		// send the last commit known for a computer.
		// so we can known if we are up-to-date in each computer timeline.
		SEND_STATE,
		//request the new states since a commit & date
		// given a directory/file, with a max depth.
		GET_TREE,
		// send all informations about all dir& files from the requested 'root', within the max depth. (only current state/last commit)
		SEND_TREE,
		////want information about at least one directory
		//GET_DIR = 30,
		//// give all the information we have about the directories requested (all commits stored)
		//SEND_DIR,
		////want information about at least one file
		//GET_FILE,
		//// give all the information we have about the files requested (all commits stored)
		//SEND_FILE,
		// do you know where are these chunks?
		GET_CHUNK_AVAILABILITY,
		// these chunks may be available here
		SEND_CHUNK_AVAILABILITY,
		// can you give me this chunk/file?
		GET_CHUNK_REACHABLE,
		// yes/no (with how many hop) i can send you these.
		SEND_CHUNK_REACHABLE,
		//want information&data about a chunk/file
		GET_CHUNK,
		// give all the information&data we have about the chunk/file requested
		SEND_CHUNK,
		// synch properties (what's its limits, what it keep where, what is your userid, your groups)
		//TODO
		GET_HOST_PROPERTIES,
		SEND_HOST_PROPERTIES,

	};
	constexpr auto operator*(SynchMessagetype smt) noexcept { return static_cast<uint8_t>(smt); }
}
