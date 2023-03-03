
#include "IdentityManager.hpp"
#include "PhysicalServer.hpp"
#include "utils/Parameters.hpp"


#include <cassert>
#include <chrono>
#include <filesystem>

namespace supercloud{


	void IdentityManager::createNewPublicKey() {
		//try {
		//	KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
		//	SecureRandom random = SecureRandom.getInstanceStrong();
		//	random.setSeed(System.currentTimeMillis());
		//	generator.initialize(1024, random);
		//	log(std::to_string(serv.getPeerId() % 100) + " generate rsa"+"\n";
		//	KeyPair pair = generator.generateKeyPair();
		//	log(std::to_string(serv.getPeerId() % 100) + " generate rsa: ended"+"\n";
		//	privateKey = pair.getPrivate();
		//	publicKey = pair.getPublic();

		//	log(std::to_string(serv.getPeerId() % 100) + " Priv key algo : " + createPrivKey(privateKey.getEncoded()).getAlgorithm()+"\n";
		//}
		//catch (NoSuchAlgorithmException e) {
		//	throw new RuntimeException(e);
		//}
		//TODO
		this->publicKey.clear();
		for (int i = 0; i < 16; i++) {
			this->publicKey.push_back(rand_u8());
		}
		this->privateKey = this->publicKey;
		// this->publicKey is just a cache for m_peer_2_peerdata[getComputerId()].rsa_public_key
		{std::lock_guard lock(m_peer_data_mutex);
			assert(m_peer_2_peerdata.find(m_myself) != m_peer_2_peerdata.end());
			m_peer_2_peerdata[this->m_myself].rsa_public_key = this->publicKey;
		}
	}

	//PrivateKey IdentityManager::createPrivKey(const std::vector<uint8_t>& datas) {
	//	//try {
	//	//	KeyFactory keyFactory = KeyFactory.getInstance("RSA");
	//	//	PKCS8EncodedKeySpec bobPrivKeySpec = new PKCS8EncodedKeySpec(datas);
	//	//	return keyFactory.generatePrivate(bobPrivKeySpec);
	//	//}
	//	//catch (NoSuchAlgorithmException | InvalidKeySpecException e) {
	//	//	throw new RuntimeException(e);
	//	//}
	//	return this->privateKey;
	//}

	PeerPtr IdentityManager::getLoadedPeer(ComputerId cid) {
		std::lock_guard lock{ m_loaded_peers_mutex };
		for (PeerPtr& peer : this->m_loaded_peers) {
			if (peer->getComputerId() == cid)
				return peer;
		}
		return {};
	}

	IdentityManager::PeerData IdentityManager::getPeerData(PeerPtr peer) const {
		std::lock_guard lock(m_peer_data_mutex);
		auto it = m_peer_2_peerdata.find(peer);
		if (it != m_peer_2_peerdata.end()) {
			assert(it->second.peer == peer);
			return it->second;
		}
		assert(false);
		return PeerData{};
	}

	bool IdentityManager::hasPeerData(PeerPtr peer) const {
		std::lock_guard lock(m_peer_data_mutex);
		return m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end();
	}

	bool IdentityManager::setPeerData(const PeerData& original, const PeerData& new_data) {
		std::lock_guard lock(m_peer_data_mutex);
		assert(original.peer == new_data.peer);
		if (!original.peer) return false;
		auto it = m_peer_2_peerdata.find(original.peer);
		if (it != m_peer_2_peerdata.end()) {
			if (it->second == original) {
				it->second = new_data;
			}
		}
		return true;
	}

	void IdentityManager::create_from_install_info() {
		//try to get data from the install params (if any)
		if (!m_install_parameters) return;
		Parameters& paramsNet = *m_install_parameters;
		if (!paramsNet.has("ClusterId")) {
			//no previous data, load nothing plz.
			log("No install data, please construct them!!!\n");
			createNewPublicKey();
		} else {

			if (paramsNet.has("ClusterId")) {
				log(std::string("set ClusterId to ") + uint64_t(paramsNet.getLong("ClusterId")) + " (= " + paramsNet.get("ClusterId") + " )"
					+ " => " + (uint64_t(paramsNet.getLong("ClusterId")) % 100) + "\n");
				clusterId = paramsNet.getLong("ClusterId");
			}
			if (paramsNet.has("ClusterPassphrase")) {
				log(std::string("set passphrase to ") + paramsNet.get("ClusterPassphrase") + "\n");
				passphrase = paramsNet.get("ClusterPassphrase");
			}
			if (paramsNet.has("PeerIp") && paramsNet.has("PeerPort")) {
				PeerPtr p = Peer::create(serv, paramsNet.get("PeerIp"), uint16_t(paramsNet.getInt("PeerPort")), Peer::ConnectionState::ENTRY_POINT);
				{ std::lock_guard lock{ m_loaded_peers_mutex };
					m_loaded_peers.push_back(p);
					for (int i = 0; i < m_loaded_peers.size(); i++)
						for (int j = i + 1; j < m_loaded_peers.size();j++)
							assert(m_loaded_peers[i].get() != m_loaded_peers[j].get());
				}
				{std::lock_guard lock(m_peer_data_mutex);
					//create associated peerdata
					m_peer_2_peerdata[p].peer = p;
					m_peer_2_peerdata[p].private_interface = PeerConnection{ paramsNet.get("PeerIp"), uint16_t(paramsNet.getInt("PeerPort")), true, {} };
				}

			}
			if (!paramsNet.has("PubKey") || !paramsNet.has("PrivKey")) {
				log("no priv/pub key defined, creating some random ones\n");
				createNewPublicKey();
			} else {
				//TODO : get key from unicode string
				//publicKey = paramsNet.;
				throw operation_not_implemented("pub/priv load key is not implemented yet");
			}
		}

		//but create new data!
		requestSave();

		//remove ClusterPwd in the conf file (to encrypt it, it doesn't protect it but just obfuscate a little)
		if (paramsNet.has("ClusterPwd")) {
			// it's not very useful, so for the beta phase, let it commented
			//TODO uncomment this for release
//					paramsNet.set("ClusterPwd","deleted");
		}
	}

