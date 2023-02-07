#pragma once

#include <cstdint>
#include <vector>
#include <set>
#include <string>

#include "../utils/Utils.hpp"
#include "../utils/ByteBuff.hpp"
#include "Peer.hpp"

namespace supercloud {
	class IdentityManager;

	enum class UnnencryptedMessageType : uint8_t {
		/// <summary>
		/// nothing
		/// </summary>
		NO_MESSAGE,
		/// <summary>
		/// get the distant server ids (and port)
		/// </summary>
		GET_SERVER_ID,
		/// <summary>
		/// receive the peer id (if any), the computer id (if any) and the listening port of this peer.
		/// or just a response to GET_SERVER_ID
	    /// data : uint8_t[8] -> long->serverid
		/// </summary>
		SEND_SERVER_ID,
		/// <summary>
		/// Ask the list of servers in this cluster
		/// </summary>
		GET_SERVER_LIST,
		/// <summary>
		///  Send his current server list (register & connected) (only ids, no ip)
		/// </summary>
		SEND_SERVER_LIST,
		/// <summary>
		/// request a SEND_SERVER_PUBLIC_KEY
		/// </summary>
		GET_SERVER_PUBLIC_KEY,
		/// <summary>
		/// Send his public key.<br>
		/// </summary>
		SEND_SERVER_PUBLIC_KEY,
		/// <summary>
		/// send a public-private encrypted message to be encoded.
		/// </summary>
		GET_VERIFY_IDENTITY,
		/// <summary>
		/// Send back the message with a public-private encryption, to tell the other one i am really me.
		/// Maybe also with my own message to encoded
		/// </summary>
		SEND_VERIFY_IDENTITY,
		/// <summary>
		///  request an AES
		/// </summary>
		GET_SERVER_AES_KEY,
		/// <summary>
		///  emit our AES (encrypted with our private & his public key)
		/// </summary>
		SEND_SERVER_AES_KEY,
		/// <summary>
		///  We a peer decide to change our computer id because of a collision, it emit this message.
		/// TODO
		/// </summary>
		REVOKE_COMPUTER_ID,
		/// <summary>
		/// For priority unencrypted message (ping or things like that)
		/// </summary>
		PRIORITY_CLEAR,
		/// <summary>
		/// Fake message when a successful connection is established with a new peer.
		/// </summary>
		NEW_CONNECTION,
		/// <summary>
		/// Fake message to notify listener for closing the socket / a connection with a peer.
		/// The reason can be get from the peer object
		/// </summary>
		CONNECTION_CLOSED,
		/// <summary>
		/// Fake message to notify the listeners every second, from the CLusterManager/PhysicalServer update thread.
		/// The message contains the current time milis
		/// </summary>
		TIMER_SECOND,
		/// <summary>
		/// Fake message to notify the listeners every minute, from the CLusterManager/PhysicalServer update thread.
		/// The message contains the current time milis
		/// </summary>
		TIMER_MINUTE,
		/// <summary>
		/// If the Id of the message is at least this number, then it's an encrypted message.
		/// </summary>
		FIRST_ENCODED_MESSAGE = 20,

		/// <summary>
		/// REquest to get the peer database of the peers it encountered, to enrich ours.
		/// </summary>
		GET_SERVER_DATABASE,
		/// <summary>
		/// The peer's database of its peers
		/// </summary>
		SEND_SERVER_DATABASE,
	};
	constexpr auto operator*(UnnencryptedMessageType emt) noexcept{return static_cast<uint8_t>(emt);}


	class ServerConnectionState {
	protected:
		size_t m_something_connecting = 0;
		mutable std::mutex mutex;
		std::set<uint16_t> m_connected;
		bool has_connection = false;
	public:
		inline bool wasConnected() const { return has_connection; }
		inline bool isConnected() const { return m_connected.size() > 0; }
		inline bool isConnected(uint16_t computer_id) const { std::lock_guard lock{ mutex }; return m_connected.find(computer_id) != m_connected.end(); }
		inline bool isConnectionInProgress() const { return m_something_connecting>0; }
		inline size_t getConnectionsCount() const { return m_connected.size(); }

		inline void beganConnection() {
			std::lock_guard lock{ mutex };
			++m_something_connecting;
		}
		inline void abordConnection() {
			std::lock_guard lock{ mutex };
			assert(m_something_connecting > 0);
			m_something_connecting = std::max(size_t(0), m_something_connecting - 1);
		}
		inline void finishConnection(uint16_t computer_id) {
			std::lock_guard lock{ mutex };
			assert(m_something_connecting > 0);
			m_something_connecting = std::max(size_t(0), m_something_connecting - 1);
			m_connected.insert(computer_id);
			has_connection = true;
		}
		inline void removeConnection(uint16_t computer_id) {
			std::lock_guard lock{ mutex };
			auto it = m_connected.find(computer_id);
			assert(it != m_connected.end());
			if (it != m_connected.end()) {
				m_connected.erase(it);
			}
		}
		inline void disconnect() {
			std::lock_guard lock{ mutex };
			assert(m_connected.empty());
			assert(m_something_connecting == 0);
			m_connected.clear();
			m_something_connecting = 0;
		}
	};

	class AbstractMessageManager {
	public:
		virtual void receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) = 0;
	};

	class ClusterManager {
	public:
		const ServerConnectionState& getState();


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
		virtual size_t getNbPeers() const = 0;
		virtual PeerList getPeersCopy() const = 0;

		/**
		 * Try to connect to a new peer at this address/port
		 * @param ip address
		 * @param port port
		 * @return true if it's maybe connected, false if it's maybe not connected
		 */
		virtual std::future<bool> connect(const std::string& string, uint16_t port, int64_t timeout_milis) = 0;
		
		/// <summary>
		/// Try to connect all peers in our database, in an async way
		/// </summary>
		/// <returns></returns>
		virtual void connect() = 0;
		virtual bool reconnect() = 0;

		/**
		 * shutdown
		 */
		virtual void close() = 0;

		virtual uint16_t getComputerId() const = 0;
		virtual void launchUpdater() = 0;
		virtual void initializeNewCluster() = 0;

		/**
		 * Get the computerId of a peerId.
		 * @param senderId the peerId (what we receive from the net message)
		 * @return  the computerid or -1 if it's not connected (yet).
		 */
		virtual uint16_t getComputerId(uint64_t senderId) const = 0; //get a computerId from a peerId (senderId)
		virtual uint64_t getPeerIdFromCompId(uint16_t compId) const = 0; //get a peerId (senderId) from a computerId

		virtual IdentityManager& getIdentityManager() = 0;

		//for logging
		virtual uint64_t getPeerId() const = 0;
	};


} // namespace supercloud
