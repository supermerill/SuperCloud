#pragma once

#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include "utils/Parameters.hpp"
#include "Peer.hpp"
#include "ConnectionMessageManager.hpp"
#include "ConnectionMessageManagerInterface.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace supercloud{

class PhysicalServer;

constexpr size_t RSA_KEY_SIZE = 3072;
	/**
	 *
	 * This class if the manager of the peers we kind of trust in the network.
	 * It store the list of them, and their keys, and some method to identify them.
	 */
class IdentityManager {
public:
	enum class EncryptionType : uint8_t{
		UNKNOWN,
		NO_ENCRYPTION,
		NAIVE,
		RSA,
		AES
	};
	struct PublicKeyHolder {
		EncryptionType type = EncryptionType::NAIVE;
		std::vector<uint8_t> raw_data;
		inline bool operator==(const PublicKeyHolder& other) const { return type == other.type && raw_data == other.raw_data; }
		inline bool operator!=(const PublicKeyHolder& other) const { return !this->operator==(other); }
		//auto operator<=>(const PublicKeyHolder&) const = default; //c++20
	};
	struct PeerConnection {
		std::string address;
		uint16_t port;
		bool first_hand_information;
		//bool intitiated_by_me;
		// I already succeed to connect to this adress/port from these networks
		std::vector<std::string> success_from;
		bool operator==(const PeerConnection& other) const { return address == other.address && port == other.port && first_hand_information == other.first_hand_information && success_from == other.success_from; }
	};
	struct PeerData {
		// the registered peer. may be in connection or connected.
		PeerPtr peer;
		PublicKeyHolder rsa_public_key;
		SecretKey aes_key;
		std::shared_ptr<void> aes_encoder; // cached encoder, to avoid recreating it each time.
		// The key is an ip (only the network part) and the value is the ip/adress where we succeffuly connect to it.
		// If we never found a way to connect to it, it's empty.
		std::vector<PeerConnection> public_interfaces;
		std::optional<PeerConnection> private_interface;
		bool operator==(const PeerData& other) const { return peer == other.peer && rsa_public_key == other.rsa_public_key && aes_key == other.aes_key 
			&& public_interfaces == other.public_interfaces && private_interface == other.private_interface; }
	};
	enum ComputerIdState : uint8_t {
		NOT_CHOOSEN = 0,
		TEMPORARY = 1,
		DEFINITIVE = 2,
	};
protected:

	std::mutex db_file_mutex;

	PrivateKey m_private_key;
	PublicKeyHolder m_public_key;

	// A fake peer to store our information
	PeerPtr m_myself;

	//database peers
	mutable std::mutex m_loaded_peers_mutex;
	PeerList m_loaded_peers;
	mutable std::mutex tempPubKey_mutex;
	std::unordered_map<PeerId, PublicKeyHolder> tempPubKey; // unidentified pub key
	mutable std::mutex m_peer_data_mutex;
	std::unordered_map<PeerPtr, PeerData> m_peer_2_peerdata;
	PhysicalServer& serv;

	ComputerIdState myComputerIdState = ComputerIdState::NOT_CHOOSEN;
	int64_t timeChooseId = 0;

	std::unordered_map<PeerId, std::string> m_pid_2_emitted_msg;
	mutable std::mutex save_mutex;
	std::shared_ptr<Parameters> m_install_parameters; // optional, to get installation information, to create the m_parameters the first launch.
	std::unique_ptr<Parameters> m_parameters; // the Parameters inside is 'mutable' as it can mute to serialize, even if we don't change our data.
	uint64_t clusterId = NO_CLUSTER_ID; // the id to identify the whole cluster
	std::string m_passphrase = "no protection"; //the passphrase of the cluster
	EncryptionType m_encryption_type = EncryptionType::UNKNOWN; // the encryption used by the cluster (should be aes), can be none for testing purpose

