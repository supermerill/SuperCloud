
#include "IdentityManager.hpp"

//#define NO_CRYPTOPP 1

#ifndef NO_CRYPTOPP
//#include "cryptopp/dll.h"
#include <cryptopp/eax.h>
#include <cryptopp/osrng.h>
#include <cryptopp/pssr.h>
#include <cryptopp/rsa.h>
#endif

#include <cassert>
#include <chrono>
#include <filesystem>

namespace supercloud{

#ifndef NO_CRYPTOPP
	constexpr IdentityManager::EncryptionType DefaultAESEncryptionType = IdentityManager::EncryptionType::AES;
	constexpr IdentityManager::EncryptionType DefaultRSAEncryptionType = IdentityManager::EncryptionType::RSA;

	CryptoPP::AutoSeededRandomPool CRYPTOPP_RANDOM_GENERATOR;

	void putCryptoPPInteger(ByteBuff& bt, const CryptoPP::Integer& val) {
		bt.putSize(val.ByteCount());
		bt.expand(val.ByteCount());
		val.Encode(bt.raw_array() + bt.position(), val.ByteCount(), CryptoPP::Integer::Signedness::UNSIGNED);
		bt.position(bt.position() + val.ByteCount());
	}

	CryptoPP::Integer getCryptoPPInteger(ByteBuff& buff) {
		size_t size = buff.getSize();
		buff.position(buff.position() + size);
		return CryptoPP::Integer(&buff.raw_array()[buff.position() - size], size, CryptoPP::Integer::Signedness::UNSIGNED);
	}

	CryptoPP::RSA::PrivateKey getCryptoppPrivateKey(const PrivateKey& key) {
		ByteBuff buff;
		buff.put(key).flip();
		CryptoPP::RSA::PrivateKey priv;
		CryptoPP::Integer n = getCryptoPPInteger(buff);
		CryptoPP::Integer e = getCryptoPPInteger(buff);
		CryptoPP::Integer d = getCryptoPPInteger(buff);
		CryptoPP::Integer p = getCryptoPPInteger(buff);
		CryptoPP::Integer q = getCryptoPPInteger(buff);
		CryptoPP::Integer dp = getCryptoPPInteger(buff);
		CryptoPP::Integer dq = getCryptoPPInteger(buff);
		CryptoPP::Integer u = getCryptoPPInteger(buff);
		priv.Initialize(n, e, d, p, q, dp, dq, u);/// note: don't put getCryptoPPInteger directly here, as the order can be random / not what you expect
		return priv;
	}

	void putCryptoppPrivateKey(const CryptoPP::RSA::PrivateKey& private_key, PrivateKey& saved_key) {
		ByteBuff save_private_key;
		putCryptoPPInteger(save_private_key, private_key.GetModulus()); //n
		putCryptoPPInteger(save_private_key, private_key.GetPublicExponent()); //e
		putCryptoPPInteger(save_private_key, private_key.GetPrivateExponent()); //d
		putCryptoPPInteger(save_private_key, private_key.GetPrime1()); //p
		putCryptoPPInteger(save_private_key, private_key.GetPrime2()); //q
		putCryptoPPInteger(save_private_key, private_key.GetModPrime1PrivateExponent()); //dp
		putCryptoPPInteger(save_private_key, private_key.GetModPrime2PrivateExponent()); //dq
		putCryptoPPInteger(save_private_key, private_key.GetMultiplicativeInverseOfPrime2ModPrime1()); //u
		saved_key = save_private_key.flip().getAll();
	}

	CryptoPP::RSA::PublicKey getCryptoppPublicKey(const PublicKey& key) {
		ByteBuff buff;
		buff.put(key).flip();
		CryptoPP::RSA::PublicKey pub;
		CryptoPP::Integer n = getCryptoPPInteger(buff); //n
		CryptoPP::Integer e = getCryptoPPInteger(buff); //e
		pub.Initialize(n, e); /// note: don't put getCryptoPPInteger directly here, as the order can be random / not what you expect
		return pub;
	}

