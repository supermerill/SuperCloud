#pragma once

#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include "Peer.hpp"
#include "ConnectionMessageManager.hpp"
#include "ConnectionMessageManagerInterface.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace supercloud{
	//TODO crypt
#define PublicKey std::string
#define PrivateKey std::string
#define SecretKey std::vector<uint8_t>

class PhysicalServer;

	/**
	 *
	 * This class if the manager of the peers we kind of trust in the network.
	 * It store the list of them, and their keys, and some method to identify them.
	 */
class ServerIdDb {
	protected:
		std::mutex db_file_mutex;

		PrivateKey privateKey;
		PublicKey publicKey;

		std::vector<PeerPtr> receivedServerList;
		std::mutex registeredPeers_mutex;
		std::vector<PeerPtr> registeredPeers;
		std::vector<PeerPtr> loadedPeers;
		std::mutex tempPubKey_mutex;
		std::unordered_map<uint64_t, PublicKey> tempPubKey; // unidentified pub key
		std::mutex id2PublicKey_mutex;
		std::unordered_map<uint16_t, PublicKey> id2PublicKey; // identified pub key
		std::mutex id2AesKey_mutex;
		std::unordered_map<uint16_t, SecretKey> id2AesKey;
		PhysicalServer& serv;

		uint16_t myComputerId = NO_COMPUTER_ID;
		int64_t timeChooseId = 0;

		std::unordered_map<uint64_t, std::string> peerId2emittedMsg;
		std::mutex save_mutex;
		std::filesystem::path filepath;
		uint64_t clusterId = NO_CLUSTER_ID; // the id to identify the whole cluster
		std::string passphrase = "no protection"; //the passphrase of the cluster


	public:
		ServerIdDb(PhysicalServer& serv, const std::filesystem::path& filePath) : serv(serv), filepath(filePath) {}

		std::mutex& synchronize() { return db_file_mutex;  }

		//getters
		std::vector<PeerPtr> getRegisteredPeers() { return registeredPeers; }
		uint64_t getClusterId() { return clusterId; }
		uint16_t getComputerId() { return myComputerId; }
		void setComputerId(uint16_t myNewComputerId) { myComputerId = myNewComputerId; }
		//TODO is it safe?
		std::vector<PeerPtr>& getReceivedServerList() { return receivedServerList; }
		std::vector<PeerPtr>& getLoadedPeers() { return loadedPeers; }

		void addToReceivedServerList(PeerPtr peer) { receivedServerList.push_back(std::move(peer)); }

		uint64_t getTimeChooseId(){ return timeChooseId; }
		PublicKey getPublicKey() { return publicKey; }
		PublicKey getPublicKey(uint16_t key) {
			{ std::lock_guard lock(this->id2PublicKey_mutex);
				auto it = id2PublicKey.find(key);
				if (it != id2PublicKey.end())
					return it->second;
				return PublicKey{};
			}
		}

		//setters
		void removeBadPeer(Peer* badPeer, uint16_t computerId) {
			//loadedPeers.remove(badPeer); :
			for (auto it = loadedPeers.begin(); it != loadedPeers.end(); ++it) {
				if (it->get() == badPeer) {
					it->get()->close();
					loadedPeers.erase(it);
					break;
				}
			}
			//also erase the bad computer id
			id2PublicKey.erase(this->getComputerId());
		}

		// create our keys
		void createNewPublicKey();
		//static PrivateKey createPrivKey(const std::vector<uint8_t>& datas);

		void newClusterId() { clusterId = rand_u63(); }

		void setPassword(const std::string& pass) {
			passphrase = pass;
			requestSave();
		}

		//save/load the db into/from a file (both thread-unsafe)
		void load();
		void save(const std::filesystem::path& filePath) const;