	void create_from_install_info();
	EncryptionType getDefaultAESEncryptionType();
	EncryptionType getDefaultRSAEncryptionType();
	void encrypt(std::vector<uint8_t>& data, const PublicKeyHolder& peer_pub_key);
	bool decrypt(std::vector<uint8_t>& data, const PublicKeyHolder& peer_pub_key); // return false if the message is corrupted or wrong pub/priv key
public:
	IdentityManager(PhysicalServer& serv, std::unique_ptr<Parameters>&& parameters) : serv(serv), m_parameters(std::move(parameters)) {
		std::string ip = "";
		uint16_t port = 0;
		if (m_parameters) {
			ip = m_parameters->get("publicIp", "");
			port = m_parameters->getInt("publicPort", 0);
		}
		m_myself = Peer::create(serv, ip, port, Peer::ConnectionState::US);
		m_peer_2_peerdata[m_myself].peer = m_myself;
	}
	IdentityManager& setInstallParameters(std::shared_ptr<Parameters> parameters) {
		m_install_parameters = parameters;
		return *this;
	}

	std::mutex& synchronize() { return db_file_mutex;  }

	std::string msg = "";
	//getters
	PeerPtr getSelfPeer() { return m_myself; }
	uint64_t getClusterId() const { return clusterId; }
	ComputerIdState getComputerIdState() const { return myComputerIdState; }
	ComputerId getComputerId() const { return m_myself->getComputerId(); }
	void setComputerId(ComputerId myNewComputerId, ComputerIdState newState);
	/// <summary>
	/// Return a copy of the list of loaded peers (from the stored file, and the data sent by the actual connected peers). Please don't modify the peers.
	/// </summary>
	/// <returns>a new vector of my peerPtr.</returns>
	PeerList getLoadedPeers() { std::lock_guard lock{ m_loaded_peers_mutex };  return m_loaded_peers; }
	PeerPtr getLoadedPeer(ComputerId cid);
	PeerData getPeerData(ComputerId cid) { return getPeerData(getLoadedPeer(cid)); }
	bool hasPeerData(PeerPtr peer) const; // shouldnt be necessary?
	PeerData getPeerData(PeerPtr peer) const;
	PeerData getSelfPeerData() const { return getPeerData(m_myself); }
	//don't use it outside of debugging
	bool setPeerData(const PeerData& original, const PeerData& new_data);

	int64_t getTimeChooseId(){ return timeChooseId; }
	PublicKey getPublicKey() const { return m_public_key.raw_data; }
#if _DEBUG
	EncryptionType getPublicKeyType() const { return  m_public_key.type; }//for tests
	PrivateKey getPrivateKey() const { return m_private_key; }//for tests
#endif
	PublicKey getPublicKey(PeerPtr key) const {
		{ std::lock_guard lock{ this->m_peer_data_mutex };
			auto it = m_peer_2_peerdata.find(key);
			if (it != m_peer_2_peerdata.end())
				return it->second.rsa_public_key.raw_data;
			return PublicKey{};
		}
	}

	//setters
	void removeBadPeer(PeerPtr badPeer) {
		{std::lock_guard lock{ m_loaded_peers_mutex };
			foreach(it , m_loaded_peers) {
				if (*it == badPeer) {
					log(std::to_string(this->getSelfPeer()->getPeerId() % 100) + " remove bad peer from idmana: " + std::to_string(badPeer->getPeerId() % 100));
					(*it)->close();
					it.erase();
					break;
				}
			}
		}
		//also erase the bad computer id
		{ std::lock_guard lock{ this->m_peer_data_mutex };
			m_peer_2_peerdata.erase(badPeer);
		}
	}

	// create our keys
	void createNewPublicKey(EncryptionType type);
	//static PrivateKey createPrivKey(const std::vector<uint8_t>& datas);
	SecretKey createNewSecretKey(EncryptionType type);

	void newClusterId() { clusterId = rand_u63(); }

	void setPassword(const std::string& pass) {
		m_passphrase = pass;
		requestSave();
	}

	//save/load the db into/from a file (both thread-unsafe)
	void load();
	void save() const;

