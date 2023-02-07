#include "Peer.hpp"

#include <chrono>
#include <mutex>
#include <thread>

#include "utils/Utils.hpp"
#include "network/ClusterManager.hpp"
#include "PhysicalServer.hpp"
#include "IdentityManager.hpp"

//#define SLOW_NETWORK_FOR_DEBUG 1

namespace supercloud {
	std::string Peer::getLocalIPNetwork() const {
		if (!this->m_socket) return "";
		return this->m_socket->get_local_ip_network();
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
				<< getPeerId() % 100 + " : " << e1.what()<<"\n";
		}

		// check if i'm not a duplicate
		if (myServer.getPeersCopy().getAll(*this).size() > 1 || myServer.getPeerId() == getPeerId()) {
			// i'm a duplicate, kill me!
			myServer.removeExactPeer(this);
			return;
		}

		is_thread_running.store(false);

		close();

	}

	bool Peer::connect(std::shared_ptr<Socket> sock, bool initiated_by_me) {
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
			if (myServer.getPeerId() == getPeerId()) {
				log(std::to_string(myServer.getPeerId() % 100) + " i have the same id as " + getPeerId() % 100);
				// me and the other server must recreate a hash id.
				// compare ips to choose
				int winner = sock->local_endpoint().address() < (sock->remote_endpoint().address());
				if (sock->local_endpoint().address() == (sock->remote_endpoint().address()))
					winner = compare(sock->local_endpoint().port(), sock->remote_endpoint().port());
					//winner = Integer.compare(sock.getLocalPort(), sock.getPort());
				if (winner > 0) {
					iWin = true;
					// close();
					// myServer.rechooseId();
				} else {
					iWin = false;
				}
			} else if (myServer.getPeerId() > getPeerId()) {
				iWin = true;
			} else {
				iWin = false;
			}

			if (iWin) {
				// i can kill this new one.
				sock->close();
				log(std::to_string(myServer.getPeerId() % 100) + " now close the socket to " + getPeerId() % 100);
			} else {
				// I'm not the one to kill one connection. I have to wait the close event from the other computer.
				this->m_socket_wait_to_delete = sock;
				//TODO: 
			}

			log(std::to_string(myServer.getPeerId() % 100) + "fail to connect (already connected) to " + sock->remote_endpoint().port());
			return false;
		} else {
			log(std::to_string(myServer.getPeerId() % 100) + " win to connect with " + sock->remote_endpoint().port());
			// connect
			this->m_socket = sock;
			if (initiated_by_me) {
				std::lock_guard lock{ this->synchronize() };
				this->setState(this->getState() | ConnectionState::FROM_ME);
			}

			//Send the first message. Should start the connection pipeline via the ConnectionMessageManager
			// began directly by sending the id, instead of asking for it first. (emit the message that it's already been asked for)
			myServer.propagateMessage(this->ptr(), *UnnencryptedMessageType::GET_SERVER_ID, ByteBuff{});

			// read sendServerId from the other peer
			readMessage();

			// set that we are connecting
			aliveAndSet.store(true);
			//log(std::to_string(myServer.getPeerId() % 100) + " " + myServer.getListenPort() + " succeed to connect to "
			//		+ sock->remote_endpoint().port());
			return true;
		}
	}

	void Peer::startListen() {

		if (this->is_thread_running.load()) {
			msg(myServer.getPeerId() % 100 + " error, a listening thread is already started for addr "
				+ getIP() + "...");
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
		if (!m_socket || !m_socket->is_open())
			return;
		//if (message.limit() == 0) {
		//	log(std::string("Warn : emit null message, id :") + int32_t(messageId)+" : "+messageId_to_string(messageId));
		//}

		size_t encodedMsgLength = message.limit() - message.position();

		ByteBuff fullData;
		fullData.put(uint8_t(5))
			.put(uint8_t(5))
			.put(uint8_t(5))
			.put(uint8_t(5))
			.put(*UnnencryptedMessageType::PRIORITY_CLEAR)
			.put(*UnnencryptedMessageType::PRIORITY_CLEAR)
			.put(messageId);
		fullData.putInt(int32_t(encodedMsgLength + 1))
			.putInt(int32_t(encodedMsgLength + 1));
		fullData.put(message);
		fullData.flip();
		log(std::to_string( this->myServer.getPeerId() % 100 ) + " write socket: " + m_socket->is_open() + "\n");
		{ std::lock_guard lock_socket{ socket_read_write_mutex };
			if (isConnected() || alive.load()) { // don't write if the socket is just closed (shouldn't happen, just in case)
				m_socket->write(fullData);
			}
		}
		if (message.position() != 0) {
			msg(std::string("Warn, you want to send a buffer which is not rewinded : ") + message.position());
		}
		msg(std::string("WRITE PRIORITY MESSAGE : ") + messageId + " : " + (message.position() >= message.limit() ? "null" : ""+(message.limit() - message.position())));
		
	}

	//synchronized
	void Peer::writeMessage(uint8_t messageId, ByteBuff& message) {
		if (!m_socket || !m_socket->is_open())
			return;

#ifdef SLOW_NETWORK_FOR_DEBUG
		Sleep(1000 + this->myServer.getPeerId() % 200 + rand_u8());
#endif
		
		//if (message.limit() - message.position() <= 0) {
		//	msg(std::string("Warn : emit null message, id :") + int32_t(messageId) + " : " + messageId_to_string(messageId));
		//}
		//try {

			//encode mesage
			//if (encoder == null) encoder = myServer.getIdentityManager().getSecretCipher(this, Cipher.ENCRYPT_MODE);
			uint8_t* encodedMsg = nullptr;
			size_t encodedMsgLength = 0;
			if (messageId > *UnnencryptedMessageType::FIRST_ENCODED_MESSAGE) {
				if (this->isConnected()) {
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
			fullData.putInt(int32_t(encodedMsgLength))
				.putInt(int32_t(encodedMsgLength));
			if (encodedMsgLength > 0) {
				fullData.put(encodedMsg, encodedMsgLength);
			}
			fullData.flip();
			{ std::lock_guard lock_socket{ socket_read_write_mutex };
				if (isConnected() || alive.load()) { // don't write if the socket is just closed (shouldn't happen, just in case)
					m_socket->write(fullData);
				}
			}
			if (encodedMsg != nullptr && message.position() != 0) {
				msg(std::string("Warn, you want to send a buffer which is not rewinded : ") + message.position());
			}
			log(std::to_string(myServer.getPeerId() % 100) + "->" + (getPeerId() % 100) +  " WRITE MESSAGE : " + messageId + " " + messageId_to_string(messageId) 
				+ " : " + (message.position() >= message.limit() ? std::string("null") : std::to_string(message.limit() - message.position())));
		//}
		//catch (std::exception e) {
		//	std::cerr << std::to_string(myServer.getPeerId() % 100) + "->" + (getPeerId() % 100)<<  " ERROR: " << e.what() << "\n";
		//	throw std::runtime_error(e.what());
		//}
	}

	void Peer::readMessage() {
		if (!this->alive) { return; }
#ifdef SLOW_NETWORK_FOR_DEBUG
		Sleep(100 + this->myServer.getPeerId() % 20 + rand_u8()%100);
#endif
		try {
			// go to a pos where there are the two byte [5,5]
			size_t size_read = 0;
			ByteBuff buffer_one_byte{ 1 };
			uint8_t byte_read = 0;
			int nb5 = 0;
			do {

				{ std::lock_guard lock_socket{ socket_read_write_mutex };
					size_read = m_socket->read(buffer_one_byte.rewind());
					byte_read = buffer_one_byte.flip().get();
				}
				if (size_read < 1) {
					error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + (" stream error: receive ") + size_read + "bytes");
					throw read_error("End of stream");
				}
				if (byte_read == 5) {
					nb5++;
				} else {
					nb5 = 0;
					error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + (" stream error: receive ") + byte_read + " instead of 5  ,peerid=" + getPeerId() % 100);
				}
				if (!this->alive) { return; }
			} while (nb5 < 4);

			// read messagetype
			//log(std::to_string( this->myServer.getPeerId() % 100 ) << " read type socket: " << socket->is_open() << "\n";
			uint8_t same_byte;
			{ std::lock_guard lock_socket{ socket_read_write_mutex };
				size_read = m_socket->read(buffer_one_byte.rewind());
				byte_read = buffer_one_byte.flip().get();
				if (!this->alive) { return; }
				size_read = m_socket->read(buffer_one_byte.rewind());
				same_byte = buffer_one_byte.flip().get();
			}
			if (same_byte != byte_read) {
				error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + (" Stream error: not same byte for message id : ") + byte_read + " != " + same_byte);
				return;
			}
			//log(std::to_string(myServer.getPeerId() % 100) + " read message id :" + newByte+" "+ messageId_to_string(newByte));
			if (size_read < 1) {
				throw read_error("End of stream");
			}
			if (!this->alive) { return; }
			if (byte_read >50) {
				log(std::string("error, receive a too big message id: ") + byte_read);
				return;
			}
			// read message
			ByteBuff buff_message;
			size_t message_size;
			try {
				ByteBuff buff_8bytes(8);
				//streamIn->read((char*)buffIn.raw_array(), 8);
				{ std::lock_guard lock_socket{ socket_read_write_mutex };
					size_read = m_socket->read(buff_8bytes);
					buff_8bytes.flip();
					if (!this->alive) { return; }
				}
				message_size = buff_8bytes.getInt();
				size_t message_size2 = buff_8bytes.getInt();
				if (message_size < 0) {
					error("Stream error: stream want me to read a negative number of bytes");
					return;
				}
				if (message_size != message_size2) {
					error(std::string("Stream error: not same number of bytes to read : ") + std::to_string(message_size) + " != " + std::to_string(message_size2));
					return;
				}
				//log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + ("Read ") + nbBytes + " from the stream");
				buff_message.limit(message_size).rewind();
				if (message_size > 0) {
					size_t pos = 0;
					//while mandatory, because it's not a buffered stream.
					while (pos < message_size) {
						//pos += streamIn->readsome((char*)buffIn.raw_array() + pos, nbBytes - pos);
						{ std::lock_guard lock_socket{ socket_read_write_mutex };
							pos += m_socket->read(buff_message);
							buff_message.flip();
						}
						if (!this->alive) { return; }
					}
				}
			}
			catch (std::exception e) {
				error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " readMessage ERROR in socket read: " + e.what());
				throw std::runtime_error(e.what());
			}
			try{
				//decode mesage
				//if (decoder == nullptr) decoder = myServer.getIdentityManager().getSecretCipher(this, Cipher.DECRYPT_MODE);
				uint8_t* decodedMsg = nullptr;
				size_t decodedMsgLength = 0;
				if (byte_read > *UnnencryptedMessageType::FIRST_ENCODED_MESSAGE) {
					if (this->isConnected()) {
						if (buff_message.position() < buff_message.limit() && message_size > 0 && buff_message.limit() - buff_message.position() > 0) {
							//decodedMsg = decoder.doFinal(buffIn.raw_array(), buffIn.position(), buffIn.limit());
							// TODO: naive xor
							//put decoded message into the read buffer
							buff_message.reset().put(decodedMsg, decodedMsgLength).rewind();
						}
					} else {
						error(std::string("Error, try to receive a ") + messageId_to_string(byte_read) + " message when we don't have a aes key!");
						return;
					}
				}//else : nothing to do, it's not encoded
				log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " Read message " + int32_t(byte_read)+" : " + messageId_to_string(byte_read));
				//use message
				if (byte_read == *UnnencryptedMessageType::PRIORITY_CLEAR) {
					byte_read = buff_message.get();
					log(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " read PRIORITY_CLEAR : "+ int32_t(byte_read) + " : " + messageId_to_string(byte_read));
				}
				if (!this->alive) { return; }

				// standard case, give the peer id. Our physical server should be able to retrieve us.
				myServer.propagateMessage(this->ptr(), (uint8_t)byte_read, buff_message);

			}
			catch (std::exception e) {
				error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " readMessage ERROR in message propagation: " + e.what() );
				throw std::runtime_error(e.what());
			}

		}
		catch (std::exception e) {
			error(std::to_string(myServer.getPeerId() % 100) + "<-" + (getPeerId() % 100) + " readMessage FATAL ERROR (closing socket): " + e.what());
			close();
		}
	}

	void Peer::close() {
		log(std::string("Closing ") + getPeerId() % 100);
		//log(std::to_string(Thread.getAllStackTraces().get(Thread.currentThread())));
		//set as not alive
		bool was_alive = alive.exchange(false);
		aliveAndSet.store(false);
		// notify the listener that this connection is lost.
		// (so they still have a chance to emit a last message before closure) 
		if (was_alive) {
			this->myServer.propagateMessage(this->ptr(), *UnnencryptedMessageType::CONNECTION_CLOSED, ByteBuff{});
		}
		//update the peer connection status (track the connectivity of a single peer)
		// now other manager can emit message to the peer, and be notified by timers.
		{
			std::lock_guard lock{ synchronize() };
			Peer::ConnectionState state = getState();
			if (0 != (state & Peer::ConnectionState::CONNECTED)) {
				//add "was connected" flag if disconnected after full connection.
				state |= Peer::ConnectionState::DISCONNECTED;
			}
			//remove connect status
			state &= ~Peer::ConnectionState::CONNECTING;
			state &= ~Peer::ConnectionState::CONNECTED;
			setState(state);

		}
		//stay connected until the CONNECTION_CLOSED is finished, just before deleting the sockets
		{ std::lock_guard lock_socket{ socket_read_write_mutex };

			//close the socket
			try {
				if (m_socket_wait_to_delete && m_socket_wait_to_delete->is_open()) {
					m_socket_wait_to_delete->close();
				}
				if (m_socket && m_socket->is_open()) {
					m_socket->close();
				}
			}
			catch (std::exception e) {
				std::cerr << "ERROR: " << e.what() << "\n";
			}
		}

	}

} // namespace supercloud
