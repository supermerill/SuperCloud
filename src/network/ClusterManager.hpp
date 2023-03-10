#pragma once

#include <cassert>
#include <cstdint>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../utils/Utils.hpp"
#include "../utils/ByteBuff.hpp"

namespace supercloud {
	class IdentityManager;

	class Peer;
	typedef std::shared_ptr<Peer> PeerPtr;
	typedef std::vector<PeerPtr> PeerList;

	typedef uint64_t PeerId;
	//#define serializePeerId(buff,pid) buff.putULong(pid)
#define serializePeerId putULong
#define deserializePeerId getULong

	// it's only 24bits of useful space
	typedef uint32_t ComputerId;
#define serializeComputerId putUInt
#define deserializeComputerId getUInt
	constexpr ComputerId COMPUTER_ID_MASK = 0x00FFFFFF;

	constexpr ComputerId NO_COMPUTER_ID = ComputerId(-1);
	constexpr uint64_t NO_CLUSTER_ID = uint64_t(-1);
	constexpr PeerId NO_PEER_ID = PeerId(-1);

	//TODO crypt
	typedef std::vector<uint8_t> PublicKey; //can't use string because 0x00 create problems.
	typedef std::vector<uint8_t> PrivateKey;
	typedef std::vector<uint8_t> SecretKey;

	// message type 0->29 are reserved for network
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
		///  Ask to know if the connection can be considered established.
		/// </summary>
		GET_CONNECTION_ESTABLISHED,
		/// <summary>
		///  Notify that we consider this connection established
		/// </summary>
		SEND_CONNECTION_ESTABLISHED,
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
		/// Used to notify the other side taht we close the pipe.
		/// Used to notify listener for closing the socket / a connection with a peer.
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
		//TODO
		//GET_MAX_FLOW,
		//SEND_MAX_FLOW,
	};
	constexpr auto operator*(UnnencryptedMessageType emt) noexcept{return static_cast<uint8_t>(emt);}

	// interface to let the connection manager manange the connection state, while it's still immutable for evryone else.
	class ServerConnectionState {
	protected:
		size_t m_something_connecting = 0;
		mutable std::mutex mutex;
		std::set<ComputerId> m_connected;
		bool has_connection = false;
		bool is_disconnecting = false; // == closed,  when turned to true it can't go back to false;
		std::vector<std::string> logmsg;
		bool m_is_computer_id_problematic = false;
	public:
		inline bool wasConnected() const { return has_connection; }
		inline bool isConnected() const { return m_connected.size() > 0; }
		inline bool isConnected(ComputerId computer_id) const { std::lock_guard lock{ mutex }; return m_connected.find(computer_id) != m_connected.end(); }
		inline bool isConnectionInProgress() const { return m_something_connecting>0; }
		inline size_t getConnectionsCount() const { return m_connected.size(); }
		inline void setOurComputerIdInvalid() { m_is_computer_id_problematic = true; }
		inline bool isOurComputerIdProblematic() const { return m_is_computer_id_problematic; }
		inline bool isClosed() const { return is_disconnecting; }

		inline void beganConnection() {
			std::lock_guard lock{ mutex };
			++m_something_connecting;
			is_disconnecting = false;
			logmsg.push_back("began connection");
		}
		inline void abordConnection() {
			std::lock_guard lock{ mutex };
			if (!is_disconnecting) {
				assert(m_something_connecting > 0);
				m_something_connecting = std::max(size_t(0), m_something_connecting - 1);
				logmsg.push_back("abordConnection");
			}
		}
		inline void finishConnection(ComputerId computer_id) {
			std::lock_guard lock{ mutex };
			assert(m_something_connecting > 0);
			assert(!is_disconnecting);
			m_something_connecting = std::max(size_t(0), m_something_connecting - 1);
			m_connected.insert(computer_id);
			has_connection = true;
			logmsg.push_back(std::string("finishConnection ") + computer_id);
		}
		inline void removeConnection(ComputerId computer_id) {
			std::lock_guard lock{ mutex };
			if (!is_disconnecting) {
				auto it = m_connected.find(computer_id);
				assert(it != m_connected.end());
				if (it != m_connected.end()) {
					m_connected.erase(it);
				}
				logmsg.push_back(std::string("removeConnection ") + computer_id);
			}
		}
		inline void disconnect() {
			std::lock_guard lock{ mutex };
			is_disconnecting = true;
			has_connection = false;
			m_connected.clear();
			m_something_connecting = 0;
			logmsg.push_back(std::string("DISCONNECT"));
		}
	};

	class AbstractMessageManager {
	public:
		virtual void receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) = 0;
	};

	class ClusterManager {
	public:
		const ServerConnectionState& getState();

		/// <summary>
		/// Get the current UTC time, as defined by the network.
		/// the precision of DateTime is at ms scale, but the network doesn't enforce a datetime synhronization below the second.
		/// In practice, sub-second synch doesn't matter.
		/// </summary>
		virtual DateTime getCurrentTime() = 0;

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
		//virtual bool writeMessage(int64_t peerId, uint8_t messageType, const ByteBuff& message) = 0;
		virtual void registerListener(uint8_t message_id, std::shared_ptr<AbstractMessageManager> listener) = 0;
		virtual void unregisterListener(uint8_t message_id, std::shared_ptr<AbstractMessageManager> listener) = 0;
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
		virtual std::future<bool> connect(PeerPtr peer, const std::string& string, uint16_t port, int64_t timeout_milis) = 0;
		
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

		virtual ComputerId getComputerId() const = 0;
		virtual void launchUpdater() = 0;
		virtual void initializeNewCluster() = 0;

		/**
		 * Get the computerId of a peerId.
		 * @param senderId the peerId (what we receive from the net message)
		 * @return  the computerid or -1 if it's not connected (yet).
		 */
		virtual ComputerId getComputerId(PeerId senderId) const = 0; //get a computerId from a peerId (senderId)
		virtual PeerId getPeerIdFromCompId(ComputerId compId) const = 0; //get a peerId (senderId) from a computerId

		virtual IdentityManager& getIdentityManager() = 0;
		virtual std::string getLocalIPNetwork() const = 0;

		//for logging
		virtual PeerId getPeerId() const = 0;
	};


} // namespace supercloud