	//	public static void main(std::string[] args) {
	////		byte[] arr = new byte[]{48, -126, 2, 118, 2, 1, 0, 48, 13, 6, 9, 42, -122, 72, -122, -9, 13, 1, 1, 1, 5, 0, 4, -126, 2, 96, 48, -126, 2, 92, 2, 1, 0, 2, -127, -127, 0, -127, -83, -115, 114, -35, 8, 54, -43, -68, -74, -122, -79, 118, 42, 102, -112, 20, -41, -26, -27, -59, -101, -48, -37, 55, 46, 56, 52, 18, 125, 34, -85, -51, 59, 13, -90, 61, 86, -117, -39, -98, 53, 28, 69, 106, 88, -51, 97, -128, 38, 75, -42, -99, -50, 97, -69, -61, 29, -114, -93, 100, 70, 74, -103, 31, -26, -125, 67, -22, 55, 117, -114, 12, 18, -108, -24, 36, -18, -27, 58, 112, -52, -127, -2, -75, -23, -122, 7, 2, 9, -126, -20, -66, 23, 98, 71, 37, -125, -77, 8, 56, -70, 58, 20, 91, -90, -4, 64, 100, 3, -32, 11, -97, -125, 30, -3, -110, -117, 111, 107, 13, -126, -50, 106, -59, -8, -112, -65, 2, 3, 1, 0, 1, 2, -127, -128, 34, 81, 10, 48, -114, 91, -127, 31, 88, -68, 56, -78, -73, -95, -118, -40, -80, 27, 94, 104, 9, -66, 45, 44, 5, -45, 62, 94, 81, 82, 58, 29, -102, -58, -8, -38, -72, 58, -79, -15, -103, -45, 86, 50, -20, 108, -87, -107, 22, -77, -117, -72, 52, -76, -117, -38, -125, 76, -52, 21, 99, 16, -46, -26, -121, -40, 30, -124, -5, -36, 1, -116, 105, 44, -6, 61, 21, -125, -22, -11, -112, -65, -34, -89, 46, -128, -25, -32, 38, -30, 40, -121, 52, -76, -95, -2, 2, 38, -57, 34, -110, -66, -83, 91, -6, 95, 16, 16, -37, 116, 12, 52, 119, -106, 33, -1, -111, -123, -53, 44, -94, 107, -99, -96, -70, 99, 63, -55, 2, 65, 0, -57, -17, 16, -94, -49, 59, 34, -36, 36, -97, -79, -79, -98, 39, -48, -67, -108, -121, -67, 44, 109, 75, 7, -17, 95, -25, 102, 95, -83, 63, -7, 13, -126, -41, 14, -1, -105, -102, -21, 67, -27, 88, -60, -11, -24, -1, 89, 0, -71, 54, 30, 31, -27, -15, 5, 58, 75, 101, 86, 68, 53, -107, 87, -51, 2, 65, 0, -90, 10, -19, 74, -84, -106, -103, 85, -67, -112, -21, -119, -117, -60, 80, 14, 85, -104, -3, 56, -15, -80, -73, 127, 58, -59, -84, 13, 85, -35, -61, 65, -4, -16, -25, -7, 48, -17, 92, -45, -36, 115, 119, 94, -97, 35, -90, 19, -51, -73, 2, 28, 57, 65, 97, -124, -7, -54, 55, 114, -73, 61, 38, -69, 2, 64, 9, -104, -10, 73, 122, 125, 50, 61, 51, 28, -33, 96, -47, 96, -61, -22, 117, -40, -42, 65, -19, -75, 46, 90, 85, 86, 60, 89, -41, 109, 60, -67, 99, 76, -125, -111, -51, 107, 72, 99, -25, -4, -116, -25, -23, 25, 104, -30, 90, 1, -71, 12, 122, -13, 72, -10, -11, 107, -107, -22, -116, 79, -16, -7, 2, 64, 58, -43, -88, 91, 75, 104, 89, -112, -50, 8, -23, -52, -27, 31, 124, -106, 119, -78, 44, 23, -33, 92, 20, -55, 26, 84, 44, -80, -44, -6, 45, 83, -42, -126, -82, 79, -40, 13, 24, -63, 97, 93, -16, -80, 48, -121, 123, 51, -115, 51, 9, -90, 98, -117, 78, 56, -58, 33, -25, 31, -40, -39, -20, 61, 2, 65, 0, -109, -31, -106, -105, 17, -33, -16, 2, -127, -78, 110, 11, 11, -31, -124, 1, -89, -95, 6, 85, 105, -81, 15, -122, -40, 28, 29, 48, 26, -32, -66, -98, -17, -77, 121, -66, 34, -75, 61, 92, -81, 103, 85, 6, 70, -98, -83, -7, -14, -33, -42, 40, -108, -11, -74, -49, 110, 92, -25, -15, 85, -29, -59, -20};
	//		byte[] arr = new byte[]{48, -126, 2, 118, 2, 1, 0, 48, 13, 6, 9, 42, -122, 72, -122, -9, 13, 1, 1, 1, 5, 0, 4, -126, 2, 96, 48, -126, 2, 92, 2, 1, 0, 2, -127, -127, 0, -127, -83, -115, 114, -35, 8, 54, -43, -68, -74, -122, -79, 118, 42, 102, -112, 20, -41, -26, -27, -59, -101, -48, -37, 55, 46, 56, 52, 18, 125, 34, -85, -51, 59, 13, -90, 61, 86, -117, -39, -98, 53, 28, 69, 106, 88, -51, 97, -128, 38, 75, -42, -99, -50, 97, -69, -61, 29, -114, -93, 100, 70, 74, -103, 31, -26, -125, 67, -22, 55, 117, -114, 12, 18, -108, -24, 36, -18, -27, 58, 112, -52, -127, -2, -75, -23, -122, 7, 2, 9, -126, -20, -66, 23, 98, 71, 37, -125, -77, 8, 56, -70, 58, 20, 91, -90, -4, 64, 100, 3, -32, 11, -97, -125, 30, -3, -110, -117, 111, 107, 13, -126, -50, 106, -59, -8, -112, -65, 2, 3, 1, 0, 1, 2, -127, -128, 34, 81, 10, 48, -114, 91, -127, 31, 88, -68, 56, -78, -73, -95, -118, -40, -80, 27, 94, 104, 9, -66, 45, 44, 5, -45, 62, 94, 81, 82, 58, 29, -102, -58, -8, -38, -72, 58, -79, -15, -103, -45, 86, 50, -20, 108, -87, -107, 22, -77, -117, -72, 52, -76, -117, -38, -125, 76, -52, 21, 99, 16, -46, -26, -121, -40, 30, -124, -5, -36, 1, -116, 105, 44, -6, 61, 21, -125, -22, -11, -112, -65, -34, -89, 46, -128, -25, -32, 38, -30, 40, -121, 52, -76, -95, -2, 2, 38, -57, 34, -110, -66, -83, 91, -6, 95, 16, 16, -37, 116, 12, 52, 119, -106, 33, -1, -111, -123, -53, 44, -94, 107, -99, -96, -70, 99, 63, -55, 2, 65, 0, -57, -17, 16, -94, -49, 59, 34, -36, 36, -97, -79, -79, -98, 39, -48, -67, -108, -121, -67, 44, 109, 75, 7, -17, 95, -25, 102, 95, -83, 63, -7, 13, -126, -41, 14, -1, -105, -102, -21, 67, -27, 88, -60, -11, -24, -1, 89, 0, -71, 54, 30, 31, -27, -15, 5, 58, 75, 101, 86, 68, 53, -107, 87, -51, 2, 65, 0, -90, 10, -19, 74, -84, -106, -103, 85, -67, -112, -21, -119, -117, -60, 80, 14, 85, -104, -3, 56, -15, -80, -73, 127, 58, -59, -84, 13, 85, -35, -61, 65, -4, -16, -25, -7, 48, -17, 92, -45, -36, 115, 119, 94, -97, 35, -90, 19, -51, -73, 2, 28, 57, 65, 97, -124, -7, -54, 55, 114, -73, 61, 38, -69, 2, 64, 9, -104, -10, 73, 122, 125, 50, 61, 51, 28, -33, 96, -47, 96, -61, -22, 117, -40, -42, 65, -19, -75, 46, 90, 85, 86, 60, 89, -41, 109, 60, -67, 99, 76, -125, -111, -51, 107, 72, 99, -25, -4, -116, -25, -23, 25, 104, -30, 90, 1, -71, 12, 122, -13, 72, -10, -11, 107, -107, -22, -116, 79, -16, -7, 2, 64, 58, -43, -88, 91, 75, 104, 89, -112, -50, 8, -23, -52, -27, 31, 124, -106, 119, -78, 44, 23, -33, 92, 20, -55, 26, 84, 44, -80, -44, -6, 45, 83, -42, -126, -82, 79, -40, 13, 24, -63, 97, 93, -16, -80, 48, -121, 123, 51, -115, 51, 9, -90, 98, -117, 78, 56, -58, 33, -25, 31, -40, -39, -20, 61, 2, 65, 0, -109, -31, -106, -105, 17, -33, -16, 2, -127, -78, 110, 11, 11, -31, -124, 1, -89, -95, 6, 85, 105, -81, 15, -122, -40, 28, 29, 48, 26, -32, -66, -98, -17, -77, 121, -66, 34, -75, 61, 92, -81, 103, 85, 6, 70, -98, -83, -7, -14, -33, -42, 40, -108, -11, -74, -49, 110, 92, -25, -15, 85, -29, -59, -20};
	//		System.out.println(createPrivKey(arr).getFormat());
	//	}

