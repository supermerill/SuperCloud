
#include "IdentityManager.hpp"
#include "PhysicalServer.hpp"
#include "utils/Parameters.hpp"


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


	IdentityManager::PeerData IdentityManager::getPeerData(uint16_t computer_id) const {
		std::lock_guard lock(peer_data_mutex);
		auto it = id_2_peerdata.find(computer_id);
		if (it != id_2_peerdata.end()) {
			return it->second;
		}

		return PeerData{};
	}
	bool IdentityManager::setPeerData(const PeerData& original, const PeerData& new_data) {
		std::lock_guard lock(peer_data_mutex);
		if (!original.peer) return false;
		auto it = id_2_peerdata.find(original.peer->getComputerId());
		if (it != id_2_peerdata.end()) {
			if (it->second == original) {
				it->second = new_data;
			}
		}
		return false;
	}

	void IdentityManager::create_from_install_file(const std::filesystem::path& currrent_filepath) {
		//try to get data from the install file
		std::filesystem::path abs_path = std::filesystem::absolute(currrent_filepath);
		Parameters paramsNet{ abs_path.parent_path() / "network.properties" };
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
				//tcp::endpoint addr{ boost::asio::ip::address::from_string(paramsNet.get("PeerIp")), uint16_t(paramsNet.getInt("PeerPort")) };
				PeerPtr p = Peer::create(serv, paramsNet.get("PeerIp"), uint16_t(paramsNet.getInt("PeerPort")));
				p->setComputerId((uint16_t)-1); // set it to <0 to let us know it's invalid
				{ std::lock_guard lock{ loadedPeers_mutex };
					loadedPeers.push_back(p);
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

		//choose the file.
		std::filesystem::path currrent_filepath = this->filepath;
		bool exists = std::filesystem::exists(currrent_filepath);
		if (!exists) {
			currrent_filepath.replace_filename(currrent_filepath.filename().string() + std::string("_1"));
			exists = std::filesystem::exists(currrent_filepath);
			if (!exists) {
				create_from_install_file(this->filepath);
				return;
			}
		}

		std::vector<PeerPtr> loaded_peers_copy;
		Parameters params_server_db{ currrent_filepath };
		this->clusterId = params_server_db.getLong("clusterId");
		this->myComputerIdState = (ComputerIdState)params_server_db.getInt("computerIdState");
		this->myComputerId = (uint16_t)params_server_db.getInt("computerId");
		this->passphrase = params_server_db.get("passphrase");
		this->publicKey = params_server_db.get("publicKey");
		this->privateKey = params_server_db.get("privateKey");
		size_t nbPeers = size_t(params_server_db.getInt("nbPeers", 0));
		{std::lock_guard lock(peer_data_mutex);
			for (size_t i = 0; i < nbPeers; i++) {
				const std::string& dist_ip = params_server_db.get("peer" + std::to_string(i) + "_ip");
				uint16_t dist_port = uint16_t(params_server_db.getInt("peer" + std::to_string(i) + "_port"));
				uint16_t dist_id = uint16_t(params_server_db.getInt("peer" + std::to_string(i) + "_id"));
				const std::string& dist_pub_key = params_server_db.get("peer" + std::to_string(i) + "_publicKey");
				PeerPtr p = Peer::create(serv, dist_ip, dist_port);
				p->setComputerId(dist_id);
				loaded_peers_copy.push_back(p);
				PeerData& data = id_2_peerdata[dist_id];
				data.rsa_public_key = dist_pub_key;
				data.peer = p;
				for (size_t idx_net2conn = 0; params_server_db.has("peer" + std::to_string(i) + "_con_" + std::to_string(idx_net2conn) + "_net"); ++idx_net2conn) {
					std::vector<PeerConnection>& connections = data.net2validAddress[params_server_db.get("peer" + std::to_string(i) + "_con_" + std::to_string(idx_net2conn) + "_net")];
					connections.clear();
					for (size_t idx_addr = 0; params_server_db.has("peer" + std::to_string(i) + "_con_" + std::to_string(idx_net2conn) + "_" + std::to_string(idx_addr) + "_ip"); ++idx_addr) {
						std::string connection_ip = params_server_db.get("peer" + std::to_string(i) + "_con_" + std::to_string(idx_net2conn) + "_" + std::to_string(idx_addr) + "_ip");
						uint16_t connection_port = uint16_t(params_server_db.getInt("peer" + std::to_string(i) + "_con_" + std::to_string(idx_net2conn) + "_" + std::to_string(idx_addr) + "_port"));
						if (connection_ip.size() > 2) {
							// PeerConnection{address, first_hand_information, initiated_by_me }
							connections.push_back(PeerConnection{ connection_ip.substr(2), connection_port, connection_ip[0] == '!', connection_ip[1] == 'O' });
						}
					}
				}
			}
		}
		{ std::lock_guard lock{ loadedPeers_mutex };
			loadedPeers = loaded_peers_copy;
		}
	}

	void IdentityManager::save(const std::filesystem::path& filePath) const {
		Parameters params_server_db{ filePath };
		params_server_db.setLong("clusterId", this->clusterId);
		params_server_db.setInt("computerId", this->myComputerId);
		params_server_db.set("passphrase", this->passphrase);
		params_server_db.set("publicKey", this->publicKey);
		params_server_db.set("privateKey", this->privateKey);
		std::vector<PeerPtr> loaded_peers_copy;
		{ std::lock_guard lockpeers{ loadedPeers_mutex };
			loaded_peers_copy = this->loadedPeers;
		}
		params_server_db.setInt("nbPeers", int32_t(loaded_peers_copy.size()));
		size_t peer_idx = 0;
		{std::lock_guard<std::mutex> lockpdata(this->peer_data_mutex);
			for (const PeerPtr& peer : loaded_peers_copy) {
				params_server_db.set("peer" + std::to_string(peer_idx) + "_ip", peer->getIP());
				params_server_db.setInt("peer" + std::to_string(peer_idx) + "_port", peer->getPort());
				params_server_db.setInt("peer" + std::to_string(peer_idx) + "_id", peer->getComputerId());
				auto found = id_2_peerdata.find(peer->getComputerId());
				if (found != id_2_peerdata.end() && found->second.peer) {
					const PeerData& data = found->second;
					params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", data.rsa_public_key);
					size_t idx_conn = 0;
					for (const auto& net2ip : data.net2validAddress) {
						params_server_db.set("peer" + std::to_string(peer_idx) + "_con_" + std::to_string(idx_conn) + "_net", net2ip.first);
						size_t idx_addr = 0;
						for (const PeerConnection& connection : net2ip.second) {
							std::stringstream ss;
							ss << connection.first_hand_information ? '!' : '?';
							ss << connection.intitiated_by_me ? 'O' : 'X';
							ss << connection.address;
							params_server_db.set("peer" + std::to_string(peer_idx) + "_con_" + std::to_string(idx_conn) + "_" + std::to_string(idx_addr) + "_ip", ss.str());
							params_server_db.setInt("peer" + std::to_string(peer_idx) + "_con_" + std::to_string(idx_conn) + "_" + std::to_string(idx_addr) + "_port", connection.port);
							++idx_addr;
						}
						++idx_conn;
					}
				} else {
					params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", "ERROR, CANT FIND");
				}
				//relance
				peer_idx++;
			}
		}

		params_server_db.save();
	}

	void IdentityManager::fusionWithConnectedPeer(PeerPtr connectedPeer) {
		assert(connectedPeer->isAlive() && connectedPeer->isConnected());
		{ std::lock_guard lock{ loadedPeers_mutex };
			Peer* deletedPeer = nullptr;
			foreach(peer_it, loadedPeers) {
				if ((*peer_it)->getComputerId() == connectedPeer->getComputerId()) {
					deletedPeer = (*peer_it).get();
					//fusionPeers(connectedPeer, (*peer_it)); // nothing to fusion
					peer_it.erase();
					loadedPeers.push_back(connectedPeer);
				}
			}
			if (deletedPeer == nullptr) {
				//add it to loaded peers
				loadedPeers.push_back(connectedPeer);
			}
		}
		//replace also in the peerdata
		{ std::lock_guard lock(peer_data_mutex);
			id_2_peerdata[connectedPeer->getComputerId()].peer = connectedPeer;
		}
	}


	void IdentityManager::addNewPeer(uint16_t computerId, const PeerData& data) {
		bool not_found = false;
		PeerPtr new_peer;
		{std::lock_guard lock{ loadedPeers_mutex };
			//ensure it's not here
			not_found = loadedPeers.end() == std::find_if(loadedPeers.begin(), loadedPeers.end(), [computerId](PeerPtr& p) {return computerId == p->getComputerId(); });
			if (not_found) {
				new_peer = Peer::create(serv, "", 0);
				new_peer->setComputerId(computerId);
				loadedPeers.push_back(new_peer);
			}
		}
		if (not_found) {
			std::lock_guard lock(peer_data_mutex);
			id_2_peerdata[computerId] = data;
			id_2_peerdata[computerId].peer = new_peer;
		}
	}

	//send our public key to the peer
	void IdentityManager::sendPublicKey(Peer& peer) {
		//std::cout+ &serv + " (sendPublicKey) emit GET_SERVER_PUBLIC_KEY to " + peer+"\n";
		log(std::to_string(serv.getPeerId() % 100) + " (sendPublicKey) emit SEND_SERVER_PUBLIC_KEY to " + peer.getPeerId() % 100+"\n");

		ByteBuff buff;
		//my pub key
		//uint8_t[] encodedPubKey = publicKey.getEncoded();
		const uint8_t* encodedPubKey = reinterpret_cast<const uint8_t*>(&publicKey[0]);
		size_t encodedPubKey_size = publicKey.size();
		buff.putSize(encodedPubKey_size).put(encodedPubKey, size_t(0), encodedPubKey_size);
		//send packet
		peer.writeMessage(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, buff.flip());

	}

	void IdentityManager::receivePublicKey(Peer& sender, ByteBuff& buffIn) {
		log(std::to_string(serv.getPeerId() % 100) + " (receivePublicKey) receive SEND_SERVER_PUBLIC_KEY from " + sender.getPeerId() % 100+"\n");
		
		//get pub Key
		size_t nbBytes = buffIn.getSize();
		//byte[] encodedPubKey = new byte[nbBytes];
		std::shared_ptr<uint8_t> encodedPubKey{ new uint8_t[int32_t(nbBytes + 1)] };
		buffIn.get(encodedPubKey.get(), 0, nbBytes);
		encodedPubKey.get()[nbBytes] = 0;
		const char* cstr_pub_key = reinterpret_cast<const char*>(encodedPubKey.get());
		//X509EncodedKeySpec bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//KeyFactory keyFactory = KeyFactory.getInstance("RSA");
		//PublicKey distPublicKey = keyFactory.generatePublic(bobPubKeySpec);
		PublicKey distPublicKey{ cstr_pub_key };
		{ std::lock_guard lock(tempPubKey_mutex);
			tempPubKey[sender.getPeerId()] = distPublicKey;
			//				std::cout+serv.getId()%100+" (receivePublicKey) peer "+p.getConnectionId()%100+" has now a pub key of "+tempPubKey.get(p.getConnectionId())+"\n";
		}
	}

	std::string IdentityManager::createMessageForIdentityCheck(Peer& peer, bool forceNewOne) {
		const auto& it_msg = peerId2emittedMsg.find(peer.getPeerId());
		std::string msg;
		if (it_msg == peerId2emittedMsg.end() || forceNewOne) {
			if (it_msg == peerId2emittedMsg.end()) {
				msg = to_hex_str(rand_u63());
			} else {
				msg = it_msg->second;
			}
			peerId2emittedMsg[peer.getPeerId()] = msg;
			//todo: encrypt it with our public key.
		} else {
			msg = it_msg->second;
			log(std::string(" (createMessageForIdentityCheck) We already emit a request for indentity to ") + peer.getPeerId() % 100 + " with message " + peerId2emittedMsg[peer.getPeerId()]+"\n");
		}
		return msg;
	}

	//send our public key to the peer, with the message encoded
	IdentityManager::Identityresult IdentityManager::sendIdentity(Peer& peer, const std::string& messageToEncrypt, bool isRequest) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") emit " + (isRequest ? "GET_IDENTITY" : "SEND_IDENTITY") + " to " + peer.getPeerId() % 100 + ", with message 2encrypt : " + messageToEncrypt+"\n");
		//check if we have the public key of this peer
		PublicKey theirPubKey = "";
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer.getPeerId());
			if (it_theirPubKey == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") i don't have public key! why are you doing that? Request his one!"+"\n");
				return IdentityManager::Identityresult::NO_PUB;
			} else {
				theirPubKey = it_theirPubKey->second;
			}
		}

		//don't emit myId if it's NO_COMPUTER_ID (-1)
		if (myComputerId == NO_COMPUTER_ID) {
			log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") but i have null id!! \n");
			ByteBuff buff;
			buff.putShort(NO_COMPUTER_ID);
			//send packet
			if (!isRequest) {
				log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") so i return '-1' \n");
				peer.writeMessage(*UnnencryptedMessageType::SEND_VERIFY_IDENTITY, buff.flip());
			}
			return IdentityManager::Identityresult::BAD;
		}

		//TODO: encrypt it also with his public key if we have it?
		ByteBuff buff;
		//encode msg
		//Cipher cipher = Cipher.getInstance("RSA");
		//cipher.init(Cipher.ENCRYPT_MODE, privateKey);
		//ByteBuff buffEncoded = blockCipher(new ByteBuff().putShort(myComputerId).putUTF8(messageToEncrypt).putUTF8(passphrase).flip().toArray(), Cipher.ENCRYPT_MODE, cipher);
		ByteBuff buffEncoded; buffEncoded.putShort(myComputerId).putUTF8(messageToEncrypt).putUTF8(passphrase).flip();

		//encrypt more
		//cipher.init(Cipher.ENCRYPT_MODE, theirPubKey);
		//buffEncoded = blockCipher(buffEncoded.array(), Cipher.ENCRYPT_MODE, cipher);

		buff.putSize(buffEncoded.limit()).put(buffEncoded);
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") message : " + messageToEncrypt+"\n");
		//log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") Encryptmessage : " + Arrays.tostd::string(buffEncoded.rewind().array())+"\n");

		//send packet
		peer.writeMessage(isRequest ? *UnnencryptedMessageType::GET_VERIFY_IDENTITY : *UnnencryptedMessageType::SEND_VERIFY_IDENTITY, buff.flip());

		return IdentityManager::Identityresult::OK;
	}

	ByteBuff IdentityManager::getIdentityDecodedMessage(const PublicKey& key, ByteBuff& buffIn) {
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

	IdentityManager::Identityresult IdentityManager::answerIdentity(Peer& peer, ByteBuff& buffIn) {
		log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) receive GET_IDENTITY to " + peer.getPeerId() % 100+"\n");


		PublicKey theirPubKey = "";
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer.getPeerId());
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
			error(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) he ask use somethign whithout a message! it's crazy!!! " + peer.getComputerId() + " : " + peer.getPeerId() % 100+"\n");
			//not ready, maybe he didn't choose his computerid yet?
			//we have to ask him a bit later.
			//ping() is already in charge of that
			return Identityresult::BAD;
		}

		ByteBuff buffDecoded = getIdentityDecodedMessage(theirPubKey, buffIn);
		uint16_t unverifiedCompId = (uint16_t)(buffDecoded.getShort()); ///osef short distId = because we can't verify it.
		std::string msgDecoded = buffDecoded.getUTF8();
		std::string theirPwd = buffDecoded.getUTF8();
		if (theirPwd != (passphrase)) {
			log(std::string("Error: peer ") + peer.getPeerId() % 100 + " : ?" + unverifiedCompId + "? has not the same pwd as us."+"\n");
			peer.close(); //TODO: verify that Physical server don't revive it.
			//TODO : remove it from possible server list
			return Identityresult::BAD;
		} else {
			log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) msgDecoded = " + msgDecoded+"\n");
			//same pwd, continue
			sendIdentity(peer, msgDecoded, false);
			return Identityresult::OK;
		}
	}

	IdentityManager::Identityresult IdentityManager::receiveIdentity(PeerPtr peer, ByteBuff& message) {
		log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) receive SEND_IDENTITY to " + peer->getPeerId() % 100+"\n");


		PublicKey theirPubKey = "";
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
		uint16_t distId = uint16_t(buffDecoded.getShort());
		std::string msgDecoded = buffDecoded.getUTF8();
		std::string theirPwd = buffDecoded.getUTF8();


		if (distId == NO_COMPUTER_ID || peerId2emittedMsg.find(peer->getPeerId()) == peerId2emittedMsg.end()) {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) BAD receive computerid " + distId + " and i " + (peerId2emittedMsg.find(peer->getPeerId()) != peerId2emittedMsg.end()) + " emmitted a message"+"\n");
			//the other peer doesn't have an computerid (yet)
			if(peerId2emittedMsg.find(peer->getPeerId()) != peerId2emittedMsg.end())
				peerId2emittedMsg.erase(peer->getPeerId());
			return Identityresult::BAD;
		} else {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) GOOD receive computerid " + distId + " and i " + (peerId2emittedMsg.find(peer->getPeerId()) != peerId2emittedMsg.end()) + " emmitted a message"+"\n");
		}
		if (theirPwd != (passphrase)) {
			log(std::string("Error: peer ") + peer->getPeerId() % 100 + " : " + distId + " has not the same pwd as us (2)."+"\n");
			peer->close(); //TODO: verify that Physical server don't revive it.
			//TODO : remove it from possible server list
			return Identityresult::BAD;
		}

		log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) i have sent to " + peer->getPeerId() % 100 + " the message " + (peerId2emittedMsg.find(peer->getPeerId())->second) + " to encode. i have received " + msgDecoded + " !!!"+"\n");

		//check if this message is inside our peer
		if ( (peerId2emittedMsg.find(peer->getPeerId())->second) == (msgDecoded)) {

			bool found = false;
			PublicKey found_public_key;
			//check if the public key is the same
			{ std::lock_guard lock(peer_data_mutex);
				auto search = this->id_2_peerdata.find(distId);
				found = search != this->id_2_peerdata.end();
				if (found) { found_public_key = search->second.rsa_public_key; }
			}
			// if not already in the storage and the key is valid, then accept & store it.
			if (!found || found_public_key == "" && distId > 0) {
				log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) assign new publickey  for computerid " + distId + " , connId=" + peer->getPeerId() % 100+"\n");
				//validate this peer
				{ std::lock_guard lock(peer_data_mutex);
					this->id_2_peerdata[distId].rsa_public_key = theirPubKey;
				}
				// the computerId is validated, as we can't compare it with previous rsa key (first time we see it).
				//TODO: find a way to revoke the rsa key when comparing our server list with other peers. Like, if we found that the majority has a different public key, revoke our and take their.
				peer->setComputerId(distId);
				// remove the message sent: as it's now validated we don't need it anymore
				if (this->peerId2emittedMsg.find(peer->getPeerId()) != this->peerId2emittedMsg.end())
					this->peerId2emittedMsg.erase(peer->getPeerId());
				requestSave();
				//OK, now request a aes key

			} else {
				//if (!Arrays.equals(this->id_2_peerdata.get(distId).getEncoded(), theirPubKey.getEncoded())) {
				if (found_public_key != theirPubKey) {
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, cluster id " + distId + " has a wrong public key (not the one i registered) " + peer->getPeerId() % 100 + " "+"\n");
					//error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) what i have : " + Arrays.tostd::string(this->id_2_peerdata.get(distId).getEncoded())+"\n");
					//error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) what i received : " + Arrays.tostd::string(this->id_2_peerdata.get(distId).getEncoded())+"\n");
					return Identityresult::BAD;
				} else if (distId <= 0) {
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, cluster id " + distId + " hasn't a clusterId > 0 "+"\n");
					return Identityresult::BAD;
				} else {
					log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) publickey ok for computerid " + distId+"\n");
					peer->setComputerId(distId);
					//OK, now request a aes key
				}
			}

		}

		return Identityresult::OK;
		
	}


	void IdentityManager::requestSave() {
		// save the current state into the file.
		if (clusterId  || myComputerId == NO_COMPUTER_ID) return; //don't save if we are not registered on the server yet.
		// get synch
		{ std::lock_guard<std::mutex> lock(this->save_mutex);
			std::filesystem::path fic(filepath);
			std::filesystem::path ficBak = fic;
			std::filesystem::path ficTemp(filepath);
			ficTemp .replace_filename(ficTemp.filename().string()+std::string("_1"));
			// create an other file
			save(ficTemp);
			// remove first file
			ficBak.replace_filename(ficBak.filename().string() + ".bak");
			bool exists = std::filesystem::exists(fic);
			bool exists_bak = std::filesystem::exists(ficBak);
			if (exists_bak) std::filesystem::remove(ficBak);
			if (exists) std::filesystem::rename(fic, ficBak);
			// rename file
			std::filesystem::rename(ficTemp, fic);
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

	void IdentityManager::sendAesKey(Peer& peer, uint8_t aesState) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) emit SEND_SERVER_AES_KEY state:" + (int)(aesState)+" : " + ((aesState & AES_CONFIRM) != 0 ?  "CONFIRM" : "PROPOSAL") + " to " + peer.getPeerId() % 100+"\n");

		//check if i'm able to do this
		bool has_pub_key = false;
		{ std::lock_guard lock(peer_data_mutex);
			has_pub_key = peer.getComputerId() >= 0
				&& this->id_2_peerdata.find(peer.getComputerId()) != this->id_2_peerdata.end()
				&& this->id_2_peerdata[peer.getComputerId()].rsa_public_key != "";
		}
		if (has_pub_key) {

			SecretKey secretKey;
			{ std::lock_guard lock(peer_data_mutex);
				auto& it_secretKey = id_2_peerdata.find(peer.getComputerId());
				if (it_secretKey == id_2_peerdata.end()) {
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
					it_secretKey->second.aes_key = secretKey;
				} else {
					secretKey = it_secretKey->second.aes_key;
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
			//cipherPub.init(Cipher.ENCRYPT_MODE, id2PublicKey.get(peer.getComputerId()));
			//ByteBuff buffEncodedPrivPub = blockCipher(buffEncodedPriv.toArray(), Cipher.ENCRYPT_MODE, cipherPub);
			//buffMsg.putSize(buffEncodedPrivPub.limit()).put(buffEncodedPrivPub);
			//log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) EncryptKey2 : " + Arrays.tostd::string(buffEncodedPrivPub.array())+"\n");
			buffMsg.putSize(secretKey.size()).put(secretKey);

			//send packet
			peer.writeMessage(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, buffMsg.flip());

		} else {
			log(std::string("Error, peer ") + peer.getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}
	}

	bool IdentityManager::receiveAesKey(Peer& peer, ByteBuff& message) {
		log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY" + " from " + peer.getPeerId() % 100+"\n");
		//check if i'm able to do this
		bool has_pub_key = false;
		{ std::lock_guard lock(peer_data_mutex);
			has_pub_key = peer.getComputerId() >= 0  
				&& this->id_2_peerdata.find(peer.getComputerId()) != this->id_2_peerdata.end()
				&& this->id_2_peerdata[peer.getComputerId()].rsa_public_key != "";
		}
		if (has_pub_key) {
			//decrypt the key
			//0 : get the message
			uint8_t aesStateMsg = message.get();
			log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY state:" + (int)(aesStateMsg)+" : " + ((aesStateMsg & AES_CONFIRM) != 0 ? "CONFIRM" : "PROPOSAL")+"\n");
			size_t nbBytesMsg = message.getSize();
			std::vector<uint8_t> aesKeyEncrypt = message.get(nbBytesMsg);
			//log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) EncryptKey2 : " + aesKeyEncrypt +"\n");
			//1: decrypt with our private key
			//Cipher cipher = Cipher.getInstance("RSA");
			//cipher.init(Cipher.DECRYPT_MODE, this->privateKey);
			//ByteBuff aesKeySemiDecrypt = blockCipher(aesKeyEncrypt, Cipher.DECRYPT_MODE, cipher);
			//log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) EncryptKey : " + Arrays.tostd::string(aesKeySemiDecrypt.array())+"\n");
			//2 deccrypt with his public key
			//Cipher cipher2 = Cipher.getInstance("RSA");
			//cipher2.init(Cipher.DECRYPT_MODE, id2PublicKey.get(peer.getComputerId()));
			//ByteBuff aesKeyDecrypt = blockCipher(aesKeySemiDecrypt.array(), Cipher.DECRYPT_MODE, cipher2);
			//log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) DecryptKey : " + Arrays.tostd::string(aesKeyDecrypt.array())+"\n");
			//SecretKey secretKeyReceived = new SecretKeySpec(aesKeyDecrypt.array(), aesKeyDecrypt.position(), aesKeyDecrypt.limit(), "AES");
			SecretKey secretKeyReceived = aesKeyEncrypt;
			std::string aes_log_str;
			for (uint8_t c : secretKeyReceived) {
				aes_log_str += to_hex_str(c);
			}

			//check if we already have one
			uint8_t shouldEmit = uint8_t(-1);
			{ std::lock_guard lock(peer_data_mutex);
				auto& it_secretKey = id_2_peerdata.find(peer.getComputerId());
				if (it_secretKey == id_2_peerdata.end()) {
					//store the new one
					id_2_peerdata[peer.getComputerId()].aes_key = secretKeyReceived;
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) use new one: " + aes_log_str +"\n");
					if ((aesStateMsg & AES_CONFIRM) != 0) {
						shouldEmit = AES_CONFIRM;
					}
				} else if (it_secretKey->second.aes_key == secretKeyReceived) {
					//same, no problem
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: "+ aes_log_str +"\n");
					if ((aesStateMsg & AES_CONFIRM) == 0) {
						log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: so now i will send a message to validate it.\n");
						shouldEmit = AES_CONFIRM;
					}
				} else {
					//error, conflict?
					if ((aesStateMsg & AES_CONFIRM) != 0) {
						// he blocked this one, choose it
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, receive a contradict confirm"+"\n");
						id_2_peerdata[peer.getComputerId()].aes_key = secretKeyReceived;
					} else {
						//it seem he sent an aes at the same time as me, so there is a conflict.
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, conflict"+"\n");
						//use peerid to deconflict
						bool  test = serv.getPeerId() > peer.getPeerId();
						if ((aesStateMsg & AES_CONFLICT_DETECTED) != 0) {
							// if using the peer isn't enough to deconflict, sue some random number until it converge
							test = rand_u8() % 2 == 0;
						}
						if (test) {
							//use new one
							id_2_peerdata[peer.getComputerId()].aes_key = secretKeyReceived;
							log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) conflict: use new: "+ aes_log_str +"\n");
						} else {
							//nothing, we keep the current one
							std::string aes_log_str_old;
							for (uint8_t c : id_2_peerdata[peer.getComputerId()].aes_key) {
								aes_log_str_old += to_hex_str(c);
							}
							log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) conflict: use old : "+ aes_log_str_old + "\n");
						}
						//notify (outside of sync group)
						shouldEmit = AES_PROPOSAL | AES_CONFLICT_DETECTED;
					}
				}
			} // end the lock before doing things (like sendAesKey)
			
			if (shouldEmit != uint8_t(-1)) {
				sendAesKey(peer, shouldEmit);
			}
			return (shouldEmit & AES_CONFIRM) != 0 && (shouldEmit & AES_CONFLICT_DETECTED) == 0;

		} else {
			log(std::string("Error, peer ") + peer.getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}

		return false;
	}



} // namespace supercloud
