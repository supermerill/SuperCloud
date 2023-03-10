
//#define CATCH_CONFIG_DISABLE

#include <catch_main.hpp> // main is in test_connection
//#include <catch2/catch.hpp>
#include "IdentityManager.hpp"
#include <cryptopp/osrng.h>
#include <cryptopp/rsa.h>


namespace supercloud::testcrypto {

    SCENARIO("testing CryptoPP::Integer conversions") {


        CryptoPP::Integer i(42);


        ByteBuff buff;
        putCryptoPPInteger(buff, i);

        CryptoPP::Integer i2 = getCryptoPPInteger(buff.rewind());

        REQUIRE(i.ConvertToLong() == 42);
        REQUIRE(i == i2);

        ByteBuff buff2;
        putCryptoPPInteger(buff2, i2);

        REQUIRE(buff.rewind() == buff2.rewind());
    }

    SCENARIO("testing createNewPublicKey()") {

        GIVEN("RSA") {
            IdentityManager mana;
            mana.createNewPublicKey(IdentityManager::EncryptionType::RSA);

            REQUIRE(mana.m_public_key.type == IdentityManager::EncryptionType::RSA);
            REQUIRE(!mana.m_public_key.raw_data.empty());
            REQUIRE(!mana.m_private_key.empty());
            REQUIRE(mana.m_private_key != mana.m_public_key.raw_data);

        }
        GIVEN("NAIVE") {
            IdentityManager mana;
            mana.createNewPublicKey(IdentityManager::EncryptionType::NAIVE);

            REQUIRE(mana.m_public_key.type == IdentityManager::EncryptionType::NAIVE);
            REQUIRE(!mana.m_public_key.raw_data.empty());
            REQUIRE(!mana.m_private_key.empty());
            REQUIRE(mana.m_private_key == mana.m_public_key.raw_data);
        }

        GIVEN("RSA test") {

            CryptoPP::InvertibleRSAFunction params;
            CryptoPP::AutoSeededRandomPool rand;
            params.GenerateRandomWithKeySize(rand, 128);

            ///////////////////////////////////////
            // Create Keys
            CryptoPP::RSA::PrivateKey private_key = CryptoPP::RSA::PrivateKey(params);
            CryptoPP::RSA::PublicKey public_key(params);

            //validation
            REQUIRE(private_key.Validate(rand, 3));

            REQUIRE(public_key.Validate(rand, 3));

            REQUIRE(private_key.GetModulus() == public_key.GetModulus());
            REQUIRE(private_key.GetPublicExponent() == public_key.GetPublicExponent());

            //save
            PrivateKey priv;
            PublicKey pub;
            putCryptoppPrivateKey(private_key, priv);
            putCryptoppPublicKey(public_key, pub);

            CryptoPP::RSA::PublicKey public_key_2 = getCryptoppPublicKey(pub);
            CryptoPP::RSA::PrivateKey private_key_2 = getCryptoppPrivateKey(priv);

            REQUIRE(public_key.GetModulus() == public_key_2.GetModulus());
            REQUIRE(public_key.GetPublicExponent() == public_key_2.GetPublicExponent());
            REQUIRE(private_key.GetModulus() == private_key_2.GetModulus());
            REQUIRE(private_key.GetPublicExponent() == private_key_2.GetPublicExponent());
            REQUIRE(private_key.GetPrivateExponent() == private_key_2.GetPrivateExponent());

            PrivateKey priv2;
            PublicKey pub2;
            putCryptoppPrivateKey(private_key_2, priv2);
            putCryptoppPublicKey(public_key_2, pub2);

            REQUIRE(pub2 == pub);
            REQUIRE(priv == priv2);
        }

	}

    SCENARIO("testing encrypt -> decrypt") {

        IdentityManager mana;
        mana.createNewPublicKey(IdentityManager::EncryptionType::RSA);

        IdentityManager mana2;
        mana2.createNewPublicKey(IdentityManager::EncryptionType::RSA);

        mana.m_peer_2_peerdata[2].rsa_public_key = mana2.m_public_key;
        mana2.m_peer_2_peerdata[1].rsa_public_key = mana.m_public_key;

        uint64_t test_msg = rand_u8() | (rand_u8() << 8) | (rand_u8() << 16) | (rand_u8() << 24);
        std::stringstream stream;
        stream << std::hex << test_msg;
        std::string str_input = stream.str();
        ByteBuff buff_in;
        buff_in.putUTF8(str_input).flip();
        std::vector<uint8_t> data_in = buff_in.getAll();

        std::vector<uint8_t> data = data_in;
        mana.encrypt(data, mana2.m_public_key);

        mana2.decrypt(data, mana.m_public_key);

        REQUIRE(data == data_in);

        ByteBuff buff_out;
        buff_out.put(data).flip();
        std::string str_output = buff_out.getUTF8();

        REQUIRE(str_output == str_input);

    }
}
