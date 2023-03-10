#pragma once

#include "utils/ByteBuff.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <random>

#include <cryptopp/rsa.h>
#include <cryptopp/pssr.h>

namespace supercloud{
	//small key size to test quickly
	constexpr size_t RSA_KEY_SIZE = 1001;

	typedef std::vector<uint8_t> PublicKey;
	typedef std::vector<uint8_t> PrivateKey;
	typedef std::vector<uint8_t> SecretKey;

	inline std::random_device rd;     // Only used once to initialise (seed) engine
	inline std::mt19937 rng(rd());    // Random-number engine used (Mersenne-Twister in this case)
	inline std::uniform_int_distribution<uint16_t> uni_u8(0, std::numeric_limits<uint8_t>::max()); // Guaranteed unbiased
	inline uint8_t rand_u8() {
		return uint8_t(uni_u8(rng));
	}
	inline std::uniform_int_distribution<uint64_t> uni_u63(0, uint64_t(std::numeric_limits<int64_t>::max())); // Guaranteed unbiased
	inline uint64_t rand_u63() {
		return uni_u63(rng);
	}
	inline std::string to_hex_str(uint64_t data)
	{
		std::stringstream stream;
		stream << std::hex << data;
		return stream.str();
	}
	struct Peer {
		uint64_t id;
		uint64_t getPeerId() {
			return id;
		}
		Peer() :id(rand_u63()) {}
		Peer(uint64_t i) :id(i) {}
	};
	typedef std::shared_ptr<Peer> PeerPtr;


	inline std::mutex useless_mutex;

	inline std::mutex* loglock() { return &useless_mutex; }

	void putCryptoPPInteger(ByteBuff& bt, const CryptoPP::Integer& val);

	CryptoPP::Integer getCryptoPPInteger(ByteBuff& buff);

	CryptoPP::RSA::PrivateKey getCryptoppPrivateKey(const PrivateKey& key);

	void putCryptoppPrivateKey(const CryptoPP::RSA::PrivateKey& private_key, PrivateKey& saved_key);

	CryptoPP::RSA::PublicKey getCryptoppPublicKey(const PublicKey& key);

	void putCryptoppPublicKey(const CryptoPP::RSA::PublicKey& private_key, PublicKey& saved_key);

	std::vector<uint8_t> cryptopp_sign(const std::vector<uint8_t>& data, CryptoPP::RSA::PrivateKey private_key);
	std::vector<uint8_t> cryptopp_unsign(const std::vector<uint8_t>& data, CryptoPP::RSA::PublicKey public_key);
	std::vector<uint8_t> cryptopp_encrypt(const std::vector<uint8_t>& data, CryptoPP::RSA::PublicKey public_key);
	std::vector<uint8_t> cryptopp_decrypt(const std::vector<uint8_t>& data, CryptoPP::RSA::PrivateKey private_key);

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

	struct PeerData {
		// the registered peer. may be in connection or connected.
		PeerPtr peer;
		PublicKeyHolder rsa_public_key;
		SecretKey aes_key;
		std::shared_ptr<void> aes_encoder; // cached encoder, to avoid recreating it each time.
		bool operator==(const PeerData& other) const {
			return peer == other.peer && rsa_public_key == other.rsa_public_key && aes_key == other.aes_key;
		}
	};

	PeerPtr m_myself;

	mutable std::mutex m_peer_data_mutex;
	std::unordered_map<PeerPtr, PeerData> m_peer_2_peerdata;


	PrivateKey m_private_key;
	PublicKeyHolder m_public_key;

	EncryptionType m_encryption_type = EncryptionType::UNKNOWN; // the encryption used by the cluster (should be aes), can be none for testing purpose

	IdentityManager(uint64_t id) {
		m_myself = std::make_shared<Peer>(id);
		m_peer_2_peerdata[m_myself].peer = m_myself;
	}

	void create_from_install_info();
	EncryptionType getDefaultAESEncryptionType();
	EncryptionType getDefaultRSAEncryptionType();
	void encrypt(std::vector<uint8_t>& data, const PublicKeyHolder& peer_pub_key);
	bool decrypt(std::vector<uint8_t>& data, const PublicKeyHolder& peer_pub_key); // return false if the message is corrupted or wrong pub/priv key

	/// <summary>
	/// 
	/// </summary>
	/// <param name="message"></param>
	/// <param name="peer"></param>
	/// <returns> extra message to send before the encrypted message</returns>
	void encodeMessageSecret(ByteBuff& message, PeerPtr peer, size_t message_ct);
	void decodeMessageSecret(ByteBuff& message, PeerPtr peer, size_t message_ct);

	void createNewPublicKey(EncryptionType type);
	SecretKey createNewSecretKey(EncryptionType type);


	inline EncryptionType getEncryptionType() { return m_encryption_type; }

	inline void setEncryption(EncryptionType type) { this->m_encryption_type = type; }

	inline void requestSave() {}
};

} // namespace supercloud