	//fusion (or add) a connected peer to our lists 
	void fusionWithConnectedPeer(PeerPtr peer);
	// add an unconnected peer to our list of possible peers.
	PeerPtr addNewPeer(ComputerId computerId, const PeerData& data);

	//request->send->receive a public key from a peer
	//void requestPublicKey(Peer& peer);
	void sendPublicKey(PeerPtr peer);
	void receivePublicKey(PeerPtr peer, const ByteBuff& buffIn);

	// mesage to verify the peer identity
	std::string createMessageForIdentityCheck(Peer& peer, bool forceNewOne);


	ByteBuff getIdentityDecodedMessage(const PublicKeyHolder& key, const ByteBuff& buffIn);

	enum class Identityresult : uint8_t { NO_PUB, BAD, OK };
	//send our public key to the peer, with the message encoded
	Identityresult sendIdentity(PeerPtr peer, const std::string& messageToEncrypt, bool isRequest);
	Identityresult answerIdentity(PeerPtr peer, const ByteBuff& buffIn);
	Identityresult receiveIdentity(PeerPtr peer, const ByteBuff& buffIn);

	//can't be const because of the mutex
	void requestSave();

	//public Cipher getSecretCipher(Peer p, int mode);

	//ByteBuff blockCipher(byte[] bytes, int mode, Cipher cipher);

