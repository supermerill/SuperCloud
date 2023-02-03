
#include "ServerIdDb.hpp"
#include "PhysicalServer.hpp"
#include "utils/Parameters.hpp"


#include <chrono>
#include <filesystem>

namespace supercloud{


	void ServerIdDb::createNewPublicKey() {
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

	//PrivateKey ServerIdDb::createPrivKey(const std::vector<uint8_t>& datas) {
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


	void ServerIdDb::load() {
		//choose the file.
		std::filesystem::path currrent_filepath = this->filepath;
		bool exists = std::filesystem::exists(currrent_filepath);
		if (!exists) {
			currrent_filepath.replace_filename(currrent_filepath.filename().string() + std::string("_1"));
			exists = std::filesystem::exists(currrent_filepath);
			if (!exists) {

				//try to get data from the install file
				std::filesystem::path abs_path = std::filesystem::absolute(currrent_filepath);
				Parameters paramsNet{ abs_path.parent_path() / "network.properties" };
				if (!paramsNet.has("ClusterId")) {
					//no previous data, load nothing plz.
					log("No install data, please construct them!!!\n");
					createNewPublicKey();
				} else {

					if (paramsNet.has("ClusterId")) {
						log(std::string("set ClusterId to ") + uint64_t(paramsNet.getLong("ClusterId")) + " (= " + paramsNet.get("ClusterId")  + " )"
							+" => "+ (uint64_t(paramsNet.getLong("ClusterId")) %100) +"\n");
						clusterId = paramsNet.getLong("ClusterId");
					}
					if (paramsNet.has("ClusterPassphrase")) {
						log(std::string("set passphrase to ") + paramsNet.get("ClusterPassphrase")+"\n");
						passphrase = paramsNet.get("ClusterPassphrase");
					}
					if (paramsNet.has("PeerIp") && paramsNet.has("PeerPort")) {
						//tcp::endpoint addr{ boost::asio::ip::address::from_string(paramsNet.get("PeerIp")), uint16_t(paramsNet.getInt("PeerPort")) };
						PeerPtr p = Peer::create(serv, paramsNet.get("PeerIp"), uint16_t(paramsNet.getInt("PeerPort")));
						p->setComputerId((uint16_t)-1); // set it to <0 to let us know it's invalid
						loadedPeers.push_back(p);
					}
					if (!paramsNet.has("PubKey") || !paramsNet.has("PrivKey")) {
						log("no priv/pub key defeined, creating some random ones\n");
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
				return;
			}
		}

		Parameters params_server_db{ currrent_filepath };
		this->clusterId = params_server_db.getLong("clusterId");
		this->myComputerIdState = (ComputerIdState)params_server_db.getInt("computerIdState");
		this->myComputerId = (uint16_t)params_server_db.getInt("computerId");
		this->passphrase = params_server_db.get("passphrase");
		this->publicKey = params_server_db.get("publicKey");
		this->privateKey = params_server_db.get("privateKey");
		size_t nbPeers = size_t(params_server_db.getInt("nbPeers", 0));
		for (size_t i = 0; i < nbPeers; i++) {
			const std::string& dist_ip = params_server_db.get("peer" + std::to_string(i) + "_ip");
			uint16_t dist_port = uint16_t(params_server_db.getInt("peer" + std::to_string(i) + "_port"));
			uint16_t dist_id = uint16_t(params_server_db.getInt("peer" + std::to_string(i) + "_id"));
			const std::string& dist_pub_key = params_server_db.get("peer" + std::to_string(i) + "_publicKey");
			PeerPtr p = Peer::create(serv, dist_ip, dist_port);
			p->setComputerId(dist_id);
			loadedPeers.push_back(p);
			id2PublicKey[dist_id] = dist_pub_key;
		}
		//try(BufferedInputStream in = new BufferedInputStream(new FileInputStream(fic))) {
		//	ByteBuff bufferReac(1024);

		//	//read
		//	clusterId = bufferReac.reset().read(in, 8).flip().getLong();
		//	std::cout+serv.getPeerId() % 100 + " clusterId : " + clusterId+"\n";
		//	//			log(std::string("clusterId : "+Long.toHexstd::string(clusterId)+"\n";

		//				//read id
		//	myComputerId = bufferReac.reset().read(in, 2).flip().getShort();
		//	std::cout+serv.getPeerId() % 100 + " LOAD MY COMPUTER ID as =" + myComputerId+"\n";
		//	//			std::cout+serv.getPeerId()%100+" myComputerId : "+myComputerId+"\n";
		//	//			log(std::string("myId : "+Integer.toHexstd::string(bufferReac.rewind().getShort())+"\n";

		//				//read passphrase
		//	short nbBytesPwd = bufferReac.reset().read(in, 2).flip().getShort();
		//	passphrase = bufferReac.reset().read(in, nbBytesPwd).flip().getUTF8();

		//	//read pubKey
		//	int nbBytes = bufferReac.reset().read(in, 4).flip().getInt();
		//	byte[] encodedPubKey = new byte[nbBytes];
		//	bufferReac.reset().read(in, nbBytes).flip().get(encodedPubKey, 0, nbBytes);
		//	X509EncodedKeySpec bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//	KeyFactory keyFactory = KeyFactory.getInstance("RSA");
		//	publicKey = keyFactory.generatePublic(bobPubKeySpec);
		//	std::cout+serv.getPeerId() % 100 + " publicKey => " + Arrays.tostd::string(encodedPubKey)+"\n";

		//	//read privKey
		//	nbBytes = bufferReac.reset().read(in, 4).flip().getInt();
		//	byte[] encodedPrivKey = new byte[nbBytes];
		//	bufferReac.reset().read(in, nbBytes).flip().get(encodedPrivKey, 0, nbBytes);
		//	privateKey = createPrivKey(encodedPrivKey);

		//	//read peers
		//	loadedPeers.clear();
		//	int nbPeers = bufferReac.reset().read(in, 4).flip().getInt();
		//	std::cout+serv.getPeerId() % 100 + " i have " + nbPeers + " peers"+"\n";
		//	for (int i = 0; i < nbPeers; i++) {
		//		nbBytes = bufferReac.reset().read(in, 2).flip().getShort();
		//		std::string distIp = bufferReac.read(in, nbBytes).flip().getShortUTF8();
		//		log(std::string("read peer ip from file : " + distIp+"\n";
		//		int distPort = bufferReac.reset().read(in, 4).flip().getInt();
		//		short distId = bufferReac.reset().read(in, 2).flip().getShort();
		//		nbBytes = bufferReac.reset().read(in, 4).flip().getInt();
		//		encodedPubKey = new byte[nbBytes];
		//		PublicKey distPublicKey = null;
		//		if (nbBytes > 0) {
		//			std::cout+serv.getPeerId() % 100 + " (ext)publicKey size : " + nbBytes+"\n";
		//			bufferReac.reset().read(in, nbBytes).flip().get(encodedPubKey, 0, nbBytes);
		//			std::cout+serv.getPeerId() % 100 + "  => " + Arrays.tostd::string(encodedPubKey)+"\n";
		//			bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//			distPublicKey = keyFactory.generatePublic(bobPubKeySpec);
		//			//save
		//			Peer p = new Peer(serv, new InetSocketAddress(distIp, distPort).getAddress(), distPort);
		//			p.setComputerId(distId);
		//			loadedPeers.add(p);
		//			id2PublicKey.put(distId, distPublicKey);
		//		} else {
		//			std::cerr+serv.getPeerId() % 100 + " error, i have a peer but he has no public key."+"\n";
		//		}
		//	}


		//}
		//catch (IOException | NoSuchAlgorithmException | InvalidKeySpecException e1) {
		//	e1.printStackTrace();
		//	throw new RuntimeException(e1);
		//}
	}

	void ServerIdDb::save(const std::filesystem::path& filePath) const {

		Parameters params_server_db{ filePath };
		params_server_db.setLong("clusterId", this->clusterId);
		params_server_db.setInt("computerId", this->myComputerId);
		params_server_db.set("passphrase", this->passphrase);
		params_server_db.set("publicKey", this->publicKey);
		params_server_db.set("privateKey", this->privateKey);
		params_server_db.setInt("nbPeers", int32_t(this->loadedPeers.size()));
		size_t peer_idx = 0;
		for (const PeerPtr& peer : this->loadedPeers) {
			params_server_db.set("peer" + std::to_string(peer_idx) + "_ip", peer->getIP());
			params_server_db.setInt("peer" + std::to_string(peer_idx) + "_port", peer->getPort());
			params_server_db.setInt("peer" + std::to_string(peer_idx) + "_id", peer->getComputerId());
			auto found = id2PublicKey.find(peer->getComputerId());
			if (found != id2PublicKey.end()) {
				params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", found->second/*id2PublicKey[peer->getComputerId()]*/);
			} else {
				params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", "ERROR, CANT FIND");
			}
			//relance
			peer_idx++;
		}

		params_server_db.save();
		//try(BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(filePath, false))) {
		//	ByteBuff bufferReac = new ByteBuff(1024);

		//	//write
		//	std::cout+serv.getPeerId() % 100 + " write Cid : " + clusterId+"\n";
		//	bufferReac.reset().putLong(clusterId).flip().write(out);

		//	//write id
		//	std::cout+serv.getPeerId() % 100 + " write id : " + myComputerId+"\n";
		//	bufferReac.reset().putShort(myComputerId).flip().write(out);

		//	//write passphrase
		//	bufferReac.reset().putShort((short)0).putUTF8(passphrase);
		//	bufferReac.rewind().putShort((short)(bufferReac.limit() - 2));
		//	bufferReac.rewind().write(out);

		//	//write pubKey
		//	byte[] encodedPubKey = publicKey.getEncoded();
		//	bufferReac.reset().putInt(encodedPubKey.length).put(encodedPubKey).flip().write(out);
		//	//			log(std::string("publicKeyEncodingOfrmat : "+publicKey.getFormat()+"\n";
		//	//			log(std::string("publickey size : "+encodedPubKey.length + " => "+Arrays.tostd::string(encodedPubKey)+"\n";
		//	//			try {
		//	//				X509EncodedKeySpec bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//	//				KeyFactory keyFactory = KeyFactory.getInstance("RSA");
		//	//				log(std::string("my pub key = "+keyFactory.generatePublic(bobPubKeySpec).getFormat()+"\n";
		//	//			} catch (NoSuchAlgorithmException | InvalidKeySpecException e) {
		//	//				throw new RuntimeException(e);
		//	//			}

		//				//write privKey
		//	byte[] encodedPrivKey = privateKey.getEncoded();
		//	bufferReac.reset().putInt(encodedPrivKey.length).put(encodedPrivKey).flip().write(out);
		//	//			log(std::string("privateKey size : "+encodedPrivKey.length + " => "+Arrays.tostd::string(encodedPrivKey)+"\n";
		//	//			log(std::string("privateKeyencodingformat : "+privateKey.getFormat()+"\n";
		//	//			createPrivKey(encodedPrivKey);
		//	//			PKCS#8



		//				//write peers
		//	synchronized(registeredPeers) {
		//		int nbPeers = 0;
		//		for (Peer p : registeredPeers) {
		//			if (id2PublicKey.get(p.getComputerId()) != null) {
		//				nbPeers++;
		//			}
		//		}
		//		bufferReac.reset().putInt(nbPeers).flip().write(out);
		//		for (int i = 0; i < nbPeers; i++) {
		//			Peer p = registeredPeers.get(i);
		//			if (id2PublicKey.get(p.getComputerId()) != null) {
		//				//save ip
		//				bufferReac.reset().putShortUTF8(p.getIP()).flip().write(out);
		//				//save port
		//				bufferReac.reset().putInt(p.getPort()).flip().write(out);
		//				//save id
		//				bufferReac.reset().putShort(p.getComputerId()).flip().write(out);
		//				//savePubKey
		//				encodedPubKey = id2PublicKey.get(p.getComputerId()).getEncoded();
		//				bufferReac.reset().putInt(encodedPubKey.length).put(encodedPubKey).flip().write(out);
		//			}
		//		}
		//	}

		//	out.flush();
		//}
		//catch (IOException e1) {
		//	throw new RuntimeException(e1);
		//}
	}

	//void ServerIdDb::requestPublicKey(Peer& peer) {
	//	log(std::to_string(serv.getPeerId() % 100) + " (requestPublicKey) emit GET_SERVER_PUBLIC_KEY to " + peer.getPeerId() % 100+"\n");
	//	peer.writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
	//}

	//send our public key to the peer
	void ServerIdDb::sendPublicKey(Peer& peer) {
		//std::cout+ &serv + " (sendPublicKey) emit GET_SERVER_PUBLIC_KEY to " + peer+"\n";
		log(std::to_string(serv.getPeerId() % 100) + " (sendPublicKey) emit SEND_SERVER_PUBLIC_KEY to " + peer.getPeerId() % 100+"\n");

		ByteBuff buff;
		//my pub key
		//uint8_t[] encodedPubKey = publicKey.getEncoded();
		const uint8_t* encodedPubKey = reinterpret_cast<const uint8_t*>(&publicKey[0]);
		size_t encodedPubKey_size = publicKey.size();
		buff.putInt(encodedPubKey_size).put(encodedPubKey, size_t(0), encodedPubKey_size);
		//send packet
		peer.writeMessage(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, buff.flip());

	}

	void ServerIdDb::receivePublicKey(Peer& p, ByteBuff& buffIn) {
		log(std::to_string(serv.getPeerId() % 100) + " (receivePublicKey) receive SEND_SERVER_PUBLIC_KEY from " + p.getPeerId() % 100+"\n");
		
		//get pub Key
		int nbBytes = buffIn.getInt();
		//byte[] encodedPubKey = new byte[nbBytes];
		std::shared_ptr<uint8_t> encodedPubKey{ new uint8_t[nbBytes + 1] };
		buffIn.get(encodedPubKey.get(), 0, nbBytes);
		encodedPubKey.get()[nbBytes] = 0;
		const char* cstr_pub_key = reinterpret_cast<const char*>(encodedPubKey.get());
		//X509EncodedKeySpec bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//KeyFactory keyFactory = KeyFactory.getInstance("RSA");
		//PublicKey distPublicKey = keyFactory.generatePublic(bobPubKeySpec);
		PublicKey distPublicKey{ cstr_pub_key };
		{ std::lock_guard lock(tempPubKey_mutex);
			tempPubKey[p.getPeerId()] = distPublicKey;
			//				std::cout+serv.getId()%100+" (receivePublicKey) peer "+p.getConnectionId()%100+" has now a pub key of "+tempPubKey.get(p.getConnectionId())+"\n";
		}
	}

	std::string ServerIdDb::createMessageForIdentityCheck(Peer& peer, bool forceNewOne) {
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
	ServerIdDb::Identityresult ServerIdDb::sendIdentity(Peer& peer, const std::string& messageToEncrypt, bool isRequest) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") emit " + (isRequest ? "GET_IDENTITY" : "SEND_IDENTITY") + " to " + peer.getPeerId() % 100 + ", with message 2encrypt : " + messageToEncrypt+"\n");
		//check if we have the public key of this peer
		PublicKey theirPubKey = "";
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer.getPeerId());
			if (it_theirPubKey == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") i don't have public key! why are you doing that? Request his one!"+"\n");
				return ServerIdDb::Identityresult::NO_PUB;
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
			return ServerIdDb::Identityresult::BAD;
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

		buff.putInt(buffEncoded.limit()).put(buffEncoded);
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") message : " + messageToEncrypt+"\n");
		//log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") Encryptmessage : " + Arrays.tostd::string(buffEncoded.rewind().array())+"\n");

		//send packet
		peer.writeMessage(isRequest ? *UnnencryptedMessageType::GET_VERIFY_IDENTITY : *UnnencryptedMessageType::SEND_VERIFY_IDENTITY, buff.flip());

		return ServerIdDb::Identityresult::OK;
	}

