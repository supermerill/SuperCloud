
#define CATCH_CONFIG_DISABLE

//#include <catch_main.hpp>
#include <catch2/catch.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Parameters.hpp"
#include <filesystem>
#include <functional>

#include "network/PhysicalServer.hpp"
#include "network/IdentityManager.hpp"
#include "network/BoostAsioNetwork.hpp"
#include "FakeNetwork.hpp"
#include "WaitConnection.hpp"

namespace supercloud::test {

	uint64_t cluster_id = std::hash<std::string>{}("clusternumber 1");
	std::string cluster_passphrase = "passcluster1";


	typedef std::shared_ptr<PhysicalServer> ServPtr;

	typedef std::shared_ptr<FakeLocalNetwork> Fnet;
	std::map < std::string, Fnet > fakeNetworks;
	std::string last_listen_ip = "";
	uint16_t last_listen_port = 0;

	ServPtr createPeerAsio(std::filesystem::path& tmp_dir_path, const std::string& name, uint16_t listen_port = 0) {
		std::string my_ip = "127.0.0.1";
		if (!ServerSocket::factory) {
			ServerSocket::factory.reset(new BoostAsioSocketFactory());
		}

		//create temp install dir
		std::filesystem::create_directories(tmp_dir_path);

		ConfigFileParameters params_net(tmp_dir_path / "network.properties");
		params_net.setLong("ClusterId", cluster_id);
		params_net.setString("ClusterPassphrase", cluster_passphrase);

		// for connecting to an existing cluster
		if ((my_ip != last_listen_ip || last_listen_port != listen_port) && (last_listen_ip != "")){
			params_net.setString("PeerIp", last_listen_ip);
			params_net.setInt("PeerPort", last_listen_port);
			params_net.setBool("FirstConnection", true);
		}

		//launch first peer
		ServPtr net = PhysicalServer::createAndInit(
			std::make_unique<InMemoryParameters>(),
			std::shared_ptr<ConfigFileParameters>(new ConfigFileParameters(params_net)));
		if (listen_port > 100) {
			net->listen(listen_port);
			last_listen_port = listen_port;
			last_listen_ip = my_ip;
		}
		net->launchUpdater();

		return net;
	}

	ServPtr createPeerFakeNet(std::filesystem::path& tmp_dir_path, uint8_t ip_num, uint16_t listen_port = 0) {
		std::string my_ip = std::string("192.168.0.") + std::to_string(ip_num);

		if (!ServerSocket::factory) {
			//ServerSocket::factory.reset(new BoostAsioSocketFactory());
			ServerSocket::factory.reset(new FakeSocketFactory());
			//simple network: one network, all computer inside it
			Fnet net1 = Fnet{ new FakeLocalNetwork{"192.168.0"} };
			fakeNetworks[net1->getNetworkIp()] = (net1);
			((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, fakeNetworks["192.168.0"]);
		} else {
			((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, fakeNetworks["192.168.0"]);
		}

		//create temp install dir
		std::filesystem::create_directories(tmp_dir_path);

		ConfigFileParameters params_net(tmp_dir_path / "network.properties");
		params_net.setLong("ClusterId", cluster_id);
		params_net.setString("ClusterPassphrase", cluster_passphrase);

		// for connecting to an existing cluster
		if ((my_ip != last_listen_ip || last_listen_port != listen_port) && (last_listen_ip != "")) {
			params_net.setString("PeerIp", last_listen_ip);
			params_net.setInt("PeerPort", last_listen_port);
			params_net.setBool("FirstConnection", true);
		}

		//launch first peer
		ServPtr net = PhysicalServer::createAndInit(
			std::make_unique<InMemoryParameters>(),
			std::shared_ptr<ConfigFileParameters>(new ConfigFileParameters(params_net)));
		if (listen_port > 100) {
			net->listen(listen_port);
			last_listen_port = listen_port;
			last_listen_ip = my_ip;
		}
		net->launchUpdater();

		return net;
	}


    SCENARIO("testing the connection between two peers") {
		std::filesystem::path tmp_dir_serv1{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::filesystem::path tmp_dir_serv2{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		ServPtr& serv1 = createPeerAsio(tmp_dir_serv1, "serv1", 4242);
		//ServPtr& serv1 = createPeerFakeNet(tmp_dir_serv1, 11, 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		ServPtr& serv2 = createPeerAsio(tmp_dir_serv1, "serv2");
		//ServPtr& serv2 = createPeerFakeNet(tmp_dir_serv2, 22);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		uint16_t computer_id_1;
		uint16_t computer_id_2;

		GIVEN(("serv2 don't listen and connect to serv1")) {
			std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
			std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2);
			waiter1->startWait();
			waiter2->startWait();
			serv2->connect();
			THEN("connect fully in less than 100ms") {
				//it has 100ms to connect.
				size_t milis = get_current_time_milis();
				//bool success = false;
				//for (; milis < 10000 && !success; milis += 1) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(1));
				//	success = serv1->getState().isConnected();
				//	success = success && serv2->getState().isConnected();
				//}
				DateTime waiting_serv1 = waiter1->waitConnection(std::chrono::milliseconds(100));
				REQUIRE(waiting_serv1 != 0);
				REQUIRE(waiting_serv1 < 100);
				REQUIRE(serv->getState().isConnected());
				REQUIRE(serv->getPeer()->isConnected());
				DateTime waiting_serv2 = waiter2->waitConnection(std::chrono::milliseconds(100));
				REQUIRE(waiting_serv2 != 0);
				REQUIRE(waiting_serv2 < 100);
				REQUIRE(serv2->getPeer()->isConnected());
				REQUIRE(serv2->getState().isConnected());

				std::this_thread::sleep_for(std::chrono::milliseconds(1));

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
				REQUIRE(serv1->getIdentityManager().getLoadedPeers().size() == 1); //no fake peer on serv1 because no connection endpoind (or invalid)
				REQUIRE(serv1->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv2->getComputerId());
				REQUIRE(serv2->getIdentityManager().getLoadedPeers().size() == 2); // there is a fake peer for first connection
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
