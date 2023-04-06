
#include "IdentityManager.hpp"
#include "PhysicalServer.hpp"
#include "utils/Parameters.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>

namespace supercloud{

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
			createNewPublicKey(EncryptionType::RSA);
		} else {

			if (paramsNet.has("ClusterId")) {
				log(std::string("set ClusterId to ") + uint64_t(paramsNet.getLong("ClusterId")) + " (= " + paramsNet.get("ClusterId") + " )"
					+ " => " + (uint64_t(paramsNet.getLong("ClusterId")) % 100) + "\n");
				clusterId = paramsNet.getLong("ClusterId");
			}
			if (paramsNet.has("ClusterPassphrase")) {
				log(std::string("set passphrase to ") + paramsNet.get("ClusterPassphrase") + "\n");
				m_passphrase = paramsNet.get("ClusterPassphrase");
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

			if (paramsNet.has("SecretKeyType")) {
				setEncryption(paramsNet.get("SecretKeyType") == "AES" ? EncryptionType::AES : paramsNet.get("SecretKeyType") == "NONE" ? EncryptionType::NO_ENCRYPTION : EncryptionType::NAIVE);
			} else {
				setEncryption(getDefaultAESEncryptionType());
			}
			if (!paramsNet.has("PubKey") || !paramsNet.has("PrivKey")) {
				log("no priv/pub key defined, creating some random ones\n");
				if (paramsNet.has("PubKeyType")) {
					createNewPublicKey(paramsNet.get("PubKeyType") == "RSA" ? EncryptionType::RSA : paramsNet.get("PubKeyType") == "NONE" ? EncryptionType::NO_ENCRYPTION : EncryptionType::NAIVE);
				} else {
					createNewPublicKey(getDefaultRSAEncryptionType());
				}
			} else {
				// copy keys
				this->m_private_key = from_hex(paramsNet.get("PrivKey"));
				this->m_public_key.type = paramsNet.get("PubKeyType") == "RSA" ? EncryptionType::RSA : paramsNet.get("PubKeyType") == "NONE" ? EncryptionType::NO_ENCRYPTION : EncryptionType::NAIVE;
				this->m_public_key.raw_data = from_hex(paramsNet.get("PubKey"));
				// this->publicKey is just a cache for m_peer_2_peerdata[getComputerId()].rsa_public_key
				{std::lock_guard lock(m_peer_data_mutex);
					assert(m_peer_2_peerdata.find(m_myself) != m_peer_2_peerdata.end());
					m_peer_2_peerdata[this->m_myself].rsa_public_key = this->m_public_key;
				}
			}
		}

		//but create new data!
		requestSave();

		//remove ClusterPwd in the conf file (to encrypt it, it doesn't protect it but just obfuscate a little)
		if (paramsNet.has("ClusterPwd")) {
			// it's not very useful, so for the beta phase, let it commented
#ifndef _DEBUG
					paramsNet.set("ClusterPwd","deleted");
#endif
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
		this->m_passphrase = params_server_db.get("passphrase");
		this->m_public_key.type = EncryptionType(params_server_db.getInt("publicKeyType"));
		this->m_public_key.raw_data = from_hex(params_server_db.get("publicKey"));
		this->m_private_key = from_hex(params_server_db.get("privateKey"));
		assert(m_public_key.type > EncryptionType::NO_ENCRYPTION);
		assert(m_public_key.type < EncryptionType::AES);
		assert(m_public_key.raw_data.size() != 0);
		{std::lock_guard lock(m_peer_data_mutex);
			assert(m_peer_2_peerdata.find(m_myself) != m_peer_2_peerdata.end());
			m_peer_2_peerdata[this->m_myself].rsa_public_key = this->m_public_key;
		}
		size_t nbPeers = size_t(params_server_db.getInt("nbPeers", 0));
		//FIXME: isn't it more safe to ock the two locks? (but there is then a deadlock possibility...). Or maybe have only one lock?
		{std::lock_guard lock(m_peer_data_mutex);
			for (size_t i = 0; i < nbPeers; i++) {
				ComputerId dist_id = ComputerId(params_server_db.getInt(std::string("peer") + std::to_string(i) + "_cid"));
				PeerPtr p = Peer::create(serv, "", 0, Peer::ConnectionState::DATABASE);
				Peer::ComputerIdSetter::setComputerId(*p,dist_id);
				assert(dist_id != 0);
				assert(dist_id != NO_COMPUTER_ID);
				loaded_peers_copy.push_back(p);
				assert(m_peer_2_peerdata.find(p) != m_peer_2_peerdata.end());
				//create associated peerdata
				PeerData& data = m_peer_2_peerdata[p];
				data.rsa_public_key.type = EncryptionType(params_server_db.getInt(std::string("peer") + std::to_string(i) + "_publicKeyType"));
				data.rsa_public_key.raw_data = from_hex(params_server_db.get(std::string("peer") + std::to_string(i) + "_publicKey"));
				const std::string& dist_ip = params_server_db.get(std::string("peer") + std::to_string(i) + "_ip");
				uint16_t dist_port = uint16_t(params_server_db.getInt(std::string("peer") + std::to_string(i) + "_port"));
				bool dist_conn_by_me = params_server_db.getBool(std::string("peer") + std::to_string(i) + "_succeed");
				std::string dist_conn_from = params_server_db.get(std::string("peer") + std::to_string(i) + "_from");
				//TODO save/get the "from";
				if (dist_ip != "" || dist_port != 0) {
					data.private_interface = PeerConnection{ dist_ip , dist_port, dist_conn_by_me, split(dist_conn_from, '$') };
				} else {
					data.private_interface.reset();
				}
				data.peer = p;
				for (size_t idx_net2conn = 0; params_server_db.has(std::string("peer") + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_addr"); ++idx_net2conn) {
					PeerConnection connection = data.public_interfaces.emplace_back();
					connection.address = params_server_db.get(std::string("peer") + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_addr");
					connection.port = (uint16_t)params_server_db.getInt(std::string("peer") + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_port");
					connection.first_hand_information = params_server_db.getBool(std::string("peer") + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_succeed");
					connection.success_from = split(params_server_db.get(std::string("peer") + std::to_string(i) + "_pub_" + std::to_string(idx_net2conn) + "_from"), '$');
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
		params_server_db.set("publicIp", this->m_myself->getIP());
		params_server_db.setInt("publicPort", this->m_myself->getPort());
		params_server_db.set("passphrase", this->m_passphrase);
		params_server_db.setInt("publicKeyType", uint8_t(this->m_public_key.type));
		params_server_db.set("publicKey", to_hex(this->m_public_key.raw_data));
		params_server_db.set("privateKey", to_hex(this->m_private_key));
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
					params_server_db.setInt(std::string("peer") + std::to_string(peer_idx) + "_cid", peer->getComputerId());
					const PeerData& data = found->second;
					params_server_db.setInt(std::string("peer") + std::to_string(peer_idx) + "_publicKeyType", uint8_t(data.rsa_public_key.type));
					params_server_db.set(std::string("peer") + std::to_string(peer_idx) + "_publicKey", to_hex(data.rsa_public_key.raw_data));
					if (data.private_interface) {
						params_server_db.set(std::string("peer") + std::to_string(peer_idx) + "_ip", data.private_interface->address);
						params_server_db.setInt(std::string("peer") + std::to_string(peer_idx) + "_port", data.private_interface->port);
						params_server_db.setBool(std::string("peer") + std::to_string(peer_idx) + "_succeed", data.private_interface->first_hand_information);
						params_server_db.set(std::string("peer") + std::to_string(peer_idx) + "_from", concatenate(data.private_interface->success_from, '$'));
					}
					size_t idx_conn = 0;
					for (const PeerConnection& net_interface : data.public_interfaces) {
						params_server_db.set(std::string("peer") + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_addr", net_interface.address);
						params_server_db.setInt(std::string("peer") + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_port", net_interface.port);
						params_server_db.setBool(std::string("peer") + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_success", net_interface.first_hand_information);
						params_server_db.set(std::string("peer") + std::to_string(peer_idx) + "_pub_" + std::to_string(idx_conn) + "_from", concatenate(net_interface.success_from, '$'));
						++idx_conn;
					}
				} else {
					params_server_db.set(std::string("peer") + std::to_string(peer_idx) + "_ERROR", "ERROR, CANT FIND DATA");
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
		buff.put(uint8_t(m_public_key.type));
		//my pub key
		buff.putSize(m_public_key.raw_data.size()).put(m_public_key.raw_data);
		//send packet
		peer->writeMessage(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, buff.flip());
		log(std::string("PUBKEY:Send with") + uint8_t(m_public_key.type) + " : " + (m_myself->getPeerId() % 100) + "->" + (peer->getPeerId() % 100));
	}

	void IdentityManager::receivePublicKey(PeerPtr sender, const ByteBuff& buff_in) {
		//log(std::to_string(serv.getPeerId() % 100) + " (receivePublicKey) receive SEND_SERVER_PUBLIC_KEY from " + sender->getPeerId() % 100+"\n");
		//ensure we have create a peer data for this peer
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = m_peer_2_peerdata.find(sender); it == m_peer_2_peerdata.end()) {
				assert(sender->getState() & Peer::TEMPORARY);
				m_peer_2_peerdata[sender].peer = sender;
			}
		}
		//get encryption type
		EncryptionType rsa_encryption_type = EncryptionType(buff_in.get());
		log(std::string("PUBKEY:Get with") + uint8_t(rsa_encryption_type) + " : " + (m_myself->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100));
		//get pub Key
		size_t nb_bytes = buff_in.getSize();
		std::vector<uint8_t> dist_public_key_raw_data = buff_in.get(nb_bytes);
		//X509EncodedKeySpec bobPubKeySpec = new X509EncodedKeySpec(encodedPubKey);
		//KeyFactory keyFactory = KeyFactory.getInstance("RSA");
		//PublicKey distPublicKey = keyFactory.generatePublic(bobPubKeySpec);
		{ std::lock_guard lock(tempPubKey_mutex);
			tempPubKey[sender->getPeerId()] = { rsa_encryption_type, dist_public_key_raw_data };
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
		} else {
			msg = it_msg->second;
			log(std::string(" (createMessageForIdentityCheck) We already emit a request for indentity to ") + peer.getPeerId() % 100 + " with message " + m_pid_2_emitted_msg[peer.getPeerId()]+"\n");
		}
		return msg;
	}

	//send our public key to the peer, with the message encoded
	IdentityManager::Identityresult IdentityManager::sendIdentity(PeerPtr peer, const std::string& message_to_encrypt, bool isRequest) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") emit " + (isRequest ? "GET_IDENTITY" : "SEND_IDENTITY") + " to " + peer->getPeerId() % 100 + ", with message 2encrypt : " + message_to_encrypt +"\n");
		//check if we have the public key of this peer
		PublicKeyHolder their_pub_key;
		{ std::lock_guard lock(tempPubKey_mutex);
			if (const auto& it_their_pub_key = tempPubKey.find(peer->getPeerId()); it_their_pub_key == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity " + isRequest + ") i don't have public key! why are you doing that? Request his one!"+"\n");
				return IdentityManager::Identityresult::NO_PUB;
			} else {
				their_pub_key = it_their_pub_key->second;
			}
		}
		assert(their_pub_key.type > EncryptionType::UNKNOWN);
		assert(!their_pub_key.raw_data.empty());

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
		buff.put(uint8_t(m_public_key.type));
		log(std::string("PUBKEY:SendV with") + uint8_t(m_public_key.type) + " : " + (m_myself->getPeerId() % 100) + "->" + (peer->getPeerId() % 100));
		//encode msg with our private key => no, it's useless!
		//Cipher cipher = Cipher.getInstance("RSA");
		//cipher.init(Cipher.ENCRYPT_MODE, privateKey);
		//ByteBuff buffEncoded = blockCipher(new ByteBuff().serializeComputerId(myComputerId).putUTF8(messageToEncrypt).putUTF8(passphrase).flip().toArray(), Cipher.ENCRYPT_MODE, cipher);
		ByteBuff buff_encoded;
		buff_encoded.serializeComputerId(getComputerId())
			.putUTF8(message_to_encrypt)
			.putUTF8(m_passphrase).flip();
		std::vector<uint8_t> message_encoded = buff_encoded.getAll();
		encrypt(message_encoded, their_pub_key);

		buff.putSize(message_encoded.size()).put(message_encoded);
		log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") message : " + message_to_encrypt+"\n");
		//log(std::to_string(serv.getPeerId() % 100) + " (sendIdentity" + isRequest + ") Encryptmessage : " + Arrays.tostd::string(buffEncoded.rewind().array())+"\n");

		//send packet
		peer->writeMessage(isRequest ? *UnnencryptedMessageType::GET_VERIFY_IDENTITY : *UnnencryptedMessageType::SEND_VERIFY_IDENTITY, buff.flip());

		return IdentityManager::Identityresult::OK;
	}


	ByteBuff IdentityManager::getIdentityDecodedMessage(const PublicKeyHolder& key, const ByteBuff& buff) {
		//get msg
		size_t nbBytes = buff.getSize();
		std::vector<uint8_t> data = buff.get(nbBytes);

		decrypt(data, key);

		ByteBuff buffDecoded;
		buffDecoded.put(data).flip();
		return buffDecoded;
	}

	IdentityManager::Identityresult IdentityManager::answerIdentity(PeerPtr peer, const ByteBuff& buff_in) {
		//log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) receive GET_IDENTITY to " + peer->getPeerId() % 100+"\n");


		PublicKeyHolder their_pub_key;
		{ std::lock_guard lock(tempPubKey_mutex);
			if (const auto& it_their_pub_key = tempPubKey.find(peer->getPeerId()); it_their_pub_key == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (answerIdentity)i don't have public key! why are you doing that? Request his one!"+"\n");
				return Identityresult::NO_PUB;
			} else {
				their_pub_key = it_their_pub_key->second;
			}
		}
		assert(their_pub_key.type > EncryptionType::UNKNOWN);
		assert(!their_pub_key.raw_data.empty());

		//check if the other side is ok and sent us something (it's a pre-emptive check)
		if (buff_in.limit() <= 3) {
			error(std::to_string(serv.getPeerId() % 100) + " (answerIdentity) he ask use somethign whithout a message! it's crazy!!! " + peer->getComputerId() + " : " + peer->getPeerId() % 100+"\n");
			//not ready, maybe he didn't choose his computerid yet?
			//we have to ask him a bit later.
			//ping() is already in charge of that
			return Identityresult::BAD;
		}

		EncryptionType encryption = EncryptionType(buff_in.get());
		log(std::string("PUBKEY:GetV with") + uint8_t(encryption) + " : " + (m_myself->getPeerId() % 100) + "<-" + (peer->getPeerId() % 100));
		assert(their_pub_key.type == encryption);
		ByteBuff buffDecoded = getIdentityDecodedMessage(their_pub_key, buff_in);
		ComputerId unverifiedCompId = buffDecoded.deserializeComputerId(); ///osef dist_cid = because we can't verify it.
		std::string msgDecoded = buffDecoded.getUTF8();
		std::string theirPwd = buffDecoded.getUTF8();
		if (theirPwd != (m_passphrase)) {
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


		PublicKeyHolder their_pub_key;
		{ std::lock_guard lock(tempPubKey_mutex);
			const auto& it_theirPubKey = tempPubKey.find(peer->getPeerId());
			if (it_theirPubKey == tempPubKey.end()) {
				//request his key
				log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity)i don't have public key! why are you doing that? Request his one!"+"\n");
				return Identityresult::NO_PUB;
			} else {
				their_pub_key = it_theirPubKey->second;
			}
		}
		assert(their_pub_key.type > EncryptionType::UNKNOWN);
		assert(!their_pub_key.raw_data.empty());

		//check if the other side is ok and sent us something
		if (message.limit() == 2) {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) he doesn't want to give us his identity! " + peer->getComputerId() + " : " + peer->getPeerId() % 100+"\n");
			//not ready, maybe he didn't choose his computerid yet?
			//we have to ask him a bit later.
			//ping() is already in charge of that
			return Identityresult::BAD;
		}

		EncryptionType encryption = EncryptionType(message.get());
		assert(their_pub_key.type == encryption);
		log(std::string("PUBKEY:GetV2 with") + uint8_t(encryption) + " : " + (m_myself->getPeerId() % 100) + "<-" + (peer->getPeerId() % 100));
		ByteBuff buff_decoded = getIdentityDecodedMessage(their_pub_key, message);

		//data extracted
		ComputerId dist_cid = buff_decoded.deserializeComputerId();
		std::string msgDecoded = buff_decoded.getUTF8();
		std::string theirPwd = buff_decoded.getUTF8();


		if (dist_cid == NO_COMPUTER_ID || m_pid_2_emitted_msg.find(peer->getPeerId()) == m_pid_2_emitted_msg.end()) {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) BAD receive computerid " + dist_cid + " and i " + (m_pid_2_emitted_msg.find(peer->getPeerId()) != m_pid_2_emitted_msg.end()) + " emmitted a message"+"\n");
			//the other peer doesn't have an computerid (yet)
			if(m_pid_2_emitted_msg.find(peer->getPeerId()) != m_pid_2_emitted_msg.end())
				m_pid_2_emitted_msg.erase(peer->getPeerId());
			return Identityresult::BAD;
		} else {
			log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) GOOD receive computerid " + dist_cid + " and i " + (m_pid_2_emitted_msg.find(peer->getPeerId()) != m_pid_2_emitted_msg.end()) + " emmitted a message"+"\n");
		}
		if (theirPwd != (m_passphrase)) {
			log(std::string("Error: peer ") + peer->getPeerId() % 100 + " : " + dist_cid + " has not the same pwd as us (2)."+"\n");
			peer->close(); //TODO: verify that Physical server don't revive it.
			//TODO : remove it from possible server list
			return Identityresult::BAD;
		}

		log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) i have sent to " + peer->getPeerId() % 100 + " the message " + (m_pid_2_emitted_msg.find(peer->getPeerId())->second) + " to encode. i have received " + msgDecoded + " !!!"+"\n");

		//check if this message is inside our peer
		if ( (m_pid_2_emitted_msg.find(peer->getPeerId())->second) == (msgDecoded)) {

			bool found = false;
			PublicKeyHolder found_public_key;
			//check if the public key is the same
			{ std::lock_guard lock(m_peer_data_mutex);
				auto search = this->m_peer_2_peerdata.find(peer);
				found = search != this->m_peer_2_peerdata.end();
				if (found) { found_public_key = search->second.rsa_public_key; }
			}
			// if not already in the storage and the key is valid, then accept & store it.
			if (!found || found_public_key.raw_data.empty() && dist_cid > 0) {
				std::string rsa_log_str;
				for (uint8_t c : their_pub_key.raw_data) {
					rsa_log_str += u8_hex(c);
				}
				log(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) assign new publickey "+ their_pub_key.raw_data.size() + "{" + rsa_log_str + "} for computerid " + dist_cid + " , their pid=" + peer->getPeerId() % 100+"\n");
				//validate this peer
				{ std::lock_guard lock(m_peer_data_mutex);
					assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
					this->m_peer_2_peerdata[peer].rsa_public_key = their_pub_key;
					log(std::string("PUBKEY:Validate with") + uint8_t(their_pub_key.type) + " : " + (m_myself->getPeerId() % 100) + "<-" + (peer->getPeerId() % 100));
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
				if (found_public_key.raw_data != their_pub_key.raw_data || found_public_key.type != their_pub_key.type) {
					std::string rsa_log_str_sto;
					for (uint8_t c : found_public_key.raw_data) {
						rsa_log_str_sto += u8_hex(c);
					}
					std::string rsa_log_str;
					for (uint8_t c : their_pub_key.raw_data) {
						rsa_log_str += u8_hex(c);
					}
					error(std::to_string(serv.getPeerId() % 100) + " (receiveIdentity) error, computer id " + dist_cid + " has a wrong public key {"+ rsa_log_str +"}"+ uint8_t(their_pub_key.type) 
						+" (not the one i registered:{" + rsa_log_str_sto + "}"+ uint8_t(found_public_key.type) +") " + peer->getPeerId() % 100 + " "+"\n");
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
		assert(this->m_peer_2_peerdata.find(peer) != this->m_peer_2_peerdata.end());
		assert(m_peer_2_peerdata[peer].rsa_public_key.type == their_pub_key.type);
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

	//note: the proposal/confirm thing work because i set my aes key before i emit my proposal.

	void IdentityManager::sendAesKey(PeerPtr peer, uint8_t aesState) {
		log(std::to_string(serv.getPeerId() % 100) + " (sendAesKey) emit SEND_SERVER_AES_KEY state:" + (int)(aesState)+" : " + ((aesState & AES_CONFIRM) != 0 ?  "CONFIRM" : "PROPOSAL") + " to " + peer->getPeerId() % 100+"\n");

		assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
		//check if i'm able to do this
		bool has_pub_key = false;
		PublicKeyHolder their_pub_key;
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = this->m_peer_2_peerdata.find(peer); it != this->m_peer_2_peerdata.end() && peer->getComputerId() >= 0) {
				has_pub_key = !it->second.rsa_public_key.raw_data.empty();
				their_pub_key = it->second.rsa_public_key;
			}
		}
		if (has_pub_key) {

			SecretKey secret_key;
			{ std::lock_guard lock(m_peer_data_mutex);
				assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
				PeerData& data_peer = m_peer_2_peerdata[peer];
				if (data_peer.aes_key.size() == 0) {
					//create new aes key
					secret_key = data_peer.aes_key = this->createNewSecretKey(m_encryption_type);
				} else {
					secret_key = data_peer.aes_key;
				}
			}

			//encrypt the key
			ByteBuff buff_msg;
			buff_msg.put(uint8_t(m_encryption_type));
			assert(secret_key.size() > 0);
			buff_msg.putSize(secret_key.size()).put(secret_key);

			//encode msg 
			std::vector<uint8_t> data_to_encode = buff_msg.flip().getAll();
			encrypt(data_to_encode, their_pub_key);

			//send it
			buff_msg.reset();
			buff_msg.put(aesState);
			buff_msg.put(uint8_t(m_public_key.type));
			buff_msg.put(uint8_t(their_pub_key.type));
			buff_msg.putSize(data_to_encode.size()).put(data_to_encode);

			//send packet
			peer->writeMessage(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, buff_msg.flip());

		} else {
			log(std::string("Error, peer ") + peer->getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}
	}

	bool IdentityManager::receiveAesKey(PeerPtr peer, const ByteBuff& message) {
		log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY" + " from " + peer->getPeerId() % 100+"\n");
		//check if i'm able to do this
		assert(m_peer_2_peerdata.find(peer) != m_peer_2_peerdata.end());
		bool has_pub_key = false;
		PublicKeyHolder their_pub_key;
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = this->m_peer_2_peerdata.find(peer); it != this->m_peer_2_peerdata.end() && peer->getComputerId() >= 0) {
				has_pub_key = !it->second.rsa_public_key.raw_data.empty();
				their_pub_key = it->second.rsa_public_key;
			}
		}
		if (has_pub_key) {
			//decrypt the key
			//0 : get the message
			SecretKey test = message.getAll();
			message.rewind();
			uint8_t aes_state_msg = message.get();
			EncryptionType pub_key_protocol_him = EncryptionType(message.get());
			EncryptionType pub_key_protocol_me = EncryptionType(message.get());
			assert(m_public_key.type == pub_key_protocol_me);
			assert(their_pub_key.type == pub_key_protocol_him);
			size_t nb_bytes_msg = message.getSize();
			log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY state:" + (int)(aes_state_msg)+" : " + ((aes_state_msg & AES_CONFIRM) != 0 ? "CONFIRM" : "PROPOSAL")+", keysize="+ nb_bytes_msg);
			std::vector<uint8_t> aes_key_msg = message.get(nb_bytes_msg);
			std::vector<uint8_t> test_to_del = aes_key_msg;

			bool valid = decrypt(aes_key_msg, their_pub_key);
			if (!valid) {
				//there is an error with this peer: disconnect
				peer->close();
				return false;
			}
			ByteBuff buff_aes;
			buff_aes.put(aes_key_msg).flip();

			EncryptionType secret_key_protocol_him = EncryptionType(buff_aes.get());
			nb_bytes_msg = buff_aes.getSize();
			if (nb_bytes_msg > buff_aes.available()) {
				//TODO: if multiple times, disconnect this peer (or even ban him)
				assert(false);
				return false;
			}
			const SecretKey secret_key_received = buff_aes.get(nb_bytes_msg);

			//don't accept a protocol less secure than ours
			if (m_encryption_type > secret_key_protocol_him) {
				//send our key
				sendAesKey(peer, AES_PROPOSAL);
				return false;
			}
			if (m_encryption_type < secret_key_protocol_him) {
				{ std::lock_guard lock(m_peer_data_mutex);
					//assert their is no other aes-verified peers yet
					assert(std::find_if(this->m_peer_2_peerdata.begin(), this->m_peer_2_peerdata.end(), [peer](const std::pair<PeerPtr, PeerData>& p) { return !p.second.aes_key.empty() && p.first != peer; }) == this->m_peer_2_peerdata.end());
					setEncryption(secret_key_protocol_him);
					for (auto& peer2data : m_peer_2_peerdata) {
						peer2data.second.aes_key.clear();
					}
				}
			}

			std::string aes_log_str;
			for (uint8_t c : secret_key_received) {
				aes_log_str += to_hex_str(c);
			}
			log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) receive SEND_SERVER_AES_KEY Key:" + aes_log_str + " (size="+ secret_key_received.size()+")");

			//check if we already have one
			uint8_t shouldEmit = 0;
			{ std::lock_guard lock(m_peer_data_mutex);
				auto& it_secretKey = m_peer_2_peerdata.find(peer);
				if (it_secretKey == m_peer_2_peerdata.end()) {
					//store the new one
					m_peer_2_peerdata[peer].aes_key = secret_key_received;
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) use new one: " + aes_log_str +"\n");
					if ((aes_state_msg & AES_CONFIRM) != 0) {
						shouldEmit = AES_CONFIRM;
					}
				} else if (secret_key_received == it_secretKey->second.aes_key  || it_secretKey->second.aes_key.size() == 0) {
					//same, no problem
					log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: "+ aes_log_str  +" (size="+ it_secretKey->second.aes_key.size());
					if ((aes_state_msg & AES_CONFIRM) == 0) {
						log(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) same as i have: so now i will send a message to validate it.\n");
						shouldEmit = AES_CONFIRM;
					}
				} else {
					//error, conflict?
					if ((aes_state_msg & AES_CONFIRM) != 0) {
						// he blocked this one, choose it
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, receive a contradict confirm"+"\n");
						m_peer_2_peerdata[peer].aes_key = secret_key_received;
					} else {
						//it seem he sent an aes at the same time as me, so there is a conflict.
						error(std::to_string(serv.getPeerId() % 100) + " (receiveAesKey) warn, conflict"+"\n");
						//use peerid to deconflict
						bool  test = serv.getPeerId() > peer->getPeerId();
						if ((aes_state_msg & AES_CONFLICT_DETECTED) != 0) {
							// if using the peer isn't enough to deconflict, sue some random number until it converge
							test = rand_u8() % 2 == 0;
						}
						if (test) {
							//use new one
							m_peer_2_peerdata[peer].aes_key = secret_key_received;
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

			bool receive_confirm = (aes_state_msg & AES_CONFIRM) != 0 && (aes_state_msg & AES_CONFLICT_DETECTED) == 0;
			bool allow_confirm = (shouldEmit & AES_PROPOSAL) == 0 && (shouldEmit & AES_CONFLICT_DETECTED) == 0;
			return (receive_confirm && allow_confirm) || (shouldEmit == AES_CONFIRM);

		} else {
			log(std::string("Error, peer ") + peer->getPeerId() % 100 + " want an aes key but we don't have a rsa one yet!"+"\n");
		}

		return false;
	}



} // namespace supercloud
