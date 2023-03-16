#pragma once

#include "network/ClusterManager.hpp"

namespace supercloud {

	//we need the 30-50 section of message types
	// 0-29 is reserved by network package.
	enum class SynchMessagetype : uint8_t {
		ENUM_START = 29,
		// request the last commit for some computers (or all).
		GET_STATE, //NOT USED (yet?)
		// send the last commit known for a computer.
		// so we can known if we are up-to-date in each computer timeline.
		SEND_STATE, //NOT USED (yet?)
		//request the new states since a commit & date ; given a directory/file, with a max depth.
		GET_TREE,
		// send all informations about all dir& files from the requested 'root', within the max depth. (only current state/last commit)
		SEND_TREE,
		// Send a message to notify that (an) element(s) is modified and a GET_TREE is needed to get the last state.
		// this message exists to avoid sending SEND_TREE evry microsecond when copiying a big directory. Other peers can ask for the change when they need it or some time after.
		SEND_INVALIDATE_ELT,
		// do you know where are these chunks?
		GET_CHUNK_AVAILABILITY,
		// these chunks may be available here
		SEND_CHUNK_AVAILABILITY,
		// can you give me this chunk/file?
		GET_CHUNK_REACHABLE,
		// yes/no (with how many hop) i can send you the chunk(s).
		SEND_CHUNK_REACHABLE,
		//want information&data about a chunk
		GET_SINGLE_CHUNK,
		// give all the information&data we have about the chunk requested
		SEND_SINGLE_CHUNK,
		// synch properties (what's its limits, what it keep where, what is your userid, your groups)
		//TODO
		GET_HOST_PROPERTIES, //NOT USED (yet?)
		SEND_HOST_PROPERTIES, //NOT USED (yet?)

	};
	constexpr auto operator*(SynchMessagetype smt) noexcept { return static_cast<uint8_t>(smt); }
}