	void putCryptoppPublicKey(const CryptoPP::RSA::PublicKey& pub_key, PublicKey& saved_key) {
		ByteBuff save_pub_key;
		putCryptoPPInteger(save_pub_key, pub_key.GetModulus()); //n
		putCryptoPPInteger(save_pub_key, pub_key.GetPublicExponent()); //e
		saved_key = save_pub_key.flip().getAll();
	}
	struct CryptoppAES {
		CryptoPP::EAX<CryptoPP::AES>::Encryption encoder;
		CryptoPP::EAX<CryptoPP::AES>::Decryption decoder;
	};
	struct CryptoppSecretKey {
		CryptoPP::SecByteBlock key;
		CryptoPP::SecByteBlock iv;
		CryptoppSecretKey() : key(CryptoPP::AES::DEFAULT_KEYLENGTH), iv(CryptoPP::AES::BLOCKSIZE){};
		CryptoppSecretKey(const SecretKey& n_key, const SecretKey& n_iv) : key(n_key.data(), n_key.size()), iv(n_iv.data(), n_iv.size()) {}
	};
	CryptoppSecretKey  getCryptoppSecretKey(const SecretKey& sec_key) {
		ByteBuff buff;
		buff.put(sec_key).flip();
		SecretKey key = buff.get(buff.getSize());
		SecretKey iv = buff.get(buff.getSize());
		return CryptoppSecretKey{ key , iv };
	}

	void putCryptoppSecretKey(const CryptoppSecretKey& sec_key, SecretKey& saved_key) {
		ByteBuff save_pub_key;
		save_pub_key.putSize(sec_key.key.size()).put(sec_key.key.data(), sec_key.key.size());
		save_pub_key.putSize(sec_key.iv.size()).put(sec_key.iv.data(), sec_key.iv.size());
		saved_key = save_pub_key.flip().getAll();
	}
	typedef std::vector<uint8_t> VecByte;
	VecByte cryptopp_sign(const VecByte& data, CryptoPP::RSA::PrivateKey private_key) {
		// signer
		CryptoPP::RSASS<CryptoPP::PSSR, CryptoPP::SHA1>::Signer signer{ private_key };
		//CryptoPP::RSASS<CryptoPP::PSSR, CryptoPP::SHA256>::Signer signer{ private_key };
		// transform buffer into cryptopp type
		CryptoPP::SecByteBlock signature(signer.MaxSignatureLength(data.size()));
		//sign
		size_t signature_len = signer.SignMessageWithRecovery(CRYPTOPP_RANDOM_GENERATOR, &data[0], data.size(), NULL, 0, signature);
		//transform output into vector<byte>
		VecByte v;
		v.resize(signature_len);
		std::copy(signature.BytePtr(), signature.BytePtr() + signature_len, v.begin());
		return v;
	}

	VecByte cryptopp_unsign(const VecByte& data, CryptoPP::RSA::PublicKey public_key) {
		// verifier
		CryptoPP::RSASS<CryptoPP::PSSR, CryptoPP::SHA1>::Verifier verifier(public_key);
		//CryptoPP::RSASS<CryptoPP::PSSR, CryptoPP::SHA256>::Verifier verifier(public_key);
		// transform buffer into cryptopp type
		CryptoPP::SecByteBlock signature(&data[0], data.size());
		// decode & verify
		CryptoPP::SecByteBlock recovered(verifier.MaxRecoverableLengthFromSignatureLength(data.size()));
		CryptoPP::DecodingResult result = verifier.RecoverMessage(recovered, NULL, 0, signature, data.size());
		if (!result.isValidCoding) {
			return VecByte{};
		}
		//transform output into vector<byte>
		size_t recovered_len = result.messageLength;
		VecByte v;
		v.resize(recovered_len);
		std::copy(recovered.BytePtr(), recovered.BytePtr() + recovered_len, v.begin());
		return v;
	}

	VecByte cryptopp_encrypt(const VecByte& data, CryptoPP::RSA::PublicKey public_key) {
		//encryptor
		CryptoPP::RSAES_OAEP_SHA_Encryptor encryptor{ public_key };
		//test that the data is not too big
		assert(0 != encryptor.FixedMaxPlaintextLength());
		assert(data.size() <= encryptor.FixedMaxPlaintextLength());
		//create output buffer
		VecByte data_out;
		data_out.resize(encryptor.CiphertextLength(data.size()));
		encryptor.Encrypt(CRYPTOPP_RANDOM_GENERATOR, &data[0], data.size(), &data_out[0]);
		//finished
		return data_out;
	}
	VecByte cryptopp_decrypt(const VecByte& data, CryptoPP::RSA::PrivateKey private_key) {
		//decryptor
		CryptoPP::RSAES_OAEP_SHA_Decryptor decryptor{ private_key };
		//create output buffer
		VecByte data_out;
		data_out.resize(decryptor.MaxPlaintextLength(data.size()));
		CryptoPP::DecodingResult result = decryptor.Decrypt(CRYPTOPP_RANDOM_GENERATOR, &data[0], data.size(), &data_out[0]);
		//checks
		assert(result.isValidCoding);
		assert(result.messageLength <= data_out.size());
		//format output
		data_out.resize(result.messageLength);
		return data_out;
	}
#else
	constexpr IdentityManager::EncryptionType DefaultAESEncryptionType = IdentityManager::EncryptionType::NAIVE;
	constexpr IdentityManager::EncryptionType DefaultRSAEncryptionType = IdentityManager::EncryptionType::NAIVE;
#endif

