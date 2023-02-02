
//#define CATCH_CONFIG_DISABLE

//#include <catch2/catch.hpp>
#include <catch_main.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Parameters.hpp"
//#include "libsupercloud/utils/ByteBuff.hpp"
//#include <libsupercloud/utils/ByteBuff.hpp>
#include <filesystem>
#include <functional>

#include "network/PhysicalServer.hpp"

namespace supercloud::test {

	uint64_t cluster_id = std::hash<std::string>{}("clusternumber 1");
	std::string cluster_passphrase = "passcluster1";

    void createPeer(const std::string& name, uint16_t listen_port = 4242) {
		
        //create temp install dir
        std::filesystem::path tmp_dir_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
        std::filesystem::create_directories(tmp_dir_path);
        //create config files
  //      std::filesystem::path net_prop_file = tmp_dir_path / "clusterIds.properties";
		//Parameters params_server_db{ net_prop_file };
		//params_server_db.setLong("clusterId", 42l);
		//params_server_db.setInt("computerId", 1);
		//params_server_db.set("passphrase", "catchtest");
		//params_server_db.set("publicKey", "pubkey1");
		//params_server_db.set("privateKey", "pubkey2");
		//params_server_db.setInt("nbPeers", 0);
		//size_t peer_idx = 0;
		//for (const PeerPtr& peer : this->loadedPeers) {
		//	params_server_db.set("peer" + std::to_string(peer_idx) + "_ip", peer->getIP());
		//	params_server_db.setInt("peer" + std::to_string(peer_idx) + "_port", peer->getPort());
		//	params_server_db.setInt("peer" + std::to_string(peer_idx) + "_id", peer->getComputerId());
		//	if (id2PublicKey.find(peer->getComputerId()) != id2PublicKey.end()) {
		//		params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", id2PublicKey[peer->getComputerId()]);
		//	} else {
		//		params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", "ERROR, CANT FIND");
		//	}
		//	//relance
		//	peer_idx++;
		//}
		//params_server_db.save();

		Parameters params_net(tmp_dir_path / "network.properties");
		params_net.setLong("ClusterId", cluster_id);
		params_net.setString("ClusterPassphrase", cluster_passphrase);

		// for connecting to an existing cluster
			//params_net.setString("PeerIp", "127.0.01");
			//params_net.setLong("PeerPort", "4242");
			//params_net.setBool("FirstConnection", true);


		params_net.setString("PrivKey", "priv1");
		params_net.setString("PubKey", "pub1");
		

        //launch first peer
		std::shared_ptr<FileSystemManager> stub{};
		std::shared_ptr<PhysicalServer> net{ new PhysicalServer{stub, tmp_dir_path} };
		net->listen(listen_port);
		net->launchUpdater();


    }


    SCENARIO("ByteBuff TrailInt") {
		GIVEN(("TrailInt 0")) {
			ByteBuff buff;
			buff.putTrailInt(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == 0);
			}
		}
		GIVEN(("TrailInt 1")) {
			ByteBuff buff;
			buff.putTrailInt(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == 1);
			}
		}
		GIVEN(("TrailInt -1")) {
			ByteBuff buff;
			buff.putTrailInt(-1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == -1);
			}
		}
		GIVEN(("TrailInt 63")) {
			ByteBuff buff;
			buff.putTrailInt(63).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == 63);
			}
		}
		GIVEN(("TrailInt -64")) {
			ByteBuff buff;
			buff.putTrailInt(-64).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == -64);
			}
		}
		GIVEN(("TrailInt 2bits")) {
			int nb_bits = 2;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = (1 << (7 * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
		GIVEN(("TrailInt 3bits")) {
			int nb_bits = 3;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = (1 << (7 * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
		GIVEN(("TrailInt 4bits")) {
			int nb_bits = 4;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = (1 << (7 * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
		GIVEN(("TrailInt 5bits")) {
			int nb_bits = 5;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = 0x7FFFFFFF;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val;
			THEN("check negative quasi-end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
    }

	SCENARIO("ByteBuff Byte") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().put(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().put(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 1);
			}
		}
		GIVEN(("0xFF")) {
			buff.reset().put(0xFF).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 0xFF);
			}
		}
		GIVEN(("0x7F")) {
			buff.reset().put(0x7F).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 0x7F);
			}
		}
	}

	SCENARIO("ByteBuff Int32") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putInt(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putInt(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == 1);
			}
		}
		GIVEN(("-1")) {
			buff.reset().putInt(-1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == -1);
			}
		}
		GIVEN(("0x7FFFFFFF = " + std::to_string(int32_t(0x7FFFFFFF)))) {
			buff.reset().putInt(0x7FFFFFFF).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == 0x7FFFFFFF);
			}
		}
	}

	SCENARIO("ByteBuff Int64") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putLong(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putLong(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == 1);
			}
		}
		GIVEN(("-1")) {
			buff.reset().putLong(-1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == -1);
			}
		}
		GIVEN(("max = " + std::to_string(std::numeric_limits<int64_t>::max()))) {
			buff.reset().putLong(std::numeric_limits<int64_t>::max()).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == std::numeric_limits<int64_t>::max());
			}
		}
	}

	SCENARIO("ByteBuff UInt64") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putULong(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putULong(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == 1);
			}
		}
		GIVEN(("-1 = "+std::to_string(uint64_t(-1)))) {
			buff.reset().putULong(uint64_t(-1)).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == uint64_t (-1));
			}
		}
		GIVEN(("max = " + std::to_string(std::numeric_limits<uint64_t>::max()))) {
			buff.reset().putULong(std::numeric_limits<uint64_t>::max()).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == std::numeric_limits<uint64_t>::max());
			}
		}
	}

	SCENARIO("ByteBuff String") {
		ByteBuff buff;
		GIVEN((" empty string")) {
			buff.reset().putUTF8("").flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getUTF8() == "");
			}
		}
		GIVEN((" '1' ")) {
			buff.reset().putUTF8("1").flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 2);
				REQUIRE(buff.getUTF8() == "1");
			}
		}
		GIVEN((" 'a bigger string' ")) {
			buff.reset().putUTF8("a bigger string").flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == std::string("a bigger string").length() + 1);
				REQUIRE(buff.getUTF8() == "a bigger string");
			}
		}
		GIVEN(("a very long string (20k cars)")) {
			std::stringstream ss;
			for (int i = 0; i < 20000; i++) {
				ss << char('a' + char(i % 20));
			}
			std::string str = ss.str();
			buff.reset().putUTF8(str).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 3+ 20000);
				REQUIRE(buff.getUTF8() == str);
			}
		}
		//TODO utf chars
	}
}