		//	public static void main(std::string[] args) {
		////		byte[] arr = new byte[]{48, -126, 2, 118, 2, 1, 0, 48, 13, 6, 9, 42, -122, 72, -122, -9, 13, 1, 1, 1, 5, 0, 4, -126, 2, 96, 48, -126, 2, 92, 2, 1, 0, 2, -127, -127, 0, -127, -83, -115, 114, -35, 8, 54, -43, -68, -74, -122, -79, 118, 42, 102, -112, 20, -41, -26, -27, -59, -101, -48, -37, 55, 46, 56, 52, 18, 125, 34, -85, -51, 59, 13, -90, 61, 86, -117, -39, -98, 53, 28, 69, 106, 88, -51, 97, -128, 38, 75, -42, -99, -50, 97, -69, -61, 29, -114, -93, 100, 70, 74, -103, 31, -26, -125, 67, -22, 55, 117, -114, 12, 18, -108, -24, 36, -18, -27, 58, 112, -52, -127, -2, -75, -23, -122, 7, 2, 9, -126, -20, -66, 23, 98, 71, 37, -125, -77, 8, 56, -70, 58, 20, 91, -90, -4, 64, 100, 3, -32, 11, -97, -125, 30, -3, -110, -117, 111, 107, 13, -126, -50, 106, -59, -8, -112, -65, 2, 3, 1, 0, 1, 2, -127, -128, 34, 81, 10, 48, -114, 91, -127, 31, 88, -68, 56, -78, -73, -95, -118, -40, -80, 27, 94, 104, 9, -66, 45, 44, 5, -45, 62, 94, 81, 82, 58, 29, -102, -58, -8, -38, -72, 58, -79, -15, -103, -45, 86, 50, -20, 108, -87, -107, 22, -77, -117, -72, 52, -76, -117, -38, -125, 76, -52, 21, 99, 16, -46, -26, -121, -40, 30, -124, -5, -36, 1, -116, 105, 44, -6, 61, 21, -125, -22, -11, -112, -65, -34, -89, 46, -128, -25, -32, 38, -30, 40, -121, 52, -76, -95, -2, 2, 38, -57, 34, -110, -66, -83, 91, -6, 95, 16, 16, -37, 116, 12, 52, 119, -106, 33, -1, -111, -123, -53, 44, -94, 107, -99, -96, -70, 99, 63, -55, 2, 65, 0, -57, -17, 16, -94, -49, 59, 34, -36, 36, -97, -79, -79, -98, 39, -48, -67, -108, -121, -67, 44, 109, 75, 7, -17, 95, -25, 102, 95, -83, 63, -7, 13, -126, -41, 14, -1, -105, -102, -21, 67, -27, 88, -60, -11, -24, -1, 89, 0, -71, 54, 30, 31, -27, -15, 5, 58, 75, 101, 86, 68, 53, -107, 87, -51, 2, 65, 0, -90, 10, -19, 74, -84, -106, -103, 85, -67, -112, -21, -119, -117, -60, 80, 14, 85, -104, -3, 56, -15, -80, -73, 127, 58, -59, -84, 13, 85, -35, -61, 65, -4, -16, -25, -7, 48, -17, 92, -45, -36, 115, 119, 94, -97, 35, -90, 19, -51, -73, 2, 28, 57, 65, 97, -124, -7, -54, 55, 114, -73, 61, 38, -69, 2, 64, 9, -104, -10, 73, 122, 125, 50, 61, 51, 28, -33, 96, -47, 96, -61, -22, 117, -40, -42, 65, -19, -75, 46, 90, 85, 86, 60, 89, -41, 109, 60, -67, 99, 76, -125, -111, -51, 107, 72, 99, -25, -4, -116, -25, -23, 25, 104, -30, 90, 1, -71, 12, 122, -13, 72, -10, -11, 107, -107, -22, -116, 79, -16, -7, 2, 64, 58, -43, -88, 91, 75, 104, 89, -112, -50, 8, -23, -52, -27, 31, 124, -106, 119, -78, 44, 23, -33, 92, 20, -55, 26, 84, 44, -80, -44, -6, 45, 83, -42, -126, -82, 79, -40, 13, 24, -63, 97, 93, -16, -80, 48, -121, 123, 51, -115, 51, 9, -90, 98, -117, 78, 56, -58, 33, -25, 31, -40, -39, -20, 61, 2, 65, 0, -109, -31, -106, -105, 17, -33, -16, 2, -127, -78, 110, 11, 11, -31, -124, 1, -89, -95, 6, 85, 105, -81, 15, -122, -40, 28, 29, 48, 26, -32, -66, -98, -17, -77, 121, -66, 34, -75, 61, 92, -81, 103, 85, 6, 70, -98, -83, -7, -14, -33, -42, 40, -108, -11, -74, -49, 110, 92, -25, -15, 85, -29, -59, -20};
		//		byte[] arr = new byte[]{48, -126, 2, 118, 2, 1, 0, 48, 13, 6, 9, 42, -122, 72, -122, -9, 13, 1, 1, 1, 5, 0, 4, -126, 2, 96, 48, -126, 2, 92, 2, 1, 0, 2, -127, -127, 0, -127, -83, -115, 114, -35, 8, 54, -43, -68, -74, -122, -79, 118, 42, 102, -112, 20, -41, -26, -27, -59, -101, -48, -37, 55, 46, 56, 52, 18, 125, 34, -85, -51, 59, 13, -90, 61, 86, -117, -39, -98, 53, 28, 69, 106, 88, -51, 97, -128, 38, 75, -42, -99, -50, 97, -69, -61, 29, -114, -93, 100, 70, 74, -103, 31, -26, -125, 67, -22, 55, 117, -114, 12, 18, -108, -24, 36, -18, -27, 58, 112, -52, -127, -2, -75, -23, -122, 7, 2, 9, -126, -20, -66, 23, 98, 71, 37, -125, -77, 8, 56, -70, 58, 20, 91, -90, -4, 64, 100, 3, -32, 11, -97, -125, 30, -3, -110, -117, 111, 107, 13, -126, -50, 106, -59, -8, -112, -65, 2, 3, 1, 0, 1, 2, -127, -128, 34, 81, 10, 48, -114, 91, -127, 31, 88, -68, 56, -78, -73, -95, -118, -40, -80, 27, 94, 104, 9, -66, 45, 44, 5, -45, 62, 94, 81, 82, 58, 29, -102, -58, -8, -38, -72, 58, -79, -15, -103, -45, 86, 50, -20, 108, -87, -107, 22, -77, -117, -72, 52, -76, -117, -38, -125, 76, -52, 21, 99, 16, -46, -26, -121, -40, 30, -124, -5, -36, 1, -116, 105, 44, -6, 61, 21, -125, -22, -11, -112, -65, -34, -89, 46, -128, -25, -32, 38, -30, 40, -121, 52, -76, -95, -2, 2, 38, -57, 34, -110, -66, -83, 91, -6, 95, 16, 16, -37, 116, 12, 52, 119, -106, 33, -1, -111, -123, -53, 44, -94, 107, -99, -96, -70, 99, 63, -55, 2, 65, 0, -57, -17, 16, -94, -49, 59, 34, -36, 36, -97, -79, -79, -98, 39, -48, -67, -108, -121, -67, 44, 109, 75, 7, -17, 95, -25, 102, 95, -83, 63, -7, 13, -126, -41, 14, -1, -105, -102, -21, 67, -27, 88, -60, -11, -24, -1, 89, 0, -71, 54, 30, 31, -27, -15, 5, 58, 75, 101, 86, 68, 53, -107, 87, -51, 2, 65, 0, -90, 10, -19, 74, -84, -106, -103, 85, -67, -112, -21, -119, -117, -60, 80, 14, 85, -104, -3, 56, -15, -80, -73, 127, 58, -59, -84, 13, 85, -35, -61, 65, -4, -16, -25, -7, 48, -17, 92, -45, -36, 115, 119, 94, -97, 35, -90, 19, -51, -73, 2, 28, 57, 65, 97, -124, -7, -54, 55, 114, -73, 61, 38, -69, 2, 64, 9, -104, -10, 73, 122, 125, 50, 61, 51, 28, -33, 96, -47, 96, -61, -22, 117, -40, -42, 65, -19, -75, 46, 90, 85, 86, 60, 89, -41, 109, 60, -67, 99, 76, -125, -111, -51, 107, 72, 99, -25, -4, -116, -25, -23, 25, 104, -30, 90, 1, -71, 12, 122, -13, 72, -10, -11, 107, -107, -22, -116, 79, -16, -7, 2, 64, 58, -43, -88, 91, 75, 104, 89, -112, -50, 8, -23, -52, -27, 31, 124, -106, 119, -78, 44, 23, -33, 92, 20, -55, 26, 84, 44, -80, -44, -6, 45, 83, -42, -126, -82, 79, -40, 13, 24, -63, 97, 93, -16, -80, 48, -121, 123, 51, -115, 51, 9, -90, 98, -117, 78, 56, -58, 33, -25, 31, -40, -39, -20, 61, 2, 65, 0, -109, -31, -106, -105, 17, -33, -16, 2, -127, -78, 110, 11, 11, -31, -124, 1, -89, -95, 6, 85, 105, -81, 15, -122, -40, 28, 29, 48, 26, -32, -66, -98, -17, -77, 121, -66, 34, -75, 61, 92, -81, 103, 85, 6, 70, -98, -83, -7, -14, -33, -42, 40, -108, -11, -74, -49, 110, 92, -25, -15, 85, -29, -59, -20};
		//		System.out.println(createPrivKey(arr).getFormat());
		//	}