	IdentityManager::EncryptionType IdentityManager::getDefaultAESEncryptionType() { return DefaultAESEncryptionType; }
	IdentityManager::EncryptionType IdentityManager::getDefaultRSAEncryptionType() { return DefaultRSAEncryptionType; }

	void IdentityManager::createNewPublicKey(EncryptionType type) {
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
		if (type == EncryptionType::RSA) {
			//TODO
#ifndef NO_CRYPTOPP
			///////////////////////////////////////
			// Generate Parameters
			CryptoPP::InvertibleRSAFunction params;
			params.GenerateRandomWithKeySize(CRYPTOPP_RANDOM_GENERATOR, RSA_KEY_SIZE);

			///////////////////////////////////////
			// Create Keys
			CryptoPP::RSA::PrivateKey private_key = CryptoPP::RSA::PrivateKey(params);
			CryptoPP::RSA::PublicKey public_key(params);

			//validation
			if(!private_key.Validate(CRYPTOPP_RANDOM_GENERATOR, 3))
				throw std::runtime_error("Rsa private key validation failed");

			if (!public_key.Validate(CRYPTOPP_RANDOM_GENERATOR, 3))
				throw std::runtime_error("Rsa public key validation failed");

			//save
			putCryptoppPrivateKey(private_key, this->m_private_key);
			putCryptoppPublicKey(public_key, this->m_public_key.raw_data);
			this->m_public_key.type = EncryptionType::RSA;
#else
			throw new std::exception("error, no cryptopp for rsa creation");
#endif
		} else {
			this->m_public_key.type = type;
			this->m_public_key.raw_data.clear();
			for (int i = 0; i < 16; i++) {
				this->m_public_key.raw_data.push_back(rand_u8());
			}
			this->m_private_key = this->m_public_key.raw_data;
		}
		// this->publicKey is just a cache for m_peer_2_peerdata[getComputerId()].rsa_public_key
		{std::lock_guard lock(m_peer_data_mutex);
			assert(m_peer_2_peerdata.find(m_myself) != m_peer_2_peerdata.end());
			m_peer_2_peerdata[this->m_myself].rsa_public_key = this->m_public_key;
		}
		requestSave();
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

	void compute_naive_xor(uint8_t* data, const size_t size, const std::vector<uint8_t>& key) {
		for (size_t i = 0; i < size; ++i) {
			data[i] ^= key[i % key.size()];
		}
	}

	void IdentityManager::encrypt(std::vector<uint8_t>& data, const PublicKeyHolder& peer_pub_key) {
#ifndef NO_CRYPTOPP
		VecByte temp_buffer;
#endif
		//encrypt with our private key (for signing purpose)
		if (m_public_key.type == EncryptionType::NAIVE) {
			//public==private, it's a xor
			compute_naive_xor(&data[0], data.size(), m_private_key);
		} else if (m_public_key.type == EncryptionType::RSA) {
#ifndef NO_CRYPTOPP
			// get private key
			CryptoPP::RSA::PrivateKey private_key = getCryptoppPrivateKey(m_private_key);
			// sign
			temp_buffer = cryptopp_sign(data, private_key);
#else
			throw new std::exception("error, no cryptopp for rsa creation");
#endif
		} else {
			//no encryption
		}

		//encrypt now with the public key of the peer
		//cipher.init(Cipher.ENCRYPT_MODE, theirPubKey);
		//buffEncoded = blockCipher(buffEncoded.array(), Cipher.ENCRYPT_MODE, cipher);
		if (peer_pub_key.type == EncryptionType::NAIVE) {
			//public==private, it's a xor
			compute_naive_xor(&data[0], data.size(), peer_pub_key.raw_data);
		} else if (peer_pub_key.type == EncryptionType::RSA) {
#ifndef NO_CRYPTOPP
			// get public key
			CryptoPP::RSA::PublicKey public_key = getCryptoppPublicKey(peer_pub_key.raw_data);
			//crypt in two part
			VecByte data_crypt_1 = cryptopp_encrypt(VecByte{ temp_buffer.begin(), temp_buffer.begin() + temp_buffer.size() / 2 }, public_key);
			VecByte data_crypt_2 = cryptopp_encrypt(VecByte{ temp_buffer.begin() + temp_buffer.size() / 2, temp_buffer.end() }, public_key);
			//concatenate with size
			ByteBuff buff;
			buff.putSize(data_crypt_1.size()).put(data_crypt_1);
			buff.putSize(data_crypt_2.size()).put(data_crypt_2);
			data = buff.flip().getAll();
#else
			throw new std::exception("error, no cryptopp for rsa creation");
#endif
		} else {
			//no encryption
		}
	}


	bool IdentityManager::decrypt(std::vector<uint8_t>& data, const PublicKeyHolder& peer_pub_key) {

#ifndef NO_CRYPTOPP
		VecByte temp_buffer;
#endif

		//decrypt the message encoded with our key
		if (this->m_public_key.type == EncryptionType::NAIVE) {
			//public==private, it's a xor
			compute_naive_xor(&data[0], data.size(), this->m_private_key);
		} else if (this->m_public_key.type == EncryptionType::RSA) {
#ifndef NO_CRYPTOPP
			// get private key
			CryptoPP::RSA::PrivateKey private_key = getCryptoppPrivateKey(this->m_private_key);
			//get the two parts from the buffer
			ByteBuff buff;
			buff.put(data).flip();
			VecByte part1 = buff.get(buff.getSize());
			VecByte part2 = buff.get(buff.getSize());
			assert(buff.available() == 0);

			temp_buffer = cryptopp_decrypt(part1, private_key);
			VecByte decoded_p2 = cryptopp_decrypt(part2, private_key);
			if (temp_buffer.empty() || decoded_p2.empty()) {
				assert(false);
				return false;
			}
			temp_buffer.insert(temp_buffer.end(), decoded_p2.begin(), decoded_p2.end());
#else
			throw new std::exception("error, no cryptopp for rsa creation");
#endif
		} else {
			//no encryption
		}

		// decrypt the message encoded with the peer's key (to check the signing)
		if (peer_pub_key.type == EncryptionType::NAIVE) {
			//public==private, it's a xor
			compute_naive_xor(&data[0], data.size(), peer_pub_key.raw_data);
		} else if (peer_pub_key.type == EncryptionType::RSA) {
#ifndef NO_CRYPTOPP
			// get public key
			CryptoPP::RSA::PublicKey public_key = getCryptoppPublicKey(peer_pub_key.raw_data);
			//verify
			data = cryptopp_unsign(temp_buffer, public_key);
			if (data.empty()) {
				assert(false);
				return false;
			}
#else
			throw new std::exception("error, no cryptopp for rsa creation");
#endif
		} else {
			//no encryption
		}

		return true;
	}

	//note: currently, iv has the same ttl as the key. 
	// the iv should change at least a bit for every message.
	// so it sill be added to a counter from the peer
	SecretKey IdentityManager::createNewSecretKey(EncryptionType type) {
		SecretKey secret_key;
		if (m_encryption_type == EncryptionType::AES) {
#ifndef NO_CRYPTOPP
			CryptoppSecretKey crypto_secret_key;
			CRYPTOPP_RANDOM_GENERATOR.GenerateBlock(crypto_secret_key.key, crypto_secret_key.key.size());
			CRYPTOPP_RANDOM_GENERATOR.GenerateBlock(crypto_secret_key.iv, crypto_secret_key.iv.size());
			putCryptoppSecretKey(crypto_secret_key, secret_key);
#else
			throw new std::exception("error, no cryptopp for aes creation");
#endif
		} else {
			std::string secretStr = to_hex_str(rand_u63());
			for (int i = 0; i < secretStr.size(); i++) {
				secret_key.push_back(uint8_t(secretStr[i]));
			}
			//						byte[] aesKey = new byte[128 / 8];	// aes-128 (can be 192/256)
			//						SecureRandom prng = new SecureRandom();
			//						prng.nextBytes(aesKey);
		}
		return secret_key;
	}

	void IdentityManager::encodeMessageSecret(ByteBuff& message, PeerPtr peer, size_t message_counter) {
		SecretKey our_secret_key;
#ifndef NO_CRYPTOPP
		std::shared_ptr<CryptoppAES> our_encoder;
#endif
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = this->m_peer_2_peerdata.find(peer); it != this->m_peer_2_peerdata.end()) {
				our_secret_key = it->second.aes_key;
#ifndef NO_CRYPTOPP
				our_encoder = std::static_pointer_cast<CryptoppAES>(it->second.aes_encoder);
#endif
			}
		}
		if (our_secret_key.empty()) {
			throw std::exception("Error, no secret key to use"); //TODO my exception
		}
		if (m_encryption_type == EncryptionType::NO_ENCRYPTION) {
			//easy
		} else if (m_encryption_type == EncryptionType::NAIVE) {
			compute_naive_xor(message.raw_array(), message.limit(), our_secret_key);
		} else if (m_encryption_type == EncryptionType::AES) {
#ifndef NO_CRYPTOPP
			CryptoppSecretKey crypto_secret_key = getCryptoppSecretKey(our_secret_key);
			if (!our_encoder) {
				our_encoder = std::make_shared<CryptoppAES>();
				our_encoder->encoder.SetKeyWithIV(crypto_secret_key.key, crypto_secret_key.key.size(), crypto_secret_key.iv, crypto_secret_key.iv.size());
				our_encoder->decoder.SetKeyWithIV(crypto_secret_key.key, crypto_secret_key.key.size(), crypto_secret_key.iv, crypto_secret_key.iv.size());
				//update cache storage
				{ std::lock_guard lock(m_peer_data_mutex);
				if (auto it = this->m_peer_2_peerdata.find(peer); it != this->m_peer_2_peerdata.end()) {
					it->second.aes_encoder = our_encoder;
				}
				}
			}
			//randomize the iv a bit (should be 16bytes, so 2*8) (crypto_secret_key is a copy, we can modify it)
			for (size_t i = 0; i < crypto_secret_key.iv.size() - 7; i += 8) {
				((uint64_t*)crypto_secret_key.iv.data())[i] ^= (message_counter + peer->getPeerId());
			}
			our_encoder->encoder.SetKeyWithIV(crypto_secret_key.key, crypto_secret_key.key.size(), crypto_secret_key.iv, crypto_secret_key.iv.size());
			our_encoder->encoder.ProcessString(message.raw_array(), message.limit());
#else
			throw new std::exception("error, no cryptopp for aes");
#endif
		} else {
			throw std::exception("Error, no secret protocol to use"); //TODO my exception
		}
	}

	void IdentityManager::decodeMessageSecret(ByteBuff& message, PeerPtr peer, size_t message_counter) {
		SecretKey our_secret_key;
#ifndef NO_CRYPTOPP
		std::shared_ptr<CryptoppAES> our_encoder;
#endif
		{ std::lock_guard lock(m_peer_data_mutex);
			if (auto it = this->m_peer_2_peerdata.find(peer); it != this->m_peer_2_peerdata.end()) {
				our_secret_key = it->second.aes_key;
#ifndef NO_CRYPTOPP
				our_encoder = std::static_pointer_cast<CryptoppAES>(it->second.aes_encoder);
#endif
			}
		}
		if (our_secret_key.empty()) {
			throw std::exception("Error, no secret key to use"); //TODO my exception
		}
		if (m_encryption_type == EncryptionType::NO_ENCRYPTION) {
			//easy
		} else if (m_encryption_type == EncryptionType::NAIVE) {
			compute_naive_xor(message.raw_array(), message.limit(), our_secret_key);
		} else if (m_encryption_type == EncryptionType::AES) {
#ifndef NO_CRYPTOPP
			CryptoppSecretKey crypto_secret_key = getCryptoppSecretKey(our_secret_key);
			if (!our_encoder) {
				our_encoder = std::make_shared<CryptoppAES>();
				our_encoder->encoder.SetKeyWithIV(crypto_secret_key.key, crypto_secret_key.key.size(), crypto_secret_key.iv, crypto_secret_key.iv.size());
				our_encoder->decoder.SetKeyWithIV(crypto_secret_key.key, crypto_secret_key.key.size(), crypto_secret_key.iv, crypto_secret_key.iv.size());
				//update cache storage
				{ std::lock_guard lock(m_peer_data_mutex);
					if (auto it = this->m_peer_2_peerdata.find(peer); it != this->m_peer_2_peerdata.end()) {
						it->second.aes_encoder = our_encoder;
					}
				}
			}
			//get the special iv for this exact message (crypto_secret_key is a copy, we can modify it)
			for (size_t i = 0; i < crypto_secret_key.iv.size() - 7; i += 8) {
				((uint64_t*)crypto_secret_key.iv.data())[i] ^= (message_counter + m_myself->getPeerId());
			}
			our_encoder->decoder.SetKeyWithIV(crypto_secret_key.key, crypto_secret_key.key.size(), crypto_secret_key.iv, crypto_secret_key.iv.size());
			our_encoder->decoder.ProcessString(message.raw_array(), message.limit());
#else
			throw new std::exception("error, no cryptopp for aes");
#endif
		} else {
			throw std::exception("Error, no secret protocol to use"); //TODO my exception
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


} // namespace supercloud