	ByteBuff ServerIdDb::getIdentityDecodedMessage(const PublicKey& key, ByteBuff& buffIn) {
		//get msg
		int nbBytes = buffIn.getInt();
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

	ServerIdDb::Identityresult ServerIdDb::answerIdentity(Peer& peer, ByteBuff& buffIn) {
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

	ServerIdDb::Identityresult ServerIdDb::receiveIdentity(PeerPtr peer, ByteBuff& message) {
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
			//now check if this id isn't already taken.
			{ std::lock_guard lock(registeredPeers_mutex);
				foreach(peer, registeredPeers) {
					if ((*peer)->getComputerId() == distId) {
						if (!(*peer)->isAlive()) {
							(*peer)->close();
							peer.erase();
							log(std::string("Seems like computer ") + distId + " has reconnected!"+"\n");
							return Identityresult::BAD;
						} else {
							log(std::string("error, cluster id ") + distId + " already taken for " + (*peer)->getPeerId() % 100 + " "+"\n");
							//TODO: emit something to let it know we don't like his clusterId
							return Identityresult::BAD;
						}
					}
				}
			}

			bool found = false;
			PublicKey found_public_key;
			//check if the public key is the same
			{ std::lock_guard lock(id2PublicKey_mutex);
				auto search = this->id2PublicKey.find(distId);
				found = search != this->id2PublicKey.end();
				if (found) { found_public_key = search->second; }
			}
			// if not already in the storage and the key is valid, then accept & store it.
			if (!found || found_public_key == "" && distId > 0) {
				log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) assign new publickey  for computerid " + distId + " , connId=" + peer->getPeerId() % 100+"\n");
				//validate this peer
				{ std::lock_guard lock(id2PublicKey_mutex);
					this->id2PublicKey[distId] = theirPubKey;
				}
				{ std::lock_guard lock(registeredPeers_mutex);
					this->registeredPeers.push_back(peer);
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
				//if (!Arrays.equals(this->id2PublicKey.get(distId).getEncoded(), theirPubKey.getEncoded())) {
				if (found_public_key != theirPubKey) {
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, cluster id " + distId + " has a wrong public key (not the one i registered) " + peer->getPeerId() % 100 + " "+"\n");
					//error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) what i have : " + Arrays.tostd::string(this->id2PublicKey.get(distId).getEncoded())+"\n");
					//error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) what i received : " + Arrays.tostd::string(this->id2PublicKey.get(distId).getEncoded())+"\n");
					return Identityresult::BAD;
				} else if (distId <= 0) {
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, cluster id " + distId + " hasn't a clusterId > 0 "+"\n");
					return Identityresult::BAD;
				} else {
					log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) publickey ok for computerid " + distId+"\n");
					peer->setComputerId(distId);
					{ std::lock_guard lock(registeredPeers_mutex);
						if (!contains(registeredPeers, peer)) {
							this->registeredPeers.push_back(peer);
						}
					}
					//OK, now request a aes key
				}
			}

		}

			//if (peer->hasState(Peer::PeerConnectionState::HAS_VERIFIED_COMPUTER_ID)) {
			//	// easy optional leader election (add 'true||' if you want to test the proposal-conflict-resolver-algorithm).
			//	if (this->serv.getPeerId() > peer->getPeerId()) {
			//		sendAesKey(*peer, AES_PROPOSAL);
			//	} else {
			//		requestSecretKey(*peer);
			//	}
			//}

		return Identityresult::OK;
		
	}