	/// <summary>
	/// 
	/// </summary>
	/// <param name="message"></param>
	/// <param name="peer"></param>
	/// <param name="message_counter">a counter that is unique per message sent, so you can use it to  change the iv a bit.</param>
	/// <returns> extra message to send before the encrypted message</returns>
	void encodeMessageSecret(ByteBuff& message, PeerPtr peer, size_t message_counter);
	void decodeMessageSecret(ByteBuff& message, PeerPtr peer, size_t message_counter);

	void requestSecretKey(PeerPtr peer);

public:
	static inline const uint8_t AES_PROPOSAL = 1 << 0; // i propose this (maybe you will not accept it)
	static inline const uint8_t AES_CONFIRM = 1 << 1;
	static inline const uint8_t AES_RENEW = 1 << 2;
	static inline const uint8_t AES_CONFLICT_DETECTED = 1 << 3;
public:


	//note: the proposal/confirm thing work because i set my aes key before i emit my proposal.

	void sendAesKey(PeerPtr peer, uint8_t aesState);

	/// <summary>
	/// use the message to check if the aes key sent to us is acceptable.
	/// This can lead to another aes message, as long as we can't agree on a same aes key.
	/// </summary>
	/// <param name="peer"></param>
	/// <param name="message"></param>
	/// <returns>true if the aes key is valid and accepted by both of us (as much as possible)</returns>
	bool receiveAesKey(PeerPtr peer, const ByteBuff& message);

	EncryptionType getEncryptionType() { return m_encryption_type; }
protected:
	void setEncryption(EncryptionType type) { this->m_encryption_type = type; }

};

} // namespace supercloud
