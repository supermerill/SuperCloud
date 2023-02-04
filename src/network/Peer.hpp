#pragma once

#include <cstdint>
#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>

#include "../utils/ByteBuff.hpp"
#include "../utils/Utils.hpp"

using boost::asio::ip::tcp;

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

		class PeerKey {
		private:
			uint64_t otherPeerId = 0; //use for leader election, connection establishment
			std::string address;
			uint16_t port = 0;

		public:
			PeerKey(std::string inetAddress) : address(inetAddress) {
			}

			PeerKey(const std::string& address, int port) : address(address), port(port) {
			}

			//boolean equals(Object arg0) {
			//	if (arg0 instanceof PeerKey) {
			//		PeerKey o = (PeerKey)arg0;
			//		if (o.address.equals(address)) {
			//			if (o.port != 0 && port != 0) {
			//				return o.port == port;
			//			} else if (otherPeerId != 0 && o.otherPeerId != 0) {
			//				return otherPeerId == o.otherPeerId;
			//			}
			//		}
			//	}
			//	return false;
			//}

			//@Override
			//	public int hashCode() {
			//	return address.hashCode();
			//}

			uint64_t getPeerId() const {
				return otherPeerId;
			}

			const std::string& getAddress() const {
				return address;
			}

			uint16_t getPort() const {
				return port;
			}

			void setPeerId(uint64_t peerId) {
				this->otherPeerId = peerId;
			}
			
			void setPort(uint16_t port) {
				this->port = port;
			}

			void setAdress(std::string& addr) {
				this->address = addr;
			}

			std::string to_string() const {
				return address + ":" + std::to_string(otherPeerId) + ":" + std::to_string(port);
			}

		};

		enum CloseReason {
			ALIVE,
			BAD_SERVER_ID,
			BAD_COMPUTER_ID,
			NETWORK_FAIL,
		};

	private:
		PhysicalServer& myServer;

		// private Socket connexion;
		// private InetSocketAddress address;
		uint16_t distComputerId = NO_COMPUTER_ID; //unique id for the computer, private-public key protected.
		PeerKey myKey;
		std::shared_ptr<tcp::socket> socket = nullptr;
		//std::unique_ptr<std::istream> streamIn;
		std::mutex socket_read_write_mutex;
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
		/// The connection protocol has ended with success. evryone can now emit message to this peer.
		/// </summary>
		bool connected = false;
		bool is_initiator = false;

		std::shared_ptr<tcp::socket> sockWaitToDelete = nullptr;
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

		Peer(PhysicalServer& physicalServer, const std::string& inetAddress, int port) : myServer(physicalServer), myKey(PeerKey{ inetAddress, port }), createdAt(get_current_time_milis()) {}

	public:
		const int64_t createdAt;

		//factory
		[[nodiscard]] static PeerPtr create(PhysicalServer& physicalServer, const std::string& inetAddress, int port) {
			// Not using std::make_shared<Best> because the c'tor is private.
			return std::shared_ptr<Peer>(new Peer(physicalServer, inetAddress, port));
		}

		PeerPtr ptr() {
			return shared_from_this();
		}

		// read main infinite loop, that may propagate message to listener and they can write stuff.
		void run();

		void reconnect();

		bool connect(std::shared_ptr<tcp::socket> sock, bool initiated_by_me);

		void startListen();

		void writeMessagePriorityClear(uint8_t messageId, ByteBuff& message);
		void writeMessage(uint8_t messageId, ByteBuff& message = ByteBuff{});

		void readMessage();

		//// functions calleds by messages ///////////////////////////////////////////////////////////////////
		void setPeerId(uint64_t newId) {
			myKey.setPeerId(newId);
		}

		PhysicalServer& getMyServer() {
			return myServer;
		}

		//protected OutputStream getOut() {
		//	return streamOut;
		//}

		//protected InputStream getIn() {
		//	return streamIn;
		//}

		uint64_t getPeerId() const {
			return myKey.getPeerId();
		}

		std::string getIP() const {
			return myKey.getAddress();
		}

		/// <summary>
		/// Like get Ip but just the network part.
		/// </summary>
		/// <returns></returns>
		std::string getIPNetwork() const;

		uint16_t getPort() const {
			return myKey.getPort();
		}

		bool isAlive() const {
			return alive.load();
		}
		bool initiatedByMe() {
			return is_initiator;
		}

		void setPort(uint16_t port) {
			if (myKey.getPort() != port) {
				myKey.setPort(port);
			}
		}

		const PeerKey& getKey() const {
			return myKey;
		}

		void setConnected() { connected = true; }
		bool isConnected() { return connected; }
		void close();

		void setComputerId(uint16_t distId) {
			this->distComputerId = distId;
		}

		uint16_t getComputerId() const {
			return distComputerId;
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

} // namespace supercloud

