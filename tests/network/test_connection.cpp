
//#define CATCH_CONFIG_DISABLE

#include <catch_main.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Parameters.hpp"
#include <filesystem>
#include <functional>

#include "network/PhysicalServer.hpp"
#include "network/IdentityManager.hpp"
#include "network/BoostAsioNetwork.hpp"

namespace supercloud::test {

	uint64_t cluster_id = std::hash<std::string>{}("clusternumber 1");
	std::string cluster_passphrase = "passcluster1";

	//std::shared_ptr<PhysicalServer> createPeer_temp(const std::string& name, uint16_t listen_port = 4242) {

	//	//create temp install dir
	//	std::filesystem::path tmp_dir_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
	//	std::filesystem::create_directories(tmp_dir_path);
	//	//create config files
	////      std::filesystem::path net_prop_file = tmp_dir_path / "clusterIds.properties";
	//	//Parameters params_server_db{ net_prop_file };
	//	//params_server_db.setLong("clusterId", 42l);
	//	//params_server_db.setInt("computerId", 1);
	//	//params_server_db.set("passphrase", "catchtest");
	//	//params_server_db.set("publicKey", "pubkey1");
	//	//params_server_db.set("privateKey", "pubkey2");
	//	//params_server_db.setInt("nbPeers", 0);
	//	//size_t peer_idx = 0;
	//	//for (const PeerPtr& peer : this->loadedPeers) {
	//	//	params_server_db.set("peer" + std::to_string(peer_idx) + "_ip", peer->getIP());
	//	//	params_server_db.setInt("peer" + std::to_string(peer_idx) + "_port", peer->getPort());
	//	//	params_server_db.setInt("peer" + std::to_string(peer_idx) + "_id", peer->getComputerId());
	//	//	if (id2PublicKey.find(peer->getComputerId()) != id2PublicKey.end()) {
	//	//		params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", id2PublicKey[peer->getComputerId()]);
	//	//	} else {
	//	//		params_server_db.set("peer" + std::to_string(peer_idx) + "_publicKey", "ERROR, CANT FIND");
	//	//	}
	//	//	//relance
	//	//	peer_idx++;
	//	//}
	//	//params_server_db.save();

	//	Parameters params_net(tmp_dir_path / "network.properties");
	//	params_net.setLong("ClusterId", cluster_id);
	//	params_net.setString("ClusterPassphrase", cluster_passphrase);

	//	// for connecting to an existing cluster
	//		//params_net.setString("PeerIp", "127.0.01");
	//		//params_net.setLong("PeerPort", "4242");
	//		//params_net.setBool("FirstConnection", true);


	//	//params_net.setString("PrivKey", "priv1");
	//	//params_net.setString("PubKey", "pub1");


	//	//launch first peer
	//	std::shared_ptr<FileSystemManager> stub{ new FileSystemManager() };
	//	std::shared_ptr<PhysicalServer> net{ new PhysicalServer{stub, tmp_dir_path} };
	//	net->listen(listen_port);
	//	net->launchUpdater();

	//	return net;
	//}

	typedef std::shared_ptr<PhysicalServer> ServPtr;

	ServPtr createPeer(std::filesystem::path& tmp_dir_path, const std::string& name, uint16_t listen_port = 0) {
		if(!ServerSocket::factory) ServerSocket::factory.reset(new BoostAsioSocketFactory());

		//create temp install dir
		std::filesystem::create_directories(tmp_dir_path);

		Parameters params_net(tmp_dir_path / "network.properties");
		params_net.setLong("ClusterId", cluster_id);
		params_net.setString("ClusterPassphrase", cluster_passphrase);

		// for connecting to an existing cluster
		params_net.setString("PeerIp", "127.0.0.1");
		params_net.setLong("PeerPort", 4242L);
		params_net.setBool("FirstConnection", true);

		//launch first peer
		ServPtr net = PhysicalServer::createAndInit(tmp_dir_path);
		if (listen_port > 100) {
			net->listen(listen_port);
		}
		net->launchUpdater();

		return net;
	}


    SCENARIO("testing the connection between two peers") {
		std::filesystem::path tmp_dir_serv1{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::filesystem::path tmp_dir_serv2{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		ServPtr& serv1 = createPeer(tmp_dir_serv1, "serv1", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		ServPtr& serv2 = createPeer(tmp_dir_serv2, "serv2");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		uint16_t computer_id_1;
		uint16_t computer_id_2;

		GIVEN(("serv2 don't listen and connect to serv1")) {
			serv2->connect();
			THEN("connect fully in less than 100ms") {
				//it has 100ms to connect.
				size_t milis = 0;
				bool success = false;
				for (; milis < 10000 && !success; milis += 1) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					success = serv1->getState().isConnected();
					success = success && serv2->getState().isConnected();
				}
				std::cout << "connection in " << milis << "milis\n";
				std::this_thread::sleep_for(std::chrono::milliseconds(10));

				REQUIRE(serv1->getState().isConnected());
				REQUIRE(serv2->getState().isConnected());
				computer_id_1 = serv1->getComputerId();
				computer_id_2 = serv2->getComputerId();
				REQUIRE(serv1->getComputerId() != 0);
				REQUIRE(serv1->getPeerId() != 0);
				REQUIRE(serv1->getComputerId() != NO_COMPUTER_ID);
				REQUIRE(serv1->getPeerId() != NO_PEER_ID);
				REQUIRE(serv2->getComputerId() != 0);
				REQUIRE(serv2->getPeerId() != 0);
				REQUIRE(serv2->getComputerId() != NO_COMPUTER_ID);
				REQUIRE(serv2->getPeerId() != NO_PEER_ID);
				REQUIRE(serv2->getNbPeers() == 1);
				REQUIRE(serv2->getNbPeers() == 1);
				REQUIRE(serv1->getPeersCopy().front()->isConnected());
				REQUIRE(serv2->getPeersCopy().front()->isConnected());
				REQUIRE(serv1->getIdentityManager().getLoadedPeers().size() == 2); // there is a fake peer for first connection
				REQUIRE(serv1->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv2->getComputerId());
				REQUIRE(serv2->getIdentityManager().getLoadedPeers().size() == 2);
				REQUIRE(serv2->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv1->getComputerId());
				//REQUIRE(serv1->getIdentityManager().);
				REQUIRE(milis < 100);

			}
		}

		GIVEN(("serv2 disconnect")) {
			serv2->close();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

			REQUIRE(!serv1->getState().isConnected());
			REQUIRE(!serv2->getState().isConnected());
		}

		std::filesystem::remove_all(tmp_dir_serv1);
		std::filesystem::remove_all(tmp_dir_serv2);
	}
}
