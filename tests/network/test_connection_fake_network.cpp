
//#define CATCH_CONFIG_DISABLE

#include <catch_main.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Parameters.hpp"
#include <filesystem>
#include <functional>

#include "network/PhysicalServer.hpp"
#include "network/IdentityManager.hpp"
#include "network/BoostAsioNetwork.hpp"
#include "FakeNetwork.hpp"

namespace supercloud::test {

	typedef std::shared_ptr<PhysicalServer> ServPtr;
	typedef std::shared_ptr<FakeLocalNetwork> NetPtr;

	Parameters createNewConfiguration() {
		std::filesystem::path tmp_dir_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		//create temp install dir
		std::filesystem::create_directories(tmp_dir_path);

		Parameters params_net(tmp_dir_path / "network.properties");
		params_net.setLong("ClusterId", std::hash<std::string>{}("clusternumber 1"));
		params_net.setString("ClusterPassphrase", "passcluster1");

		return params_net;
	}

	// for connecting to an existing cluster
	void addEntryPoint(Parameters& param, const std::string& ip, uint16_t port) {
		param.setString("PeerIp", ip);
		param.setInt("PeerPort", port);
		param.setBool("FirstConnection", true);
	}

	ServPtr createPeerFakeNet(Parameters& params_net, NetPtr& network, const std::string& my_ip, uint16_t listen_port = 0) {
		((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, network);

		//launch first peer
		ServPtr net = PhysicalServer::createAndInit(params_net.getFileOrCreate().parent_path());
		if (listen_port > 100) {
			net->listen(listen_port);
		}
		net->launchUpdater();

		return net;
	}


	//SCENARIO("testing the connection between two peers") {
	//	ServerSocket::factory.reset(new FakeSocketFactory());
	//	NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
	//	NetPtr net_192_168_2 = NetPtr{ new FakeLocalNetwork{"192.168.2"} };

	//	typedef std::shared_ptr<PhysicalServer> ServPtr;

	//	typedef std::shared_ptr<FakeLocalNetwork> Fnet;
	//	std::map < std::string, Fnet > fakeNetworks;
	//	std::string last_listen_ip = "";
	//	uint16_t last_listen_port = 0;

	//	Parameters param_serv1 = createNewConfiguration();
	//	ServPtr& serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1", 4242);
	//	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//	Parameters param_serv2 = createNewConfiguration();
	//	addEntryPoint(param_serv2, "192.168.0.1", 4242);
	//	ServPtr& serv2 = createPeerFakeNet(param_serv2, net_192_168_0, "192.168.0.2");
	//	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//	uint16_t computer_id_1;
	//	uint16_t computer_id_2;

	//	GIVEN(("serv2 don't listen and connect to serv1")) {
	//		serv2->connect();
	//		THEN("connect fully in less than 100ms") {
	//			//it has 100ms to connect.
	//			size_t milis = 0;
	//			bool success = false;
	//			for (; milis < 10000 && !success; milis += 1) {
	//				std::this_thread::sleep_for(std::chrono::milliseconds(1));
	//				success = serv1->getState().isConnected();
	//				success = success && serv2->getState().isConnected();
	//			}
	//			std::cout << "connection in " << milis << "milis\n";
	//			std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//			REQUIRE(serv1->getState().isConnected());
	//			REQUIRE(serv2->getState().isConnected());
	//			computer_id_1 = serv1->getComputerId();
	//			computer_id_2 = serv2->getComputerId();
	//			REQUIRE(serv1->getComputerId() != 0);
	//			REQUIRE(serv1->getPeerId() != 0);
	//			REQUIRE(serv1->getComputerId() != NO_COMPUTER_ID);
	//			REQUIRE(serv1->getPeerId() != NO_PEER_ID);
	//			REQUIRE(serv2->getComputerId() != 0);
	//			REQUIRE(serv2->getPeerId() != 0);
	//			REQUIRE(serv2->getComputerId() != NO_COMPUTER_ID);
	//			REQUIRE(serv2->getPeerId() != NO_PEER_ID);
	//			REQUIRE(serv2->getNbPeers() == 1);
	//			REQUIRE(serv2->getNbPeers() == 1);
	//			REQUIRE(serv1->getPeersCopy().front()->isConnected());
	//			REQUIRE(serv2->getPeersCopy().front()->isConnected());
	//			REQUIRE(serv1->getIdentityManager().getLoadedPeers().size() == 1); //no fake peer on serv1 because no connection endpoind (or invalid)
	//			REQUIRE(serv1->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv2->getComputerId());
	//			REQUIRE(serv2->getIdentityManager().getLoadedPeers().size() == 2); // there is a fake peer for first connection
	//			REQUIRE(serv2->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv1->getComputerId());
	//			//REQUIRE(serv1->getIdentityManager().);
	//			REQUIRE(milis < 100);

	//		}
	//	}

	//	GIVEN(("serv2 disconnect")) {
	//		serv2->close();
	//		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//		REQUIRE(!serv1->getState().isConnected());
	//		REQUIRE(!serv2->getState().isConnected());
	//	}

	//	std::filesystem::remove_all(param_serv1.getFileOrCreate().parent_path());
	//	std::filesystem::remove_all(param_serv1.getFileOrCreate().parent_path());
	//}

	//SCENARIO("testing the connection between three peers in the same network, with only one entry point") {
	//	ServerSocket::factory.reset(new FakeSocketFactory());
	//	NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

	//	typedef std::shared_ptr<PhysicalServer> ServPtr;

	//	typedef std::shared_ptr<FakeLocalNetwork> Fnet;
	//	std::map < std::string, Fnet > fakeNetworks;
	//	std::string last_listen_ip = "";
	//	uint16_t last_listen_port = 0;

	//	Parameters param_serv = createNewConfiguration();
	//	ServPtr& serv = createPeerFakeNet(param_serv, net_192_168_0, "192.168.0.1", 4242);
	//	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//	std::vector<Parameters> client_parameter;
	//	std::vector<ServPtr> client_software;
	//	for (int i = 0; i < 2; i++) {
	//		client_parameter.push_back(createNewConfiguration());
	//		addEntryPoint(client_parameter.back(), "192.168.0.1", 4242);
	//		client_software.push_back(createPeerFakeNet(client_parameter.back(), net_192_168_0, "192.168.0." + std::to_string(i + 2)));
	//		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	//	}

	//	uint16_t computer_id_1;
	//	uint16_t computer_id_2;

	//	GIVEN(("connect second")) {
	//		client_software[0]->connect();
	//		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	//		client_software[1]->connect();
	//		THEN("connect fully in less than 100ms") {
	//			//it has 100ms to connect.
	//			size_t milis = 0;
	//			bool success = false;
	//			for (; milis < 10000 && !success; milis += 1) {
	//				std::this_thread::sleep_for(std::chrono::milliseconds(1));
	//				success = serv->getState().isConnected();
	//				success = success && client_software[0]->getState().isConnected();
	//				success = success && client_software[1]->getState().isConnected();
	//			}
	//			std::cout << "connection in " << milis << "milis\n";
	//			std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//			REQUIRE(client_software[1]->getState().isConnected());
	//			REQUIRE(client_software[1]->getComputerId() != 0);
	//			REQUIRE(client_software[1]->getPeerId() != 0);
	//			REQUIRE(client_software[1]->getComputerId() != NO_COMPUTER_ID);
	//			REQUIRE(client_software[1]->getPeerId() != NO_PEER_ID);
	//			REQUIRE(client_software[1]->getNbPeers() >= 1);
	//			REQUIRE(client_software[1]->getNbPeers() >= 1);
	//			REQUIRE(serv->getPeersCopy().front()->isConnected());
	//			REQUIRE(client_software[1]->getPeersCopy().front()->isConnected());
	//			REQUIRE(serv->getIdentityManager().getLoadedPeers().size() == 2); // first & second
	//			REQUIRE(serv->getIdentityManager().getLoadedPeers().back()->getComputerId() == client_software[1]->getComputerId());
	//			REQUIRE(client_software[1]->getIdentityManager().getLoadedPeers().size() >= 2); // there is a fake peer for first connection, and maybe the first client
	//			REQUIRE(client_software[1]->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv->getComputerId());
	//			//REQUIRE(serv->getIdentityManager().);
	//			REQUIRE(milis < 100);
	//		}

	//	}

	//	serv->close();
	//	for (ServPtr& client : client_software) {
	//		client->close();
	//	}
	//	std::this_thread::sleep_for(std::chrono::milliseconds(10));


	//	std::filesystem::remove_all(param_serv.getFileOrCreate().parent_path());
	//	for (Parameters& client : client_parameter) {
	//		std::filesystem::remove_all(client.getFileOrCreate().parent_path());
	//	}
	//}

	SCENARIO("testing the full connection between three peers in the same network, with only one entry point") {
		size_t nb = 2;
		ServerSocket::factory.reset(new FakeSocketFactory());
		NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

		typedef std::shared_ptr<PhysicalServer> ServPtr;

		typedef std::shared_ptr<FakeLocalNetwork> Fnet;
		std::map < std::string, Fnet > fakeNetworks;
		std::string last_listen_ip = "";
		uint16_t last_listen_port = 0;

		Parameters param_serv = createNewConfiguration();
		ServPtr& serv = createPeerFakeNet(param_serv, net_192_168_0, "192.168.0.1", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		std::vector<Parameters> client_parameter;
		std::vector<ServPtr> client_software;
		for (int i = 0; i < nb; i++) {
			client_parameter.push_back(createNewConfiguration());
			addEntryPoint(client_parameter.back(), "192.168.0.1", 4242);
			client_software.push_back(createPeerFakeNet(client_parameter.back(), net_192_168_0, "192.168.0."+std::to_string(i+2)));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		GIVEN(("connect all others, sequentially")) {
			for (int i = 0; i < client_software.size(); i++) {
				client_software[i]->connect();
				size_t milis = 0;
				for (bool success = false; milis < 10000 && !client_software[i]->getState().isConnected(); milis += 10) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				std::cout << " === connected in "<< milis <<" ====\n";
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				std::cout << " === connected => going to next step ====\n";
			}

			{
				std::cout << " === wait connection between the two 'clients' ====\n";
				size_t milis = 0;
				for (bool success = false
					; milis < 10000 && 
					(!client_software[0]->getPeerPtr(client_software[1]->getPeerId()) || !client_software[0]->getPeerPtr(client_software[1]->getPeerId())->isConnected())
					; milis += 10) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				std::cout << " === FULLY connected in " << milis << " ====\n";
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				std::cout << " === connected => going to next step ====\n";
			}

			THEN("connect fully everything with evryone in less than two seconds") {

				for (size_t check = 0; check < 4; check++) {
					REQUIRE(client_software[check]->getState().isConnected());
					REQUIRE(client_software[check]->getComputerId() != 0);
					REQUIRE(client_software[check]->getPeerId() != 0);
					REQUIRE(client_software[check]->getComputerId() != NO_COMPUTER_ID);
					REQUIRE(client_software[check]->getPeerId() != NO_PEER_ID);
					REQUIRE(client_software[check]->getPeersCopy().size() == client_software.size());
					REQUIRE(client_software[check]->getIdentityManager().getLoadedPeers().size() == client_software.size());
				}
				for (size_t check = 0; check < client_software.size(); check++) {
					for (size_t with = check+1; with < client_software.size(); with++) {
						auto data_with = client_software[check]->getIdentityManager().getPeerData(client_software[check]->getIdentityManager().getLoadedPeer(client_software[with]->getComputerId()));
						REQUIRE(bool(data_with.peer));
						REQUIRE(data_with.rsa_public_key != "");
						REQUIRE(data_with.private_interface.has_value());
					}
				}
			}
		}

		serv->close();
		for (ServPtr& client : client_software) {
			client->close();
		}

		std::filesystem::remove_all(param_serv.getFileOrCreate().parent_path());
		for (Parameters& client : client_parameter) {
			std::filesystem::remove_all(client.getFileOrCreate().parent_path());
		}
	}
}
