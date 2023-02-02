#pragma once

#include <cstdint>
#include <boost/asio.hpp>
#include <iostream>
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
	class Peer {

	public:
		enum PeerConnectionState : uint8_t {
			DEAD = (0),
			JUST_BORN = (1),
			HAS_ID = (2),
			HAS_PUBLIC_KEY = (3),
			HAS_VERIFIED_COMPUTER_ID = (4),
			CONNECTED_W_AES = (5),

		};

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

		//@Override
		//	public boolean equals(Object arg0) {
		//	if (arg0 instanceof Peer) {
		//		Peer o = (Peer)arg0;
		//		return o.myKey.address.equals(myKey.address) && o.myKey.otherPeerId == myKey.otherPeerId
		//			&& o.myKey.port == myKey.port;
		//	}
		//	return false;
		//}

		//@Override
		//	public int hashCode() {
		//	return myKey.hashCode();
		//}

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

		std::atomic<bool> alive = false;
		std::atomic<bool> aliveAndSet = false;
		int aliveFail = 0;
		std::shared_ptr<tcp::socket> sockWaitToDelete = nullptr;

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
		//my state, i added this after the dev of this class, so it's spartly used/updated for the dead/born/hasid state, need some debug to be barely reliable.
		// used mainly to see if HAS_PUBLIC_KEY, HAS_VERIFIED_COMPUTER_ID, CONNECTED_W_AES is done.
		PeerConnectionState myState = PeerConnectionState::DEAD;

	public:
		const int64_t createdAt;

		Peer(PhysicalServer& physicalServer, const std::string& inetAddress, int port) : myServer(physicalServer), myKey(PeerKey{ inetAddress, port }), createdAt(get_current_time_milis()) {}

		// write only
		/**
		 * update connection status.
		 * @return true if you should call ping quickly afterwards (connection phase)
		 */
		bool ping();
		// receive only (read)
		void run();

		void reconnect();

		bool connect(std::shared_ptr<tcp::socket> sock);

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

		uint16_t getPort() const {
			return myKey.getPort();
		}

		bool isAlive() const {
			return alive.load();
		}

		void setPort(int port) {
			if (myKey.getPort() != port) {
				myKey.setPort(port);
			}
		}

		const PeerKey& getKey() const {
			return myKey;
		}

		void close();

		void setComputerId(uint16_t distId) {
			this->distComputerId = distId;
		}

		uint16_t getComputerId() const {
			return distComputerId;
		}

		void flush() {
			//getOut().flush();
			//TODO
		}

		void changeState(PeerConnectionState newState, bool changeOnlyIfHigher) {
			//synchronized(myState) {
				if (!changeOnlyIfHigher || myState < newState)
					myState = newState;
			//}

		}

		bool hasState(PeerConnectionState stateToVerify) const {
			return myState >= stateToVerify; //!myState.lowerThan(stateToVerify);
		}

		PeerConnectionState getState() const {
			return myState;
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

