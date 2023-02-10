#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>

#include "../utils/ByteBuff.hpp"
#include "../utils/Utils.hpp"
#include "ClusterManager.hpp"
#include "networkAdapter.hpp"

namespace supercloud {

	class PhysicalServer;
	class Peer;

	/**
	 * Manage the connection with an other physical server: Check if it's alive, send and receive data.
	 *
	 * @author Admin
	 *
	 */
	class Peer : public std::enable_shared_from_this<Peer> {

	public:
		Peer(const Peer&) = delete;

		enum CloseReason {
			ALIVE,
			BAD_SERVER_ID,
			BAD_COMPUTER_ID,
			NETWORK_FAIL,
		};

		enum ConnectionState : uint16_t {
			NO_STATE = 0, // shouldn't happen, it should have at least an attribute
			// creation state
			US =			1 << 0, // the peer that represent ourself
			DATABASE =		1 << 1, // when the peer has been createdcreated at startup from the db
			ENTRY_POINT =	1 << 2, // a database entry with only hip/port, as an entry point to our first connection.
			TEMPORARY =		1 << 3, // when the peer object has been created from a connection event, but i still don't have its computer id to know more
			//connection state
			CONNECTING =	1 << 4, // when the connection between me and this peer is started.
			CONNECTED =		1 << 5, // when the connection between me and this peer is established, and we can exchange any msg.
			ALIVE_IN_NETWORK=1 << 6, // when (a peer tell me that) this peer is connected to the network
			DISCONNECTED =	1 << 7, //  when a peer isn't connected to the nework, but has been and may be still alive disconnected and may reconnect.
			// connection direction
			FROM_ME =		1 << 8, //  If i'm the one who initiate the connection.
		};
		static inline std::string connectionStateToString(ConnectionState state) {
			std::stringstream ss;
			if (state & US) ss << " | US";
			if (state & DATABASE) ss << " | DATABASE";
			if (state & ENTRY_POINT) ss << " | ENTRY_POINT";
			if (state & TEMPORARY) ss << " | TEMPORARY";
			if (state & CONNECTING) ss << " | CONNECTING";
			if (state & CONNECTED) ss << " | CONNECTED";
			if (state & ALIVE_IN_NETWORK) ss << " | ALIVE_IN_NETWORK";
			if (state & DISCONNECTED) ss << " | DISCONNECTED";
			if (state & FROM_ME) ss << " | FROM_ME";
			std::string str = ss.str();
			if (str.length() > 3) {
				return str.substr(3);
			}
			return str;
		}

	private:
		PhysicalServer& myServer;

		// private Socket connexion;
		// private InetSocketAddress address;
		ComputerId m_computer_id = NO_COMPUTER_ID; //unique id for the computer, private-public key protected.
		PeerId m_peer_id = NO_PEER_ID; //use for leader election, connection establishment

		std::string m_address;
		uint16_t m_port = 0;
		//if the connection is established, this exist and and be used to emit & receive message
		std::shared_ptr<Socket> m_socket = nullptr;
		// if we both emit connections to each other, this is the other (unused) connection.
		std::shared_ptr<Socket> m_socket_wait_to_delete = nullptr;
		//std::unique_ptr<std::istream> streamIn;
		std::mutex socket_read_mutex;
		std::mutex socket_write_mutex;
		//std::unique_ptr<std::ostream> streamOut;

		/// <summary>
		/// At least one thread is listening to the socket.
		/// </summary>
		std::atomic<bool> alive = false;

		/// <summary>
		/// At least a connection has receive a first message. Used when two connection were establish to the same peer, can happen if the two computer connect to each other at the same time.
		/// </summary>
		std::atomic<bool> aliveAndSet = false;

		/// <summary>
		/// reconnect fail counter
		/// </summary>
		int aliveFail = 0;


		/// <summary>
		/// Mutex for accesing-modify m_state, 
		/// </summary>
		std::mutex m_peer_mutex;

		ConnectionState m_state = ConnectionState::NO_STATE;
		//TODO: use it (curently not set)
		CloseReason close_reason = CloseReason::ALIVE;

		std::atomic<bool> is_thread_running = false;
		//Thread myCurrentThread = null;

		// long otherServerId = 0;

