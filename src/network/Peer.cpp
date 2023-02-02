#include "Peer.hpp"

#include <chrono>
#include <mutex>
#include <thread>

#include "utils/Utils.hpp"
#include "network/ClusterManager.hpp"
#include "PhysicalServer.hpp"
#include "ServerIdDb.hpp"

namespace supercloud {


	// write only
	/**
	 * update connection status.
	 * @return true if you should call ping quickly afterwards (connection phase)
	 */
	bool Peer::ping() {
		log( std::string("peer ") + this->myKey.getPeerId() + " compId:" + this->distComputerId + " : is alive? " + this->alive.load());
		if (!this->alive.load())
			return false;
		try {

			if (myKey.getPort() <= 0) {
				this->writeMessage(AbstractMessageManager::GET_LISTEN_PORT, nullmsg);
				// GetListenPort.get().write(getOut(), this);
				// getOut().flush();
			}
			if (myKey.getPeerId() == 0 || myState < PeerConnectionState::HAS_ID) {
				this->writeMessage(AbstractMessageManager::GET_SERVER_ID, nullmsg);
				// GetServerId.get().write(getOut(), this);
				// getOut().flush();
				return true;
			}

			// get the server list of the other one every 10-15 min.
//			this->writeMessage(AbstractMessageManager::GET_SERVER_LIST, nullmsg);
			if (nextTimeSearchMoreServers < get_current_time_milis()) {
				if (nextTimeSearchMoreServers == 0) {
					nextTimeSearchMoreServers = get_current_time_milis() + 1000;
				} else {
					nextTimeSearchMoreServers = get_current_time_milis()
						+ 1000 * 60 * (10 + rand_char()%10); // +10-19min
				// }
					this->writeMessage(AbstractMessageManager::GET_SERVER_LIST, nullmsg);
					// GetServerList.get().write(getOut(), this);
					// getOut().flush();
				}

			}

			if (myState < PeerConnectionState::HAS_PUBLIC_KEY) {
				myServer.getServerIdDb().requestPublicKey(*this);
				return true;
			}

			if (myState < PeerConnectionState::HAS_VERIFIED_COMPUTER_ID) {
				myServer.getServerIdDb().sendIdentity(*this, myServer.getServerIdDb().createMessageForIdentityCheck(*this, false), true);
				return true;
			}

			if (myState < PeerConnectionState::CONNECTED_W_AES) {
				myServer.getServerIdDb().requestSecretKey(*this);
				return true;
			}


			return false;
		}
		catch (std::exception e) {
			std::cerr << "ERROR: " << e.what() << "\n";
			return false;
		}
	}

	// receive only (read)
	void Peer::run() {
		//myCurrentThread = Thread.currentThread();
		if (is_thread_running.exchange(true)) {
			std::cerr << "error, peer is already running";
			return;
		}
		try {
			while (true) {
				readMessage();
			}
		}
		catch (std::exception e1) {
			std::cerr << "ERROR: " << e1.what() << "\n";
			std::cerr<< myServer.getPeerId() % 100 << " error in the communication stream between peers" + myServer.getPeerId() % 100 << " and "
				<< getKey().getPeerId() % 100 + " : " << e1.what()<<"\n";
		}

		// check if i'm not a duplicate
		if (myServer.getPeersCopy().getAll(*this).size() > 1 || myServer.getPeerId() == getPeerId()) {
			// i'm a duplicate, kill me!
			myServer.removeExactPeer(this);
			return;
		}

		// try to reconnect with the second connection if already enabled
		//try {
			if (!socket->is_open()) {
				if (sockWaitToDelete) {
					if (sockWaitToDelete->is_open()) {
						log(std::to_string( this->myServer.getPeerId() % 100 ) + " reconnect socket: " + socket->is_open() + "\n");
						socket = sockWaitToDelete;
						sockWaitToDelete.reset();
						//this->streamIn = new BufferedInputStream(sock.getInputStream());
						//this->streamOut = new BufferedOutputStream(sock.getOutputStream());
						std::thread relaunchThread([this]() {this->run(); });
						relaunchThread.detach();
						return; // dont launch the reconnection protocol, we are already connected.
					}
					sockWaitToDelete.reset();
				}
			}
		//}
		//catch (std::exception e2) {
		//	std::cerr << "ERROR: " << e2.what() << "\n";
		//}
		//myCurrentThread = nullptr;
		is_thread_running.store(false);
		changeState(PeerConnectionState::DEAD, false);

		if (alive.load()) {
			aliveAndSet.store(false);
			aliveFail = 0;
			alive.store(false);
			// reconnect();
		}
		// else, it's a kill from someone else who doesn't want me

	}

