#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>

#include "../utils/ByteBuff.hpp"
#include "../utils/Utils.hpp"
#include "networkAdapter.hpp"

namespace supercloud {

	class PhysicalServer;
	class Peer;
	typedef std::shared_ptr<Peer> PeerPtr;

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

		enum ConnectionState : uint8_t {
			NO_STATE = 0, // shouldn't happen, it should have at least an attribute
			LOADED_FROM_DB = 1 << 0, // when created at startup from the db
			CONNECTING = 1 << 1, // when the connection between me and this peer is started.
			CONNECTED = 1 << 2, // when the connection between me and this peer is established, and we can exchange any msg.
			ALIVE_IN_NETWORK = 1 << 3, // when (a peer tell me that) this peer is connected to the network
			DISCONNECTED = 1 << 4, //  when a peer isn't connected to the nework, but may be still alive disconnected and may reconnect.
			FROM_ME = 1 << 5, //  If i'm the one who initiate the connection.
		};

	private:
		PhysicalServer& myServer;

		// private Socket connexion;
		// private InetSocketAddress address;
		uint16_t m_computer_id = NO_COMPUTER_ID; //unique id for the computer, private-public key protected.
		uint64_t m_peer_id = NO_PEER_ID; //use for leader election, connection establishment

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
		void setPeerId(uint64_t new_id) {
			m_peer_id = new_id;
		}

		PhysicalServer& getMyServer() {
			return myServer;
		}

		uint64_t getPeerId() const {
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

		bool isAlive() const {
			return alive.load();
		}
		bool initiatedByMe() {
			return 0 != (getState() & ConnectionState::FROM_ME);
		}

		void setPort(uint16_t port) {
			m_port = port;
		}

		std::mutex& synchronize() { return m_peer_mutex; }
		ConnectionState getState() { return m_state; }
		// Should be changed to a thread-safe compare&swap but not worth the hassle. so:
		// !! Use the synchronize() before calling getState and then setState, or even setState alone.
		void setState(ConnectionState new_state) { m_state = new_state; }
		bool isConnected() { return 0 != (m_state & Peer::ConnectionState::CONNECTED); }

		void close();

		void setComputerId(uint16_t cid) {
			this->m_computer_id = cid;
		}

		uint16_t getComputerId() const {
			return m_computer_id;
		}

	};


	class PeerList : public std::vector<PeerPtr> {
	public:
		PeerList() : vector() {}

		PeerList(const std::initializer_list<PeerPtr>& c) : vector(c) {}
		PeerList(std::initializer_list<PeerPtr>&& c) : vector(c) {}
		PeerList(const std::vector<PeerPtr>& c) : vector(c) {}
		PeerList(std::vector<PeerPtr>&& c) : vector(c) {}

		PeerList(size_t initialCapacity) : vector() {
			reserve(initialCapacity);
		}

		std::optional<PeerPtr> get(const Peer& other) {
			for (PeerPtr& e : *this) {
				if (e->getPeerId() == other.getPeerId()) {
					return e;
				}
			}
			return {};
		}

		std::vector<std::shared_ptr<Peer>> getAll(const Peer& other) {
			std::vector<PeerPtr> list;
			for (PeerPtr& e : *this) {
				if (e->getPeerId() == other.getPeerId()) {
					list.push_back(e);
				}
			}
			return list;
		}

		//check if the Peer pointer is stored here
		bool contains(PeerPtr& peer) {
			for (PeerPtr& e : *this) {
				if (e.get() == peer.get()) {
					return true;
				}
			}
			return false;
		}
		bool erase_from_ptr(PeerPtr& peer) {
			for (auto it = this->begin(); it != end(); ++it) {
				if (it->get() == peer.get()) {
					this->erase(it);
					return true;
				}
			}
			return false;
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