	void ServerIdDb::requestSave() {
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

	bool ServerIdDb::has_aes(const Peer& p) {
		std::lock_guard loc{ id2AesKey_mutex };
		return this->id2AesKey.find(p.getComputerId()) != this->id2AesKey.end();
	}

	//Cipher ServerIdDb::getSecretCipher(Peer p, int mode) {
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

	void ServerIdDb::sendAesKey(Peer& peer, uint8_t aesState) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) emit SEND_SERVER_AES_KEY state:" + (int)(aesState)+" : " + ((aesState & AES_CONFIRM) != 0 ?  "CONFIRM" : "PROPOSAL") + " to " + peer.getPeerId() % 100+"\n");

		//check if i'm able to do this
		if (peer.getComputerId() >= 0 && isChoosen(peer.getComputerId())) {

			SecretKey secretKey;
			{ std::lock_guard lock(id2AesKey_mutex);
				auto& it_secretKey = id2AesKey.find(peer.getComputerId());
				if (it_secretKey == id2AesKey.end()) {
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
					id2AesKey[peer.getComputerId()] = secretKey;
				} else {
					secretKey = it_secretKey->second;
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
			//buffMsg.putInt(buffEncodedPrivPub.limit()).put(buffEncodedPrivPub);
			//log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) EncryptKey2 : " + Arrays.tostd::string(buffEncodedPrivPub.array())+"\n");
			buffMsg.putInt(secretKey.size()).put(secretKey);

			//send packet
			peer.writeMessage(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, buffMsg.flip());

		} else {
			log(std::string("Error, peer ") + peer.getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}
	}

	void ServerIdDb::receiveAesKey(Peer& peer, ByteBuff& message) {
		log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY" + " from " + peer.getPeerId() % 100+"\n");
		//check if i'm able to do this
		if (peer.getComputerId() >= 0 && isChoosen(peer.getComputerId())) {
			//decrypt the key
			//0 : get the message
			uint8_t aesStateMsg = message.get();
			log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY state:" + (int)(aesStateMsg)+" : " + ((aesStateMsg & AES_CONFIRM) != 0 ? "CONFIRM" : "PROPOSAL")+"\n");
			int nbBytesMsg = message.getInt();
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
			{ std::lock_guard lock(id2AesKey_mutex);
				auto& it_secretKey = id2AesKey.find(peer.getComputerId());
				if (it_secretKey == id2AesKey.end()) {
					//store the new one
					id2AesKey[peer.getComputerId()] = secretKeyReceived;
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) use new one: " + aes_log_str +"\n");
					//peer.changeState(Peer::PeerConnectionState::CONNECTED_W_AES, true);
					if ((aesStateMsg & AES_CONFIRM) != 0) {
						shouldEmit = AES_CONFIRM;
					}
				} else if (it_secretKey->second == secretKeyReceived) {
					//same, no problem
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: "+ aes_log_str +"\n");
					//peer.changeState(Peer::PeerConnectionState::CONNECTED_W_AES, true);
					if ((aesStateMsg & AES_CONFIRM) == 0) {
						log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: so now i will send a message to validate it.\n");
						shouldEmit = AES_CONFIRM;
					}
				} else {
					//error, conflict?
					//if (/*peer.hasState(Peer::PeerConnectionState::CONNECTED_W_AES)*/) {
					//	error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, receive a 'late' 'proposal?" + (aesStateMsg == 0) + "'"+"\n");
					//	//already confirmed, use current one
					//	//emit confirm if we need it
					//	if ((aesStateMsg & AES_CONFIRM) == 0) {
					//		shouldEmit = AES_CONFIRM;
					//	}
					//} else 
					if ((aesStateMsg & AES_CONFIRM) != 0) {
						// he blocked this one, choose it
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, receive a contradict confirm"+"\n");
						id2AesKey[peer.getComputerId()] = secretKeyReceived;
						//peer.changeState(Peer::PeerConnectionState::CONNECTED_W_AES, true);
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
							id2AesKey[peer.getComputerId()] = secretKeyReceived;
							log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) conflict: use new: "+ aes_log_str +"\n");
						} else {
							//nothing, we keep the current one
							std::string aes_log_str_old;
							for (uint8_t c : id2AesKey[peer.getComputerId()]) {
								aes_log_str_old += to_hex_str(c);
							}
							log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) conflict: use old : "+ aes_log_str_old + "\n");
						}
						//notify (outside of sync group)
						shouldEmit = AES_PROPOSAL | AES_CONFLICT_DETECTED;
					}
				}
			}
			if (shouldEmit != uint8_t(-1)) {
				sendAesKey(peer, shouldEmit);
			}


		} else {
			log(std::string("Error, peer ") + peer.getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}
	}



	void ServerIdDb::addPeer(uint16_t computerId) {
		//check the computerId
		if (this->serv.getComputerId() != computerId && computerId > 0) {
			this->id2PublicKey[computerId] = "";
		}
	}


} // namespace supercloud
