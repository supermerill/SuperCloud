#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <boost/asio.hpp>

#include "../utils/Utils.hpp"
#include "../utils/ByteBuff.hpp"

namespace supercloud {

	class AbstractMessageManager {
	public:
		//list of message id

		// ----------------------------------- connection & leader election ---------------------------------------------
			/**
			 * get the distant server id, for leader election
			 * no data
			 */
		inline static uint8_t GET_SERVER_ID = (uint8_t)1;
		/** if i detect an other server with my id, i have to create a new id, and use this command to send my new id to everyone.
		 * or just a response to GET_SERVER_ID
		 * data : uint8_t[8] -> long -> serverid
		 */
		inline static uint8_t SEND_SERVER_ID = (uint8_t)2;

		/**
		 * Ask the list of servers in this cluster
		 */
		inline static uint8_t  GET_SERVER_LIST = (uint8_t)3;
		/**
		 * Send his current server list
		 */
		inline static uint8_t  SEND_SERVER_LIST = (uint8_t)4;
		/**
		 * Ask the port needed to contact him
		 */
		inline static uint8_t  GET_LISTEN_PORT = (uint8_t)5;
		/**
		 * Send his port where he listen new connections
		 */
		inline static uint8_t  SEND_LISTEN_PORT = (uint8_t)6;

		/**
		 * request a SEND_SERVER_PUBLIC_KEY
		 */
		inline static uint8_t  GET_SERVER_PUBLIC_KEY = (uint8_t)7;
		/**
		 * Send his public key.<br>
		 */
		inline static uint8_t  SEND_SERVER_PUBLIC_KEY = (uint8_t)8;

		/**
		 * send a public-private encrypted message to be encoded.
		 */
		inline static uint8_t  GET_VERIFY_IDENTITY = (uint8_t)9;
		/**
		 * Send back the message with a public-private encryption, to tell the other one i am really me.
		 */
		inline static uint8_t  SEND_VERIFY_IDENTITY = (uint8_t)10;
		/**
		 * request a AES
		 */
		inline static uint8_t  GET_SERVER_AES_KEY = (uint8_t)11;
		/**
		 * emit our AES (encrypted with our private & his public key)
		 */
		inline static uint8_t  SEND_SERVER_AES_KEY = (uint8_t)12;

		/**
		 * For priority unencrypted message
		 */
		inline static uint8_t  PRIORITY_CLEAR = (uint8_t)13;

		/**
		 * Used to see if the message is aes-encoded
		 */
		inline static uint8_t  LAST_UNENCODED_MESSAGE = (uint8_t)14;


		virtual void receiveMessage(uint64_t senderId, uint8_t messageId, ByteBuff& message) = 0;


	};

	class ClusterManager {
	public:
		//not used ... yet?
		//virtual void requestUpdate(int64_t since) = 0;
		//virtual void requestChunk(uint64_t fileId, uint64_t chunkId) = 0;
		//virtual void propagateDirectoryChange(uint64_t directoryId, std::vector<uint8_t> changes) = 0; //TODO
		//virtual void propagateFileChange(uint64_t directoryId, std::vector<uint8_t> changes) = 0; //TODO
		//virtual void propagateChunkChange(uint64_t directoryId, std::vector<uint8_t> changes) = 0; //TODO



		//used
		/**
		 *
		 * @param sendFileDescr
		 * @param message
		 * @return number of peer conteacted (do not assumed they will all answer)
		 */
		//virtual int32_t writeBroadcastMessage(uint8_t sendFileDescr, ByteBuff& message) = 0;
		/**
		 *
		 * @param senderId
		 * @param sendDir
		 * @param messageRet
		 * @return true if the message should be emitted (no guaranty)
		 */
		//virtual bool writeMessage(int64_t peerId, uint8_t messageType, ByteBuff& message) = 0;
		virtual void registerListener(uint8_t getDir, std::shared_ptr<AbstractMessageManager> propagateChange) = 0;
		virtual void init(uint16_t listenPort) = 0;

		/**
		 * Get the number of peers with which i can communicate.
		 * @return number of connected peers at this moment.
		 */
		virtual size_t getNbPeers() = 0;

		/**
		 * Try to connect to a new peer at this address/port
		 * @param ip address
		 * @param port port
		 * @return true if it's maybe connected, false if it's maybe not connected
		 */
		virtual bool connect(const std::string& string, uint16_t port) = 0;
		/**
		 *
		 * @param ip address
		 * @param port port
		 * @return number of connected peer (approximation).
		 */
		virtual int32_t connect() = 0;

		/**
		 * shutdown
		 */
		virtual void close() = 0;

		virtual uint16_t getComputerId() = 0;
		virtual void launchUpdater() = 0;
		virtual void initializeNewCluster() = 0;

		/**
		 * Get the computerId of a peerId.
		 * @param senderId the peerId (what we receive from the net message)
		 * @return  the computerid or -1 if it's not connected (yet).
		 */
		virtual uint16_t getComputerId(uint64_t senderId) = 0; //get a computerId from a peerId (senderId)
		virtual uint64_t getPeerIdFromCompId(uint16_t compId) = 0; //get a peerId (senderId) from a computerId

		virtual boost::asio::ip::tcp::endpoint getListening() = 0;
		/**
		 *
		 * @return true if the net is currently connecting to the cluster
		 */
		virtual bool isConnecting() = 0;
	};


} // namespace supercloud