		int64_t nextTimeSearchMoreServers = 0;

		// public Peer(PhysicalServer physicalServer, InetSocketAddress inetSocketAddress) {
		// myServer = physicalServer;
		// myKey = new PeerKey(inetSocketAddress.getAddress());
		// myKey.port = inetSocketAddress.getPort();
		// // address = inetSocketAddress;
		// }
		inline static ByteBuff nullmsg = ByteBuff{};

		//Cipher encoder = null;
		//Cipher decoder = null;
		//allow IdentityManager to set our computerid.
		void setComputerId(ComputerId cid) {
			this->m_computer_id = cid;
		}
	public:
		class ComputerIdSetter
		{
		private:
			static inline void setComputerId(Peer& obj, ComputerId new_cid)
			{
				obj.setComputerId(new_cid);
			}
			friend class IdentityManager;
		};
		friend class ComputerIdSetter;
	protected:

		Peer(PhysicalServer& physicalServer, const std::string& inetAddress, int port, ConnectionState state) : m_state(state), myServer(physicalServer), m_address(inetAddress), m_port(port), createdAt(get_current_time_milis()) {}

	public:
		const int64_t createdAt;

		//factory
		[[nodiscard]] static PeerPtr create(PhysicalServer& physicalServer, const std::string& inetAddress, int port, ConnectionState state) {
			// Not using std::make_shared<Best> because the c'tor is private.
			return std::shared_ptr<Peer>(new Peer(physicalServer, inetAddress, port, state));
		}

		PeerPtr ptr() {
			return shared_from_this();
		}

		// read main infinite loop, that may propagate message to listener and they can write stuff.
		void run();

		void reconnect();

		bool connect(std::shared_ptr<Socket> sock, bool initiated_by_me);

		void startListen();

		void writeMessagePriorityClear(uint8_t messageId, ByteBuff& message);
		void writeMessage(uint8_t messageId, ByteBuff& message = ByteBuff{});

		void readMessage();

		//// getters  ///////////////////////////////////////////////////////////////////
		void setPeerId(PeerId new_id);

		PhysicalServer& getMyServer() {
			return myServer;
		}

		PeerId getPeerId() const {
			return m_peer_id;
		}

		std::string getIP() const {
			return m_address;
		}

		/// <summary>
		/// The local ip, with just the network part.
		/// </summary>
		/// <returns></returns>
		std::string getLocalIPNetwork() const;

		uint16_t getPort() const {
			return m_port;
		}

		void setPort(uint16_t port) {
			m_port = port;
		}

		bool isAlive() const {
			return alive.load();
		}
		bool initiatedByMe() {
			return 0 != (getState() & ConnectionState::FROM_ME);
		}

		std::mutex& synchronize() { return m_peer_mutex; }
		ConnectionState getState() { return m_state; }
		// Should be changed to a thread-safe compare&swap but not worth the hassle. so:
		// !! Use the synchronize() before calling getState and then setState, or even setState alone.
		void setState(ConnectionState new_state) { 
			assert(new_state != 0);
			m_state = new_state;
		}
		bool isConnected() { return 0 != (m_state & Peer::ConnectionState::CONNECTED); }

		void close();

		ComputerId getComputerId() const {
			return m_computer_id;
		}

	};


	inline Peer::ConnectionState operator|(Peer::ConnectionState a, Peer::ConnectionState b) {
		return static_cast<Peer::ConnectionState>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}
	inline Peer::ConnectionState operator&(Peer::ConnectionState a, Peer::ConnectionState b) {
		return static_cast<Peer::ConnectionState>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
	}
	inline Peer::ConnectionState operator^(Peer::ConnectionState a, Peer::ConnectionState b) {
		return static_cast<Peer::ConnectionState>(static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b));
	}
	inline Peer::ConnectionState operator~(Peer::ConnectionState a) {
		return static_cast<Peer::ConnectionState>(~static_cast<uint8_t>(a));
	}
	inline Peer::ConnectionState operator|=(Peer::ConnectionState& a, Peer::ConnectionState b) {
		a = a | b; return a;
	}
	inline Peer::ConnectionState operator&=(Peer::ConnectionState& a, Peer::ConnectionState b) {
		a = a & b; return a;
	}

} // namespace supercloud