	void IdentityManager::load() {

		PeerList loaded_peers_copy;
		Parameters& params_server_db = *m_parameters;
		params_server_db.load();

		//test if loaded
		if (!params_server_db.has("clusterId")) {
			create_from_install_info();
			return;
		}

		this->clusterId = params_server_db.getLong("clusterId");
		this->myComputerIdState = (ComputerIdState)params_server_db.getInt("computerIdState");
		Peer::ComputerIdSetter::setComputerId(*this->m_myself, (ComputerId)params_server_db.getInt("computerId"));
		uint64_t old_peer_id = (uint64_t)params_server_db.getLong("peerId", NO_PEER_ID);
		if (this->m_myself->getPeerId() == 0 || this->m_myself->getPeerId() == NO_PEER_ID) {
			std::lock_guard lock{ this->m_myself->synchronize() };
			this->m_myself->setPeerId(old_peer_id);
			this->m_myself->setState(this->m_myself->getState() | Peer::ConnectionState::DATABASE);
		}
		this->passphrase = params_server_db.get("passphrase");
		this->publicKey = from_hex(params_server_db.get("publicKey"));
		this->privateKey = from_hex(params_server_db.get("privateKey"));
		assert(publicKey.size() != 0);
		{std::lock_guard lock(m_peer_data_mutex);
			assert(m_peer_2_peerdata.find(m_myself) != m_peer_2_peerdata.end());
			m_peer_2_peerdata[this->m_myself].rsa_public_key = this->publicKey;
		}
		size_t nbPeers = size_t(params_server_db.getInt("nbPeers", 0));
		//FIXME: isn't it more safe to ock the two locks? (but there is then a deadlock possibility...). Or maybe have only one lock?
		{std::lock_guard lock(m_peer_data_mutex);
			for (size_t i = 0; i < nbPeers; i++) {
				ComputerId dist_id = ComputerId(params_server_db.getInt("peer" + std::to_string(i) + "_cid"));
				PeerPtr p = Peer::create(serv, "", 0, Peer::ConnectionState::DATABASE);
				Peer::ComputerIdSetter::setComputerId(*p,dist_id);
				assert(dist_id != 0);
				assert(dist_id != NO_COMPUTER_ID);
				loaded_peers_copy.push_back(p);
				assert(m_peer_2_peerdata.find(p) != m_peer_2_peerdata.end());
				//create associated peerdata
				PeerData& data = m_peer_2_peerdata[p];
				data.rsa_public_key = from_hex(params_server_db.get("peer" + std::to_string(i) + "_publicKey"));
				const std::string& dist_ip = params_server_db.get("peer" + std::to_string(i) + "_ip");
				uint16_t dist_port = uint16_t(params_server_db.getInt("peer" + std::to_string(i) + "_port"));
				bool dist_conn_by_me = params_server_db.getBool("peer" + std::to_string(i) + "_succeed");
				std::string dist_conn_from = params_server_db.get("peer" + std::to_string(i) + "_from");
				//TODO save/get the "from";
				if (dist_ip != "" || dist_port != 0) {
					data.private_interface = PeerConnection{ dist_ip , dist_port, dist_conn_by_me, split(dist_conn_from, '$') };
				} else {
					data.private_interface.reset();
				}
				data.peer = p;
				for (size_t idx_net2conn = 0; params_server_db.has("peer" + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_addr"); ++idx_net2conn) {
					PeerConnection connection = data.public_interfaces.emplace_back();
					connection.address = params_server_db.get("peer" + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_addr");
					connection.port = (uint16_t)params_server_db.getInt("peer" + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_port");
					connection.first_hand_information = params_server_db.getBool("peer" + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_succeed");
					connection.success_from = split(params_server_db.get("peer" + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_from"), '$');
				}
			}
		}
		{ std::lock_guard lock{ m_loaded_peers_mutex };
			m_loaded_peers = loaded_peers_copy;
		}
	}

	void IdentityManager::save() const {
		Parameters& params_server_db = *m_parameters;
		params_server_db.setLong("clusterId", this->clusterId);
		params_server_db.setInt("computerId", this->m_myself->getComputerId());
		if (this->m_myself->getPeerId() != 0 && this->m_myself->getPeerId() != NO_PEER_ID) {
			params_server_db.setLong("peerId", this->m_myself->getPeerId());
		}
		params_server_db.set("passphrase", this->passphrase);
		params_server_db.set("publicKey", to_hex(this->publicKey));
		params_server_db.set("privateKey", to_hex(this->privateKey));
		PeerList loaded_peers_copy;
		{ std::lock_guard lockpeers{ m_loaded_peers_mutex };
			loaded_peers_copy = this->m_loaded_peers;
		}
		params_server_db.setInt("nbPeers", int32_t(loaded_peers_copy.size()));
		size_t peer_idx = 0;
		{std::lock_guard<std::mutex> lockpdata(this->m_peer_data_mutex);
			for (const PeerPtr& peer : loaded_peers_copy) {
				auto found = m_peer_2_peerdata.find(peer);
				if (found != m_peer_2_peerdata.end() && found->second.peer) {
					params_server_db.setInt("peer" + std::to_string(peer_idx) + "_cid", peer->getComputerId());
					const PeerData& data = found->second;
					params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", to_hex(data.rsa_public_key));
					if (data.private_interface) {
						params_server_db.set("peer" + std::to_string(peer_idx) + "_ip", data.private_interface->address);
						params_server_db.setInt("peer" + std::to_string(peer_idx) + "_port", data.private_interface->port);
						params_server_db.setBool("peer" + std::to_string(peer_idx) + "_succeed", data.private_interface->first_hand_information);
						params_server_db.set("peer" + std::to_string(peer_idx) + "_from", concatenate(data.private_interface->success_from, '$'));
					}
					size_t idx_conn = 0;
					for (const PeerConnection& net_interface : data.public_interfaces) {
						params_server_db.set("peer" + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_addr", net_interface.address);
						params_server_db.setInt("peer" + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_port", net_interface.port);
						params_server_db.setBool("peer" + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_success", net_interface.first_hand_information);
						params_server_db.set("peer" + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_from", concatenate(net_interface.success_from, '$'));
						++idx_conn;
					}
				} else {
					params_server_db.set("peer" + std::to_string(peer_idx) + "_ERROR", "ERROR, CANT FIND DATA");
				}
				//relance
				peer_idx++;
			}
		}

		params_server_db.save();
	}

	void IdentityManager::fusionWithConnectedPeer(PeerPtr connected_peer) {
		assert(connected_peer->isAlive());
		assert(connected_peer->getComputerId() != 0);
		assert(connected_peer->getComputerId() != NO_COMPUTER_ID);
		assert(connected_peer->getPeerId() != 0);
		assert(connected_peer->getPeerId() != NO_COMPUTER_ID);
		assert(connected_peer->getState() & Peer::ConnectionState::CONNECTING);
		PeerPtr deletedPeer = nullptr;
		{ std::lock_guard lock{ m_loaded_peers_mutex };
			bool already_inside = false;
			foreach(peer_it, m_loaded_peers) {
				if ((*peer_it)->getComputerId() == connected_peer->getComputerId()) {
					assert((*peer_it) != connected_peer || !already_inside);
					if ((*peer_it) == connected_peer && !already_inside) {
						already_inside = true;
					} else {
						deletedPeer = (*peer_it);
						//fusionPeers(connectedPeer, (*peer_it)); // nothing to fusion
						peer_it.erase();
					}
				}
			}
			//add it to loaded peers
			if (!already_inside) {
				m_loaded_peers.push_back(connected_peer);
			}
			for (int i = 0; i < m_loaded_peers.size(); i++)
				for (int j = i + 1; j < m_loaded_peers.size();j++)
					assert(m_loaded_peers[i].get() != m_loaded_peers[j].get());
		}
		//update state
		if (deletedPeer) {
			std::lock_guard lock_peer{ deletedPeer->synchronize() };
			deletedPeer->setState((deletedPeer->getState() & ~Peer::DATABASE) | Peer::TEMPORARY);

		}
		//replace also in the peerdata
		if (deletedPeer) {
			std::lock_guard lock(m_peer_data_mutex);
			m_peer_2_peerdata.erase(deletedPeer);
		}
		assert(m_peer_2_peerdata.find(connected_peer) != m_peer_2_peerdata.end());
		assert(m_peer_2_peerdata[connected_peer].peer == connected_peer);
	}

	PeerPtr IdentityManager::addNewPeer(ComputerId computerId, const PeerData& data) {
		assert(computerId != 0);
		assert(computerId != NO_COMPUTER_ID);
		bool not_found = false;
		PeerPtr new_peer;
		{std::lock_guard lock{ m_loaded_peers_mutex };
			//ensure it's not here
			auto search = std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [computerId](PeerPtr& p) {return computerId == p->getComputerId(); });
			not_found = m_loaded_peers.end() == search;
			if (not_found) {
				new_peer = Peer::create(serv, "", 0, Peer::ConnectionState::DATABASE);
				Peer::ComputerIdSetter::setComputerId(*new_peer,computerId);
				m_loaded_peers.push_back(new_peer);
				for (int i = 0; i < m_loaded_peers.size(); i++)
					for (int j = i + 1; j < m_loaded_peers.size();j++)
						assert(m_loaded_peers[i].get() != m_loaded_peers[j].get());
			} else {
				new_peer = *search;
			}
		}
		if (not_found) {
			std::lock_guard lock(m_peer_data_mutex);
			//create associated peerdata
			assert(m_peer_2_peerdata.find(new_peer) == m_peer_2_peerdata.end());
			m_peer_2_peerdata[new_peer] = data;
			m_peer_2_peerdata[new_peer].peer = new_peer;
		} else {
			//what to do with data?
			assert(false);
		}
		assert(m_peer_2_peerdata.find(new_peer) != m_peer_2_peerdata.end());
		assert(m_peer_2_peerdata[new_peer].peer == new_peer);
		return new_peer;
	}

	//send our public key to the peer
	void IdentityManager::sendPublicKey(PeerPtr peer) {
		//std::cout+ &serv + " (sendPublicKey) emit GET_SERVER_PUBLIC_KEY to " + peer+"\n";
		log(std::to_string(serv.getPeerId() % 100) + " (sendPublicKey) emit SEND_SERVER_PUBLIC_KEY to " + peer->getPeerId() % 100+"\n");
		//ensure we have create a peer data for this peer
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = m_peer_2_peerdata.find(peer); it == m_peer_2_peerdata.end()) {
				assert(peer->getState() & Peer::TEMPORARY);
				m_peer_2_peerdata[peer].peer = peer;
			}
		}

		ByteBuff buff;
		//my pub key
		//uint8_t[] encodedPubKey = publicKey.getEncoded();
		const uint8_t* encodedPubKey = reinterpret_cast<const uint8_t*>(&publicKey[0]);
		size_t encodedPubKey_size = publicKey.size();
		buff.putSize(encodedPubKey_size).put(encodedPubKey, size_t(0), encodedPubKey_size);
		//send packet
		peer->writeMessage(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, buff.flip());

	}

	void IdentityManager::receivePublicKey(PeerPtr sender, const ByteBuff& buffIn) {
		//log(std::to_string(serv.getPeerId() % 100) + " (receivePublicKey) receive SEND_SERVER_PUBLIC_KEY from " + sender->getPeerId() % 100+"\n");
		//ensure we have create a peer data for this peer
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = m_peer_2_peerdata.find(sender); it == m_peer_2_peerdata.end()) {
				assert(sender->getState() & Peer::TEMPORARY);
				m_peer_2_peerdata[sender].peer = sender;
			}
		}
		
		//get pub Key
		size_t nb_bytes = buffIn.getSize();
		PublicKey dist_public_key = buffIn.get(nb_bytes);
		//X509EncodedKeySpec bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//KeyFactory keyFactory = KeyFactory.getInstance("RSA");
		//PublicKey distPublicKey = keyFactory.generatePublic(bobPubKeySpec);
		{ std::lock_guard lock(tempPubKey_mutex);
			tempPubKey[sender->getPeerId()] = dist_public_key;
			//				std::cout+serv.getId()%100+" (receivePublicKey) peer "+p.getConnectionId()%100+" has now a pub key of "+tempPubKey.get(p.getConnectionId())+"\n";
		}
	}

	std::string IdentityManager::createMessageForIdentityCheck(Peer& peer, bool forceNewOne) {
		const auto& it_msg = m_pid_2_emitted_msg.find(peer.getPeerId());
		std::string msg;
		if (it_msg == m_pid_2_emitted_msg.end() || forceNewOne) {
			if (it_msg == m_pid_2_emitted_msg.end()) {
				msg = to_hex_str(rand_u63());
			}
			m_pid_2_emitted_msg[peer.getPeerId()] = msg;
			//todo: encrypt it with our public key.
		} else {
			msg = it_msg->second;
			log(std::string(" (createMessageForIdentityCheck) We already emit a request for indentity to ") + peer.getPeerId() % 100 + " with message " + m_pid_2_emitted_msg[peer.getPeerId()]+"\n");
		}
		return msg;
	}

	//send our public key to the peer, with the message encoded
	IdentityManager::Identityresult IdentityManager::sendIdentity(PeerPtr peer, const std::string& messageToEncrypt, bool isRequest) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") emit " + (isRequest ? "GET_IDENTITY" : "SEND_IDENTITY") + " to " + peer->getPeerId() % 100 + ", with message 2encrypt : " + messageToEncrypt+"\n");
		//check if we have the public key of this peer
		PublicKey theirPubKey;
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer->getPeerId());
			if (it_theirPubKey == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") i don't have public key! why are you doing that? Request his one!"+"\n");
				return IdentityManager::Identityresult::NO_PUB;
			} else {
				theirPubKey = it_theirPubKey->second;
			}
		}

		//don't emit myId if it's NO_COMPUTER_ID (-1)
		if (getComputerId() == NO_COMPUTER_ID) {
			log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") but i have null id!! \n");
			ByteBuff buff;
			buff.serializeComputerId(NO_COMPUTER_ID);
			//send packet
			if (!isRequest) {
				log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") so i return '-1' \n");
				peer->writeMessage(*UnnencryptedMessageType::SEND_VERIFY_IDENTITY, buff.flip());
			}
			return IdentityManager::Identityresult::BAD;
		}

		//TODO: encrypt it also with his public key if we have it?
		ByteBuff buff;
		//encode msg
		//Cipher cipher = Cipher.getInstance("RSA");
		//cipher.init(Cipher.ENCRYPT_MODE, privateKey);
		//ByteBuff buffEncoded = blockCipher(new ByteBuff().serializeComputerId(myComputerId).putUTF8(messageToEncrypt).putUTF8(passphrase).flip().toArray(), Cipher.ENCRYPT_MODE, cipher);
		ByteBuff buffEncoded;
		buffEncoded.serializeComputerId(getComputerId())
			.putUTF8(messageToEncrypt)
			.putUTF8(passphrase).flip();

		//encrypt more
		//cipher.init(Cipher.ENCRYPT_MODE, theirPubKey);
		//buffEncoded = blockCipher(buffEncoded.array(), Cipher.ENCRYPT_MODE, cipher);

		buff.putSize(buffEncoded.limit()).put(buffEncoded);
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") message : " + messageToEncrypt+"\n");
		//log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") Encryptmessage : " + Arrays.tostd::string(buffEncoded.rewind().array())+"\n");

		//send packet
		peer->writeMessage(isRequest ? *UnnencryptedMessageType::GET_VERIFY_IDENTITY : *UnnencryptedMessageType::SEND_VERIFY_IDENTITY, buff.flip());

		return IdentityManager::Identityresult::OK;
	}

	ByteBuff IdentityManager::getIdentityDecodedMessage(const PublicKey& key, const ByteBuff& buffIn) {
		//get msg
		size_t nbBytes = buffIn.getSize();
		/*byte[] dataIn = new byte[nbBytes];
		buffIn.get(dataIn, 0, nbBytes);
		Cipher cipher = Cipher.getInstance("RSA");
		cipher.init(Cipher.DECRYPT_MODE, privateKey);
		ByteBuff buffDecoded = blockCipher(dataIn, Cipher.DECRYPT_MODE, cipher);
		cipher.init(Cipher.DECRYPT_MODE, key);
		buffDecoded = blockCipher(buffDecoded.array(), Cipher.DECRYPT_MODE, cipher);*/
		ByteBuff buffDecoded;
		std::vector<uint8_t> data = buffIn.get(nbBytes);
		buffDecoded.put(data).flip();
		return buffDecoded;
	}

	IdentityManager::Identityresult IdentityManager::answerIdentity(PeerPtr peer, const ByteBuff& buffIn) {
		//log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) receive GET_IDENTITY to " + peer->getPeerId() % 100+"\n");


		PublicKey theirPubKey;
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer->getPeerId());
			if (it_theirPubKey == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity)i don't have public key! why are you doing that? Request his one!"+"\n");
				return Identityresult::NO_PUB;
			} else {
				theirPubKey = it_theirPubKey->second;
			}
		}

		//check if the other side is ok and sent us something (it's a pre-emptive check)
		if (buffIn.limit() == 2 && buffIn.getShort() == -1) {
			error(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) he ask use somethign whithout a message! it's crazy!!! " + peer->getComputerId() + " : " + peer->getPeerId() % 100+"\n");
			//not ready, maybe he didn't choose his computerid yet?
			//we have to ask him a bit later.
			//ping() is already in charge of that
			return Identityresult::BAD;
		}

		ByteBuff buffDecoded = getIdentityDecodedMessage(theirPubKey, buffIn);
		ComputerId unverifiedCompId = buffDecoded.deserializeComputerId(); ///osef dist_cid = because we can't verify it.
		std::string msgDecoded = buffDecoded.getUTF8();
		std::string theirPwd = buffDecoded.getUTF8();
		if (theirPwd != (passphrase)) {
			log(std::string("Error: peer ") + peer->getPeerId() % 100 + " : ?" + unverifiedCompId + "? has not the same pwd as us."+"\n");
			peer->close(); //TODO: verify that Physical server don't revive it.
			//TODO : remove it from possible server list
			return Identityresult::BAD;
		} else {
			log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) msgDecoded = " + msgDecoded+"\n");
			//same pwd, continue
			sendIdentity(peer, msgDecoded, false);
			return Identityresult::OK;
		}
	}

	IdentityManager::Identityresult IdentityManager::receiveIdentity(PeerPtr peer, const ByteBuff& message) {
		//log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) receive SEND_IDENTITY to " + peer->getPeerId() % 100+"\n");


		PublicKey theirPubKey;
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer->getPeerId());
			if (it_theirPubKey == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity)i don't have public key! why are you doing that? Request his one!"+"\n");
				return Identityresult::NO_PUB;
			} else {
				theirPubKey = it_theirPubKey->second;
			}
		}

		//check if the other side is ok and sent us something
		if (message.limit() == 2 && message.getShort() == -1) {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) he doesn't want to give us his identity! " + peer->getComputerId() + " : " + peer->getPeerId() % 100+"\n");
			//not ready, maybe he didn't choose his computerid yet?
			//we have to ask him a bit later.
			//ping() is already in charge of that
			return Identityresult::BAD;
		}

		ByteBuff buffDecoded = getIdentityDecodedMessage(theirPubKey, message);

		//data extracted
		ComputerId dist_cid = buffDecoded.deserializeComputerId();
		std::string msgDecoded = buffDecoded.getUTF8();
		std::string theirPwd = buffDecoded.getUTF8();


		if (dist_cid == NO_COMPUTER_ID || m_pid_2_emitted_msg.find(peer->getPeerId()) == m_pid_2_emitted_msg.end()) {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) BAD receive computerid " + dist_cid + " and i " + (m_pid_2_emitted_msg.find(peer->getPeerId()) != m_pid_2_emitted_msg.end()) + " emmitted a message"+"\n");
			//the other peer doesn't have an computerid (yet)
			if(m_pid_2_emitted_msg.find(peer->getPeerId()) != m_pid_2_emitted_msg.end())
				m_pid_2_emitted_msg.erase(peer->getPeerId());
			return Identityresult::BAD;
		} else {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) GOOD receive computerid " + dist_cid + " and i " + (m_pid_2_emitted_msg.find(peer->getPeerId()) != m_pid_2_emitted_msg.end()) + " emmitted a message"+"\n");
		}
		if (theirPwd != (passphrase)) {
			log(std::string("Error: peer ") + peer->getPeerId() % 100 + " : " + dist_cid + " has not the same pwd as us (2)."+"\n");
			peer->close(); //TODO: verify that Physical server don't revive it.
			//TODO : remove it from possible server list
			return Identityresult::BAD;
		}

		log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) i have sent to " + peer->getPeerId() % 100 + " the message " + (m_pid_2_emitted_msg.find(peer->getPeerId())->second) + " to encode. i have received " + msgDecoded + " !!!"+"\n");

		//check if this message is inside our peer
		if ( (m_pid_2_emitted_msg.find(peer->getPeerId())->second) == (msgDecoded)) {

			bool found = false;
			PublicKey found_public_key;
			//check if the public key is the same
			{ std::lock_guard lock(m_peer_data_mutex);
				auto search = this->m_peer_2_peerdata.find(peer);
				found = search != this->m_peer_2_peerdata.end();
				if (found) { found_public_key = search->second.rsa_public_key; }
			}
			// if not already in the storage and the key is valid, then accept & store it.
			if (!found || found_public_key.empty() && dist_cid > 0) {
				std::string rsa_log_str;
				for (uint8_t c : theirPubKey) {
					rsa_log_str += u8_hex(c);
				}
				log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) assign new publickey "+ theirPubKey.size() + "{" + rsa_log_str + "} for computerid " + dist_cid + " , their pid=" + peer->getPeerId() % 100+"\n");
				//validate this peer
				{ std::lock_guard lock(m_peer_data_mutex);
					assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
					this->m_peer_2_peerdata[peer].rsa_public_key = theirPubKey;
				}
				// the computerId is validated, as we can't compare it with previous rsa key (first time we see it).
				//TODO: find a way to revoke the rsa key when comparing our server list with other peers. Like, if we found that the majority has a different public key, revoke our and take their.
				{ std::lock_guard lock(m_loaded_peers_mutex);
					if (contains(m_loaded_peers, peer)) {
						//ensure it isn't duplicating computerid (inside our list, you can duplicating it outside, if it tries to connect to us)
						if (std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [dist_cid, &peer](const PeerPtr& test) {return test->getComputerId() == dist_cid && test != peer; }) != m_loaded_peers.end()) {
							error("ERRRRRRRRROOOOOORR09742987");
						}
						assert(std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [dist_cid, &peer](const PeerPtr& test) {return test->getComputerId() == dist_cid && test != peer; }) == m_loaded_peers.end());
					}
					Peer::ComputerIdSetter::setComputerId(*peer, dist_cid);
				}
				// remove the message sent: as it's now validated we don't need it anymore
				//note: don't remove the m_pid_2_emitted_msg yet, as you may receive a late receiveIdentity
				//FIXME: before, it was get->send->send. now it's get->send & get->send
				//if (this->m_pid_2_emitted_msg.find(peer->getPeerId()) != this->m_pid_2_emitted_msg.end())
				//	this->m_pid_2_emitted_msg.erase(peer->getPeerId());
				requestSave();
				//OK, now request a aes key

			} else {
				//if (!Arrays.equals(this->m_peer_2_peerdata.get(dist_cid).getEncoded(), theirPubKey.getEncoded())) {
				if (found_public_key != theirPubKey) {
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, cluster id " + dist_cid + " has a wrong public key (not the one i registered) " + peer->getPeerId() % 100 + " "+"\n");
					//error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) what i have : " + Arrays.tostd::string(this->m_peer_2_peerdata.get(dist_cid).getEncoded())+"\n");
					//error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) what i received : " + Arrays.tostd::string(this->m_peer_2_peerdata.get(dist_cid).getEncoded())+"\n");
					return Identityresult::BAD;
				} else if (dist_cid <= 0) {
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, cluster id " + dist_cid + " hasn't a clusterId > 0 "+"\n");
					return Identityresult::BAD;
				} else {
					log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) publickey ok for computerid " + dist_cid+"\n");
					{ std::lock_guard lock(m_loaded_peers_mutex);
						if (contains(m_loaded_peers, peer)) {
							if (std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [dist_cid, &peer](const PeerPtr& test) {return test->getComputerId() == dist_cid && test != peer; }) != m_loaded_peers.end()) {
								error("ERRRRRRRRROOOOOORR01473897");
							}
							assert(std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [dist_cid, &peer](const PeerPtr& test) {return test->getComputerId() == dist_cid && test != peer; }) == m_loaded_peers.end());
						}
						Peer::ComputerIdSetter::setComputerId(*peer, dist_cid);
					}
					//OK, now request a aes key
				}
			}

		}

		return Identityresult::OK;
		
	}


	void IdentityManager::requestSave() {
		// save the current state into the file.
		if (clusterId || getComputerId() == NO_COMPUTER_ID) return; //don't save if we are not registered on the server yet.
		{ std::lock_guard<std::mutex> lock(this->save_mutex);
			m_parameters->save();
		}
	}



	void IdentityManager::setComputerId(ComputerId myNewComputerId, ComputerIdState newState) {
		//move our peerdata
		{std::lock_guard lock{ this->m_peer_data_mutex };
		ComputerId old = getComputerId();
			msg = std::string("moved from ") + getComputerId() +" to "+ myNewComputerId;
			{ std::lock_guard lock(m_loaded_peers_mutex);
				if (std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [myNewComputerId, this](const PeerPtr& test) {return test->getComputerId() == myNewComputerId && test != m_myself; }) != m_loaded_peers.end()) {
					error("ERRRRRRRRROOOOOORR98769876");
				}
				assert(std::find_if(m_loaded_peers.begin(), m_loaded_peers.end(), [myNewComputerId, this](const PeerPtr& test) {return test->getComputerId() == myNewComputerId && test != m_myself; }) == m_loaded_peers.end());
				Peer::ComputerIdSetter::setComputerId(*m_myself, myNewComputerId);
			}
			myComputerIdState = newState;
			//log(std::to_string(myNewComputerId) + " setComputerId: from " + old + " to " + myNewComputerId+" :> "
			//+(m_peer_2_peerdata.find(old) == m_peer_2_peerdata.end())+ ":"+ (m_peer_2_peerdata.find(myNewComputerId) != m_peer_2_peerdata.end())+":"+ m_peer_2_peerdata.find(myNewComputerId)->);
		}

	}

	//Cipher IdentityManager::getSecretCipher(Peer p, int mode) {
	//	try {
	//		if ((mode == Cipher.ENCRYPT_MODE || mode == Cipher.DECRYPT_MODE)
	//			&& p.hasState(PeerConnectionState.CONNECTED_W_AES)) {
	//			Cipher aesCipher = Cipher.getInstance("AES");
	//			aesCipher.init(mode, this->id2AesKey.get(p.getComputerId()));
	//			return aesCipher;
	//		}

	//	}
	//	catch (NoSuchAlgorithmException | NoSuchPaddingException | InvalidKeyException e) {
	//		throw new RuntimeException(e);
	//	}
	//	return null;
	//}

	//	public ByteBuff encodeSecret(ByteBuff message, Cipher cipherToReuse) throws IllegalBlockSizeException, BadPaddingException{
	//		return new ByteBuff(cipherToReuse.doFinal(message.array(), message.position(), message.limit()));
	//	}
	//	
	//	public ByteBuff decodeSecret(ByteBuff message, Cipher cipherToReuse) throws IllegalBlockSizeException, BadPaddingException{
	//		return new ByteBuff(cipherToReuse.doFinal(message.array(), message.position(), message.limit()));
	//	}

		//using skeletton from http://coding.westreicher.org/?p=23 ( Florian Westreicher)
	//private ByteBuff blockCipher(byte[] bytes, int mode, Cipher cipher) throws IllegalBlockSizeException, BadPaddingException{
	//	// string initialize 2 buffers.
	//	// scrambled will hold intermediate results
	//	byte[] scrambled = new byte[0];

	//	// toReturn will hold the total result
	//	ByteBuff toReturn = new ByteBuff();
	//	// if we encrypt we use 100 byte long blocks. Decryption requires 128 byte long blocks (because of RSA)
	//	int length = (mode == Cipher.ENCRYPT_MODE) ? 100 : 128;
	//	//		int length = (mode == Cipher.ENCRYPT_MODE) ? (keyLength / 8 ) - 11 : (keyLength / 8 );

	//		// another buffer. this one will hold the bytes that have to be modified in this step
	//		byte[] buffer = new byte[(bytes.length > length ? length : bytes.length)];

	//		for (int i = 0; i < bytes.length; i++) {

	//			// if we filled our buffer array we have our block ready for de- or encryption
	//			if ((i > 0) && (i % length == 0)) {
	//				//execute the operation
	//				scrambled = cipher.doFinal(buffer);
	//				// add the result to our total result.
	//				toReturn.put(scrambled);
	//				// here we calculate the length of the next buffer required
	//				int newlength = length;

	//				// if newlength would be longer than remaining bytes in the bytes array we shorten it.
	//				if (i + length > bytes.length) {
	//						newlength = bytes.length - i;
	//				}
	//				// clean the buffer array
	//				if (buffer.length != newlength) buffer = new byte[newlength];
	//			}
	//			// copy byte into our buffer.
	//			buffer[i % length] = bytes[i];
	//		}

	//		// this step is needed if we had a trailing buffer. should only happen when encrypting.
	//		// example: we encrypt 110 bytes. 100 bytes per run means we "forgot" the last 10 bytes. they are in the buffer array
	//		scrambled = cipher.doFinal(buffer);

	//		// final step before we can return the modified data.
	//		toReturn.put(scrambled);

	//		return toReturn.flip();
	//}

	//note: the proposal/confirm thing work because i set my aes key before i emit my proposal.

	void IdentityManager::sendAesKey(PeerPtr peer, uint8_t aesState) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) emit SEND_SERVER_AES_KEY state:" + (int)(aesState)+" : " + ((aesState & AES_CONFIRM) != 0 ?  "CONFIRM" : "PROPOSAL") + " to " + peer->getPeerId() % 100+"\n");

		assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
		//check if i'm able to do this
		bool has_pub_key = false;
		{ std::lock_guard lock(m_peer_data_mutex);
			has_pub_key = peer->getComputerId() >= 0
				&& this->m_peer_2_peerdata.find(peer) != this->m_peer_2_peerdata.end()
				&& !this->m_peer_2_peerdata[peer].rsa_public_key.empty();
		}
		if (has_pub_key) {

			SecretKey secretKey;
			{ std::lock_guard lock(m_peer_data_mutex);
				assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
				PeerData& data_peer = m_peer_2_peerdata[peer];
				if (data_peer.aes_key.size() == 0) {
					//create new aes key
					//KeyGenerator keyGen = KeyGenerator.getInstance("AES");
					//keyGen.init(128);
					//secretKey = keyGen.generateKey();
					std::string secretStr = to_hex_str(rand_u63());
					for (int i = 0; i < secretStr.size(); i++) {
						secretKey.push_back(uint8_t(secretStr[i]));
					}
					//						byte[] aesKey = new byte[128 / 8];	// aes-128 (can be 192/256)
					//						SecureRandom prng = new SecureRandom();
					//						prng.nextBytes(aesKey);
					data_peer.aes_key = secretKey;
				} else {
					secretKey = data_peer.aes_key;
				}
			}

			//encrypt the key
			ByteBuff buffMsg;
			buffMsg.put(aesState);

			//encode msg with private key
			//Cipher cipherPri = Cipher.getInstance("RSA");
			//cipherPri.init(Cipher.ENCRYPT_MODE, privateKey);
			//ByteBuff buffEncodedPriv = blockCipher(secretKey.getEncoded(), Cipher.ENCRYPT_MODE, cipherPri);
			//log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) key : " + Arrays.tostd::string(secretKey.getEncoded())+"\n");
			//log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) EncryptKey : " + Arrays.tostd::string(buffEncodedPriv.array())+"\n");

			//encode again with their public key
			//encode msg with private key
			//Cipher cipherPub = Cipher.getInstance("RSA");
			//cipherPub.init(Cipher.ENCRYPT_MODE, id2PublicKey.get(peer->getComputerId()));
			//ByteBuff buffEncodedPrivPub = blockCipher(buffEncodedPriv.toArray(), Cipher.ENCRYPT_MODE, cipherPub);
			//buffMsg.putSize(buffEncodedPrivPub.limit()).put(buffEncodedPrivPub);
			//log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) EncryptKey2 : " + Arrays.tostd::string(buffEncodedPrivPub.array())+"\n");
			assert(secretKey.size() > 0);
			buffMsg.putSize(secretKey.size()).put(secretKey);

			//send packet
			peer->writeMessage(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, buffMsg.flip());

		} else {
			log(std::string("Error, peer ") + peer->getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}
	}

	bool IdentityManager::receiveAesKey(PeerPtr peer, const ByteBuff& message) {
		log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY" + " from " + peer->getPeerId() % 100+"\n");
		//check if i'm able to do this
		assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
		bool has_pub_key = false;
		{ std::lock_guard lock(m_peer_data_mutex);
			has_pub_key = peer->getComputerId() >= 0  
				&& this->m_peer_2_peerdata.find(peer) != this->m_peer_2_peerdata.end()
				&& !this->m_peer_2_peerdata[peer].rsa_public_key.empty();
		}
		if (has_pub_key) {
			//decrypt the key
			//0 : get the message
			uint8_t aesStateMsg = message.get();
			size_t nbBytesMsg = message.getSize();
			log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY state:" + (int)(aesStateMsg)+" : " + ((aesStateMsg & AES_CONFIRM) != 0 ? "CONFIRM" : "PROPOSAL")+", keysize="+nbBytesMsg);
			std::vector<uint8_t> aesKeyEncrypt = message.get(nbBytesMsg);
			//log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) EncryptKey2 : " + aesKeyEncrypt +"\n");
			//1: decrypt with our private key
			//Cipher cipher = Cipher.getInstance("RSA");
			//cipher.init(Cipher.DECRYPT_MODE, this->privateKey);
			//ByteBuff aesKeySemiDecrypt = blockCipher(aesKeyEncrypt, Cipher.DECRYPT_MODE, cipher);
			//log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) EncryptKey : " + Arrays.tostd::string(aesKeySemiDecrypt.array())+"\n");
			//2 deccrypt with his public key
			//Cipher cipher2 = Cipher.getInstance("RSA");
			//cipher2.init(Cipher.DECRYPT_MODE, id2PublicKey.get(peer->getComputerId()));
			//ByteBuff aesKeyDecrypt = blockCipher(aesKeySemiDecrypt.array(), Cipher.DECRYPT_MODE, cipher2);
			//log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) DecryptKey : " + Arrays.tostd::string(aesKeyDecrypt.array())+"\n");
			//SecretKey secretKeyReceived = new SecretKeySpec(aesKeyDecrypt.array(), aesKeyDecrypt.position(), aesKeyDecrypt.limit(), "AES");
			SecretKey secretKeyReceived = aesKeyEncrypt;
			std::string aes_log_str;
			for (uint8_t c : secretKeyReceived) {
				aes_log_str += to_hex_str(c);
			}
			log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY Key:" + aes_log_str + " (size="+ secretKeyReceived.size()+")");

			//check if we already have one
			uint8_t shouldEmit = 0;
			{ std::lock_guard lock(m_peer_data_mutex);
				auto& it_secretKey = m_peer_2_peerdata.find(peer);
				if (it_secretKey == m_peer_2_peerdata.end()) {
					//store the new one
					m_peer_2_peerdata[peer].aes_key = secretKeyReceived;
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) use new one: " + aes_log_str +"\n");
					if ((aesStateMsg & AES_CONFIRM) != 0) {
						shouldEmit = AES_CONFIRM;
					}
				} else if (it_secretKey->second.aes_key == secretKeyReceived || it_secretKey->second.aes_key.size() == 0) {
					//same, no problem
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: "+ aes_log_str  +" (size="+ it_secretKey->second.aes_key.size());
					if ((aesStateMsg & AES_CONFIRM) == 0) {
						log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: so now i will send a message to validate it.\n");
						shouldEmit = AES_CONFIRM;
					}
				} else {
					//error, conflict?
					if ((aesStateMsg & AES_CONFIRM) != 0) {
						// he blocked this one, choose it
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, receive a contradict confirm"+"\n");
						m_peer_2_peerdata[peer].aes_key = secretKeyReceived;
					} else {
						//it seem he sent an aes at the same time as me, so there is a conflict.
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, conflict"+"\n");
						//use peerid to deconflict
						bool  test = serv.getPeerId() > peer->getPeerId();
						if ((aesStateMsg & AES_CONFLICT_DETECTED) != 0) {
							// if using the peer isn't enough to deconflict, sue some random number until it converge
							test = rand_u8() % 2 == 0;
						}
						if (test) {
							//use new one
							m_peer_2_peerdata[peer].aes_key = secretKeyReceived;
							log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) conflict: use new: "+ aes_log_str +"\n");
						} else {
							//nothing, we keep the current one
							std::string aes_log_str_old;
							for (uint8_t c : m_peer_2_peerdata[peer].aes_key) {
								aes_log_str_old += to_hex_str(c);
							}
							log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) conflict: use old : "+ aes_log_str_old + "\n");
						}
						//notify (outside of sync group)
						shouldEmit = AES_PROPOSAL | AES_CONFLICT_DETECTED;
					}
				}
			} // end the lock before doing things (like sendAesKey)

			if (shouldEmit != 0) {
				sendAesKey(peer, shouldEmit);
			}
			auto aeff_pda = m_peer_2_peerdata[peer];

			bool receive_confirm = (aesStateMsg & AES_CONFIRM) != 0 && (aesStateMsg & AES_CONFLICT_DETECTED) == 0;
			bool allow_confirm = (shouldEmit & AES_PROPOSAL) == 0 && (shouldEmit & AES_CONFLICT_DETECTED) == 0;
			return (receive_confirm && allow_confirm) || (shouldEmit == AES_CONFIRM);

		} else {
			log(std::string("Error, peer ") + peer->getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}

		return false;
	}



} // namespace supercloud