		//request->send->receive a public key from a peer
		void requestPublicKey(Peer& peer);
		void sendPublicKey(Peer& peer);
		void receivePublicKey(Peer& peer, ByteBuff& buffIn);

		// mesage to verify the peer identity
		std::string createMessageForIdentityCheck(Peer& peer, bool forceNewOne);

		//send our public key to the peer, with the message encoded
		void sendIdentity(Peer& peer, const std::string& messageToEncrypt, bool isRequest);

		ByteBuff getIdentityDecodedMessage(const PublicKey& key, ByteBuff& buffIn);

		void answerIdentity(Peer& peer, ByteBuff& buffIn);

		void receiveIdentity(PeerPtr peer, ByteBuff& buffIn);

		//can't be const because of the mutex
		void requestSave();

		//public Cipher getSecretCipher(Peer p, int mode);

		//ByteBuff blockCipher(byte[] bytes, int mode, Cipher cipher);


		bool isChoosen(uint16_t choosenId) {
			if (this->id2PublicKey.find(choosenId) != id2PublicKey.end()) {
				return true;
			}
			return false;
		}

		void requestSecretKey(Peer& peer);

	public:
		static inline const uint8_t AES_PROPOSAL = 0; // i propose this (maybe you will not accept it)
		static inline const uint8_t AES_CONFIRM = 1; // i accepted this i will not change it, I promise.
		static inline const uint8_t AES_PROPOSAL_RENEW = 2; //TODO i want to change, do you accept?
		static inline const uint8_t AES_CONFIRM_RENEW = 3; //TODO i accepted this change and now i will not change it, I promise.
		static inline const uint8_t AES_FLAG_CONFIRM = 1;
		static inline const uint8_t AES_FLAG_RENEW = 2; // 1<<1 si je me souvient bien du sens
	public:


		//note: the proposal/confirm thing work because i set my aes key before i emit my proposal.

		void sendAesKey(Peer& peer, uint8_t aesState);

		void receiveAesKey(Peer& peer, ByteBuff& message);


		/**
		 * Add a computer id to the list of known computerid. This is maintained up-to-date to let other know what id is not available.
		 * But these are not saved as they are not registered (no verified public keys). => it's half-assed we should just trash them if they are not verified ?
		 * @param computerId some computerId, maybe already stored or just plain wrong.
		 */
		void addPeer(uint16_t computerId);
};

} // namespace supercloud