	//class mysocket : public tcp::socket {
	//public:
	//	mysocket(boost::asio::io_service& context) : tcp::socket(context) {}
	//	virtual ~mysocket() {
	//		std::cout << "destroying the socket\n";
	//	}
	//	void close() override {
	//		tcp::socket::close(
	//	}
	//};

	void Peer::reconnect() {
		if (myKey.getAddress() == "" || myKey.getPort() == uint16_t(-1)) {
			msg("can't reconnect because i didn't know the address");
			return;
		}
		while (!alive.load() && aliveFail < 10) {

			// try to connect
			log(std::string("try to reconnect to ") + myKey.getAddress());

			try{
				boost::asio::io_service ios;
				tcp::endpoint addr(boost::asio::ip::address::from_string(myKey.getAddress()), myKey.getPort());
				std::shared_ptr<tcp::socket> sock(new tcp::socket(ios));
				sock->set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 2000 });
				sock->connect(addr);
				if (sock->is_open()) {
					startListen();
				}
				aliveFail++;
			}
			catch (std::exception e) {
				std::cerr << "ERROR: " << e.what() << "\n";
				throw std::runtime_error(e.what());
			}

		}

	}

	bool Peer::connect(std::shared_ptr<tcp::socket> sock) {
		log(std::to_string(myServer.getPeerId() % 100) + " " + myServer.getListenPort() + " going to connect with " + sock->remote_endpoint().port());
		bool alreadyAlive = alive.exchange(true);
		if (alreadyAlive) {
			// two connections established, keep the one with the higher number

			// wait field sets
			while (!aliveAndSet) {
				// log(this +" "+ alive.get()+" SETTO "+myServer.getId()%100+ " alive but not set yet for " + sock.getPort());
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			bool iWin = false;
			// now compare the numbers
			if (myServer.getPeerId() == myKey.getPeerId()) {
				log(std::to_string(myServer.getPeerId() % 100) + " i have the same id as " + myKey.getPeerId() % 100);
				// me and the other server must recreate a hash id.
				// compare ips to choose
				int winner = sock->local_endpoint().address().to_string() < (sock->remote_endpoint().address().to_string());
				if (sock->local_endpoint().address().to_string() == (sock->remote_endpoint().address().to_string()))
					winner = compare(sock->local_endpoint().port(), sock->remote_endpoint().port());
					//winner = Integer.compare(sock.getLocalPort(), sock.getPort());
				if (winner > 0) {
					iWin = true;
					// close();
					// myServer.rechooseId();
				} else {
					iWin = false;
				}
			} else if (myServer.getPeerId() > myKey.getPeerId()) {
				iWin = true;
			} else {
				iWin = false;
			}

			if (iWin) {
				// i can kill this new one.
				sock->close();
				log(std::to_string(myServer.getPeerId() % 100) + " now close the socket to " + myKey.getPeerId() % 100);
			} else {
				// I'm not the one to kill one connection. I have to wait the close event from the other computer.
				sockWaitToDelete = sock;
			}

			log(std::to_string(myServer.getPeerId() % 100) + "fail to connect (already connected) to " + sock->remote_endpoint().port());
			return false;
		} else {
			log(std::to_string(myServer.getPeerId() % 100) + " win to connect with " + sock->remote_endpoint().port());
			// connect
			this->socket = sock;

			// get the sockId;
			// MessageId.use[MessageId.SET_SERVER_ID.id].emit(tempStreamOut, );
			// SetServerId.get().write(myServer.getId(), streamOut);
			changeState(PeerConnectionState::JUST_BORN, true);
			myServer.message().sendServerId(*this);
			// MyListenPort.get().write(this);
			myServer.message().sendListenPort(*this);
			myServer.getServerIdDb().requestPublicKey(*this);
			//streamOut.flush();

			boost::asio::socket_base::bytes_readable command(true);
			sock->io_control(command);
			std::size_t bytes_readable = command.get();

			log(std::to_string(myServer.getPeerId() % 100) + " " + myServer.getListenPort() + " can read " + bytes_readable);
			readMessage();
			//log(std::to_string(myServer.getPeerId() % 100) + " " + myServer.getListenPort() + " read id "
			//		+ getKey().getPeerId() + "("+(getKey().getPeerId()%100)+")");

			readMessage();
			//log(std::to_string(myServer.getPeerId() % 100) + " " + myServer.getListenPort() + " read port " + getKey().getPort());

			aliveAndSet.store(true);
			log(std::to_string(myServer.getPeerId() % 100) + " " + myServer.getListenPort() + " succeed to connect to "
					+ sock->remote_endpoint().port());
			return true;
		}
	}

	void Peer::startListen() {

		if (this->is_thread_running.load()) {
			msg(myServer.getPeerId() % 100 + " error, a listening thread is already started for addr "
				+ myKey.getAddress() + "...");
		} else {
			//FIXME
			std::thread relaunchThread([this]() {this->run(); });
			relaunchThread.detach();
			//this->run();
		}
	}



	//BUGGY!!!! used by ping.
	//TODO: write a branch to allow the read() to read that.
	//synchronized
	void Peer::writeMessagePriorityClear(uint8_t messageId, ByteBuff& message) {
		if (message.limit() == 0) {
			log(std::string("Warn : emit null message, id :") + int32_t(messageId)+" : "+messageId_to_string(messageId));
		}

		size_t encodedMsgLength = message.limit() - message.position();

		ByteBuff fullData;
		fullData.put(uint8_t(5))
			.put(uint8_t(5))
			.put(uint8_t(5))
			.put(uint8_t(5))
			.put(AbstractMessageManager::PRIORITY_CLEAR)
			.put(AbstractMessageManager::PRIORITY_CLEAR)
			.put(messageId);
		fullData.putInt(encodedMsgLength + 1)
			.putInt(encodedMsgLength + 1);
		fullData.put(message);
		fullData.flip();
		log(std::to_string( this->myServer.getPeerId() % 100 ) + " write socket: " + socket->is_open() + "\n");
		{ std::lock_guard lock_socket{ socket_read_write_mutex };
			boost::asio::write(*socket, boost::asio::buffer(fullData.raw_array(), fullData.limit()));
		}
		if (message.position() != 0) {
			msg(std::string("Warn, you want to send a buffer which is not rewinded : ") + message.position());
		}
		msg(std::string("WRITE PRIORITY MESSAGE : ") + messageId + " : " + (message.position() >= message.limit() ? "null" : ""+(message.limit() - message.position())));
		
	}

	//synchronized
	void Peer::writeMessage(uint8_t messageId, ByteBuff& message) {

		Sleep(1000 + this->myServer.getPeerId() % 200 + rand_char());
		
		if (message.limit() - message.position() <= 0) {
			msg(std::string("Warn : emit null message, id :") + int32_t(messageId) + " : " + messageId_to_string(messageId));
		}
		//try {

			//encode mesage
			//if (encoder == null) encoder = myServer.getServerIdDb().getSecretCipher(this, Cipher.ENCRYPT_MODE);
			uint8_t* encodedMsg = nullptr;
			size_t encodedMsgLength = 0;
			if (messageId > AbstractMessageManager::LAST_UNENCODED_MESSAGE) {
				if (hasState(PeerConnectionState::CONNECTED_W_AES)) {
					if (message.limit() - message.position() > 0) {
						//encodedMsg = encoder.doFinal(message.array(), message.position(), message.limit());
						// TODO: naive cipher: xor with a passphrase
						encodedMsg = message.raw_array() + message.position();
						encodedMsgLength = size_t(message.limit()) - message.position();
					}
				} else {
					std::cout<<"Error, tried to send a " << messageId << " message when we don't have a aes key!\n";
					return;
				}
			} else {
				if (message.limit() - message.position() > 0) {
					encodedMsg = message.raw_array() + message.position();
					encodedMsgLength = size_t(message.limit()) - message.position();
				}
			}

			//std::ostream& out = *this->streamOut;

			ByteBuff fullData;
			fullData.put(uint8_t(5))
				.put(uint8_t(5))
				.put(uint8_t(5))
				.put(uint8_t(5))
				.put(messageId)
				.put(messageId);
			fullData.putInt(encodedMsgLength)
				.putInt(encodedMsgLength);
			if (encodedMsgLength > 0) {
				fullData.put(encodedMsg, encodedMsgLength);
			}
			fullData.flip();
			boost::system::error_code error_write;
			{ std::lock_guard lock_socket{ socket_read_write_mutex };
				boost::asio::write(*socket, boost::asio::buffer(fullData.raw_array(), fullData.limit()), error_write);
			}
			if (encodedMsg != nullptr && message.position() != 0) {
				msg(std::string("Warn, you want to send a buffer which is not rewinded : ") + message.position());
			}
			if (error_write) {
				error(std::string("Error when writing :") + error_write.value() + " " + error_write.message());
			}
			log(std::to_string(myServer.getPeerId() % 100) + "->" + (getPeerId() % 100) +  " WRITE MESSAGE : " + messageId + " " + messageId_to_string(messageId) + " : " + (message.position() >= message.limit() ? std::string("null") : std::to_string(message.limit() - message.position())));
		//}
		//catch (std::exception e) {
		//	std::cerr << std::to_string(myServer.getPeerId() % 100) + "->" + (getPeerId() % 100)<<  " ERROR: " << e.what() << "\n";
		//	throw std::runtime_error(e.what());
		//}
	}

	void Peer::readMessage() {
		Sleep(100 + this->myServer.getPeerId() % 20 + rand_char()%100);
		try {
			boost::system::error_code error_code;
			// go to a pos where there are the two byte [5,5]
			size_t bytesRead = 0;
			uint8_t newByte;
			int nb5 = 0;
			do {

				{ std::lock_guard lock_socket{ socket_read_write_mutex };
					bytesRead = boost::asio::read(*socket, boost::asio::buffer(&newByte, 1), error_code);
				}
				if (bytesRead < 1 || error_code == boost::asio::error::eof) {
					error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + (" stream error: receive ") + bytesRead + "bytes, and an error: "
						+ error_code.value() + " => " + error_code.message());
					throw std::runtime_error("End of stream");
				}
				if (newByte == 5) {
					nb5++;
				} else {
					nb5 = 0;
					error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + (" stream error: receive ") + newByte + " instead of 5  ,peerid=" + getKey().getPeerId() % 100);
				}
			} while (nb5 < 4);

			// read messagetype
			//log(std::to_string( this->myServer.getPeerId() % 100 ) << " read type socket: " << socket->is_open() << "\n";
			uint8_t sameByte;
			{ std::lock_guard lock_socket{ socket_read_write_mutex };
				bytesRead = boost::asio::read(*socket, boost::asio::buffer(&newByte, 1), error_code);
				bytesRead = boost::asio::read(*socket, boost::asio::buffer(&sameByte, 1), error_code);
			}
			if (sameByte != newByte) {
				error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + (" Stream error: not same byte for message id : ") + newByte + " != " + sameByte);
				return;
			}
			//log(std::to_string(myServer.getPeerId() % 100) + " read message id :" + newByte+" "+ messageId_to_string(newByte));
			if (bytesRead < 1 || error_code == boost::asio::error::eof) {
				throw std::runtime_error("End of stream");
			}
			if (newByte >50) {
				log(std::string("error, receive byte: ") + newByte);
				return;
			}
			// read message
			try {
				ByteBuff buffIn(8);
				//streamIn->read((char*)buffIn.raw_array(), 8);
				{ std::lock_guard lock_socket{ socket_read_write_mutex };
					bytesRead = boost::asio::read(*socket, boost::asio::buffer(buffIn.raw_array(), 8), error_code);
				}
				int nbBytes = buffIn.getInt();
				int nbBytes2 = buffIn.getInt();
				if (nbBytes < 0) {
					error("Stream error: stream want me to read a negative number of bytes");
					return;
				}
				if (nbBytes != nbBytes2) {
					error(std::string("Stream error: not same number of bytes to read : ") + std::to_string(nbBytes) + " != " + std::to_string(nbBytes2));
					return;
				}
				//log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + ("Read ") + nbBytes + " from the stream");
				buffIn.limit(nbBytes).rewind();
				if (nbBytes > 0) {
					size_t pos = 0;
					//while mandatory, because it's not a buffered stream.
					while (pos < nbBytes) {
						//pos += streamIn->readsome((char*)buffIn.raw_array() + pos, nbBytes - pos);
						{ std::lock_guard lock_socket{ socket_read_write_mutex };
							pos += boost::asio::read(*socket, boost::asio::buffer(buffIn.raw_array() + pos, nbBytes - pos), error_code);
						}
						if (error_code == boost::asio::error::eof) {
							error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " Error: end of stream");
							throw std::runtime_error("End of stream");
						}
						if (error_code) {
							error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " Error: with code "+ error_code.value()+" => "+ error_code.message());
						}
					}
				}
				//decode mesage
				//if (decoder == nullptr) decoder = myServer.getServerIdDb().getSecretCipher(this, Cipher.DECRYPT_MODE);
				uint8_t* decodedMsg = nullptr;
				size_t decodedMsgLength = 0;
				if (newByte > AbstractMessageManager::LAST_UNENCODED_MESSAGE) {
					if (hasState(PeerConnectionState::CONNECTED_W_AES)) {
						if (buffIn.position() < buffIn.limit() && nbBytes > 0 && buffIn.limit() - buffIn.position() > 0) {
							//decodedMsg = decoder.doFinal(buffIn.raw_array(), buffIn.position(), buffIn.limit());
							// TODO: naive xor
							//put decoded message into the read buffer
							buffIn.reset().put(decodedMsg, decodedMsgLength).rewind();
						}
					} else {
						error(std::string("Error, try to receive a ") + messageId_to_string(newByte) + " message when we don't have a aes key!");
						return;
					}
				}//else : nothing to do, it's not encoded
				log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " Read message " + int32_t(newByte)+" : " + messageId_to_string(newByte));
				//use message
				if (newByte == AbstractMessageManager::GET_SERVER_ID) {
					// special case, give the peer object directly.
					log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + "read GET_SERVER_ID : now sending my peerId");
					myServer.message().sendServerId(*this);
				}
				if (newByte == AbstractMessageManager::SEND_SERVER_ID) {
					// special case, give the peer object directly.
					setPeerId(buffIn.getLong());
					//check if the cluster is ok
					uint64_t clusterId = uint64_t(buffIn.getLong());
					if (clusterId > 0 && myServer.getServerIdDb().getClusterId() < 0) {
						//set our cluster id
//						myServer.getServerIdDb().setClusterId(clusterId);
						throw std::runtime_error("Error, we haven't a clusterid !! Can we pick one from an existing network? : not anymore!");
						//						changeState(PeerConnectionState::HAS_ID, true);
					} else if (clusterId > 0 && myServer.getServerIdDb().getClusterId() != clusterId) {
						//error, not my cluster!
						std::cerr<< std::to_string(myServer.getPeerId() % 100) <<" Error, trying to connect with " << getPeerId() % 100 << " but his cluster is " << clusterId << " and mine is "
							<< myServer.getServerIdDb().getClusterId() << " => closing connection\n";
						this->close();
					} else {
						log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + "read SEND_SERVER_ID : get their id: "+getPeerId());
					}
				}
				if (newByte == AbstractMessageManager::GET_LISTEN_PORT) {
					// special case, give the peer object directly.
					log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + "read GET_LISTEN_PORT : now sending my port");
					myServer.message().sendListenPort(*this);
				}
				if (newByte == AbstractMessageManager::SEND_LISTEN_PORT) {
					// special case, give the peer object directly.
					setPort(buffIn.getInt());
					log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + "read SEND_LISTEN_PORT : get the distant port: "+getPort());
				}
				if (newByte == AbstractMessageManager::PRIORITY_CLEAR) {
					newByte = buffIn.get();
					log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + "read PRIORITY_CLEAR : "+ int32_t(newByte) + " : " + messageId_to_string(newByte));
				}

				// standard case, give the peer id. Our physical server should be able to retrieve us.
				myServer.propagateMessage(getPeerId(), (uint8_t)newByte, buffIn);

			}
			catch (std::exception e) {
				std::cerr << "ERROR: " << e.what() << "\n";
				throw std::runtime_error(e.what());
			}

		}
		catch (std::exception e) {
			std::cerr << "ERROR: " << e.what() << "\n";
			throw std::runtime_error(e.what());
		}
	}

	void Peer::close() {
		log(std::string("Closing ") + getKey().getPeerId() % 100);
		//log(std::to_string(Thread.getAllStackTraces().get(Thread.currentThread())));
		alive.store(false);
		changeState(PeerConnectionState::DEAD, false);
		try {
			if (sockWaitToDelete && sockWaitToDelete->is_open()) {
				sockWaitToDelete->shutdown(tcp::socket::shutdown_both);
				sockWaitToDelete->close();
			}
			if (socket && socket->is_open()) {
				socket->shutdown(tcp::socket::shutdown_both);
				socket->close();
			}
		}
		catch (std::exception e) {
			std::cerr << "ERROR: " << e.what() << "\n";
		}

	}

} // namespace supercloud
