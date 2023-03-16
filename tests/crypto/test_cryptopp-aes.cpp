
//#define CATCH_CONFIG_DISABLE

#include <catch_main.hpp>
//#include <catch2/catch.hpp>
//#include <cryptopp/dll.h>
#include "IdentityManager.hpp"
#include <cryptopp/osrng.h>
#include <cryptopp/eax.h>
#include <cryptopp/osrng.h>
#include <cryptopp/pssr.h>


namespace supercloud::testcryptoaes {
    
    SCENARIO("test aes encryption-decryption") {
        IdentityManager mana{ 1 };
        mana.m_encryption_type = IdentityManager::EncryptionType::AES;
        
        SecretKey our_sec_key = mana.createNewSecretKey(IdentityManager::EncryptionType::AES);

        mana.m_peer_2_peerdata[mana.m_myself].aes_key = our_sec_key;

        std::vector<uint8_t> data;
        for (size_t i = 0; i < rand_u8() + 10; i++) {
            data.push_back(rand_u8());
        }

        //random message number
        size_t message_counter = rand_u8();

        //create buff
        ByteBuff buffer;
        buffer.put(data);
        size_t good_limit = buffer.limit();
        //add zeros to check if the encode/decode don't do anything nasty
        buffer.expand(data.size());
        size_t max_limit = buffer.limit();
        std::fill(buffer.raw_array()+ buffer.position(), buffer.raw_array() + buffer.limit(), 0);
        buffer.flip();

        mana.encodeMessageSecret(buffer, mana.m_myself, message_counter);

        REQUIRE(buffer.position() == 0);
        REQUIRE(buffer.limit() == good_limit);
        //check that all extra space is still 0
        REQUIRE(((buffer.raw_array() + max_limit) == std::find(buffer.raw_array() + buffer.limit(), buffer.raw_array() + max_limit, true)));
        std::vector<uint8_t> encrypted = buffer.getAll();
        REQUIRE(encrypted != data);
        buffer.rewind();

        mana.decodeMessageSecret(buffer, mana.m_myself, message_counter);

        REQUIRE(buffer.position() == 0);
        REQUIRE(buffer.limit() == good_limit);
        //check that all extra space is still 0
        REQUIRE(((buffer.raw_array() + max_limit) == std::find(buffer.raw_array() + buffer.limit(), buffer.raw_array() + max_limit, true)));
        std::vector<uint8_t> decrypted = buffer.getAll();
        REQUIRE(decrypted == data);
        buffer.rewind();



    }


}
