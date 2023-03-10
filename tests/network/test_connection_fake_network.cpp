
#define CATCH_CONFIG_DISABLE

//#include <catch_main.hpp> // main is in test_connection_message
#include <catch2/catch.hpp>
#include "utils/Utils.hpp"
#include "utils/ByteBuff.hpp"
#include "utils/Parameters.hpp"
#include <filesystem>
#include <functional>

#include <boost/version.hpp>

#include "network/PhysicalServer.hpp"
#include "network/IdentityManager.hpp"
#include "network/BoostAsioNetwork.hpp"
#include "FakeNetwork.hpp"
#include "WaitConnection.hpp"

namespace supercloud::test::fake_network_tests {

	typedef std::shared_ptr<PhysicalServer> ServPtr;
	typedef std::shared_ptr<FakeLocalNetwork> NetPtr;

	ConfigFileParameters createNewConfiguration() {
		std::filesystem::path tmp_dir_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		//create temp install dir
		std::filesystem::create_directories(tmp_dir_path);

		ConfigFileParameters params_net(tmp_dir_path / "network.properties");
		params_net.setLong("ClusterId", std::hash<std::string>{}("clusternumber 1"));
		params_net.setString("ClusterPassphrase", "passcluster1");
		params_net.setString("SecretKeyType", "NONE");
		params_net.setString("PubKeyType", "NONE");

		return params_net;
	}

	// for connecting to an existing cluster
	void addEntryPoint(Parameters& param, const std::string& ip, uint16_t port) {
		param.setString("PeerIp", ip);
		param.setInt("PeerPort", port);
		param.setBool("FirstConnection", true);
	}

	ServPtr createPeerFakeNet(ConfigFileParameters& params_net, NetPtr& network, const std::string& my_ip, uint16_t listen_port = 0) {
		((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, network);

		//launch first peer
		ServPtr net = PhysicalServer::createAndInit(
			std::make_unique<InMemoryParameters>(),
			std::shared_ptr<ConfigFileParameters>(new ConfigFileParameters(params_net)));
		if (listen_port > 100) {
			net->listen(listen_port);
		}
		net->launchUpdater();

		return net;
	}

	ServPtr createPeerFakeNets(ConfigFileParameters& params_net, std::vector<NetPtr> networks, const std::string& my_ip, uint16_t listen_port = 0) {
		((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, networks);

		//launch first peer
		ServPtr net = PhysicalServer::createAndInit(
			std::make_unique<InMemoryParameters>(),
			std::shared_ptr<ConfigFileParameters>(new ConfigFileParameters(params_net)));
		if (listen_port > 100) {
			net->listen(listen_port);
		}
		net->launchUpdater();

		return net;
	}


	SCENARIO("testing the connection between two peers") {
		ServerSocket::factory.reset(new FakeSocketFactory());
		NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

		std::map < std::string, NetPtr > fakeNetworks;
		std::string last_listen_ip = "";
		uint16_t last_listen_port = 0;

		ConfigFileParameters param_serv1 = createNewConfiguration();
		ServPtr& serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		ConfigFileParameters param_serv2 = createNewConfiguration();
		addEntryPoint(param_serv2, "192.168.0.1", 4242);
		ServPtr& serv2 = createPeerFakeNet(param_serv2, net_192_168_0, "192.168.0.2");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		uint16_t computer_id_1;
		uint16_t computer_id_2;

		GIVEN(("serv2 don't listen and connect to serv1")) {
			std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
			std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2);
			serv2->connect();
			THEN("connect fully in less than 100ms") {
				//it has 100ms to connect.
				waiter1->startWait();
				waiter2->startWait();
				DateTime waiting_serv = waiter1->waitConnection(std::chrono::milliseconds(1000));
				REQUIRE(waiting_serv > 0);
				//REQUIRE(waiting_serv < 100);
				REQUIRE(serv1->getState().isConnected());
				REQUIRE(serv1->getPeer()->isConnected());
				waiting_serv = waiter2->waitConnection(std::chrono::milliseconds(1000));
				REQUIRE(waiting_serv > 0);
				//REQUIRE(waiting_serv < 100);
				REQUIRE(serv2->getState().isConnected());
				REQUIRE(serv2->getPeer()->isConnected());

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
				REQUIRE(serv2->getIdentityManager().getLoadedPeers().size() > 0); // there MAY BE a fake peer for first connection
				REQUIRE(serv2->getIdentityManager().getLoadedPeers().size() == 1); // can't see this fake peer anymore, seems merged
				REQUIRE(serv2->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv1->getComputerId());
				//REQUIRE(serv1->getIdentityManager().);

			}
		}

		GIVEN(("serv2 disconnect")) {
			serv2->close();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

			REQUIRE(!serv1->getState().isConnected());
			REQUIRE(!serv2->getState().isConnected());
		}

		std::filesystem::remove_all(param_serv1.getFileOrCreate().parent_path());
		std::filesystem::remove_all(param_serv1.getFileOrCreate().parent_path());
	}

	SCENARIO("testing the connection between three peers in the same network, with only one entry point") {
		ServerSocket::factory.reset(new FakeSocketFactory());
		NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

		std::map < std::string, NetPtr > fakeNetworks;
		std::string last_listen_ip = "";
		uint16_t last_listen_port = 0;

		ConfigFileParameters param_serv = createNewConfiguration();
		ServPtr serv = createPeerFakeNet(param_serv, net_192_168_0, "192.168.0.1", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		std::vector<ConfigFileParameters> client_parameter;
		std::vector<ServPtr> client_software;
		for (int i = 0; i < 2; i++) {
			client_parameter.push_back(createNewConfiguration());
			addEntryPoint(client_parameter.back(), "192.168.0.1", 4242);
			client_software.push_back(createPeerFakeNet(client_parameter.back(), net_192_168_0, "192.168.0." + std::to_string(i + 2)));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		GIVEN(("connect second")) {
			std::shared_ptr<WaitConnection> waiter0 = WaitConnection::create(serv, 2);
			std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(client_software[0], 1);
			client_software[0]->connect();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(client_software[1], 1);
			waiter2->startWait();
			client_software[1]->connect();
			THEN("connect fully in less than 100ms") {
				//it has 100ms to connect.
				//size_t milis = 0;
				//bool success = false;
				//for (; milis < 10000 && !success; milis += 1) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(1));
				//	success = serv->getState().isConnected();
				//	success = success && client_software[0]->getState().isConnected();
				//	success = success && client_software[1]->getState().isConnected();
				//}
				//std::cout << "connection in " << milis << "milis\n";
				//std::this_thread::sleep_for(std::chrono::milliseconds(10));
				//check serv
				DateTime waiting_serv = waiter0->startWait().waitConnection(std::chrono::milliseconds(10000));
				REQUIRE(waiting_serv > 0);
				//REQUIRE(waiting_serv < 100);
				REQUIRE(serv->getState().isConnected());
				REQUIRE(serv->getPeer()->isConnected());
				//check client_software[0]
				DateTime waiting_cli1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
				REQUIRE(waiting_cli1 > 0);
				//REQUIRE(waiting_cli1 < 100);
				REQUIRE(client_software[0]->getState().isConnected());
				REQUIRE(client_software[0]->getPeer()->isConnected());
				//check client_software[1]
				DateTime waiting_cli2 = waiter2->waitConnection(std::chrono::milliseconds(10000));
				REQUIRE(waiting_cli2 > 0);
				//REQUIRE(waiting_cli2 < 100);
				REQUIRE(client_software[1]->getState().isConnected());
				REQUIRE(client_software[1]->getPeer()->isConnected());

				//wait a bit for clusterAdminMessageManager to be emitted
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));

				//connected, stop message read
				std::vector<std::lock_guard<std::mutex>*> locks;
				for (PeerPtr peer : serv->getPeersCopy()) {
					locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
				}
				for (auto& client : client_software) {
					for (PeerPtr peer : client->getPeersCopy()) {
						locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
					}
				}

				REQUIRE(client_software[1]->getState().isConnected());
				REQUIRE(client_software[1]->getComputerId() != 0);
				REQUIRE(client_software[1]->getPeerId() != 0);
				REQUIRE(client_software[1]->getComputerId() != NO_COMPUTER_ID);
				REQUIRE(client_software[1]->getPeerId() != NO_PEER_ID);
				REQUIRE(client_software[1]->getNbPeers() >= 1);
				REQUIRE(client_software[1]->getNbPeers() >= 1);
				REQUIRE(serv->getPeersCopy().front()->isConnected());
				REQUIRE(client_software[1]->getPeersCopy().front()->isConnected());
				REQUIRE(serv->getIdentityManager().getLoadedPeers().size() == 2); // first & second
				REQUIRE((serv->getIdentityManager().getLoadedPeers().front()->getComputerId() == client_software[1]->getComputerId()
					|| serv->getIdentityManager().getLoadedPeers().back()->getComputerId() == client_software[1]->getComputerId()));
				//REQUIRE(client_software[1]->getIdentityManager().getLoadedPeers().size() >= 2); // there is a fake peer for first connection, and maybe the first client
				//REQUIRE(client_software[1]->getIdentityManager().getLoadedPeers().size() == 1); // can't see them anymore, correctly merged now
				REQUIRE(client_software[1]->getIdentityManager().getLoadedPeers().size() == 2); //he knowns about client_software[0] 
				REQUIRE(client_software[0]->getIdentityManager().getLoadedPeers().size() == 2); //he knowns about client_software[1] 
				REQUIRE((client_software[1]->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv->getComputerId() ||
					client_software[1]->getIdentityManager().getLoadedPeers().front()->getComputerId() == serv->getComputerId()));
				//REQUIRE(serv->getIdentityManager().);

				for (auto lock : locks) {
					delete lock;
				}
			}

		}

		serv->close();
		for (ServPtr& client : client_software) {
			client->close();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));


		std::filesystem::remove_all(param_serv.getFileOrCreate().parent_path());
		for (ConfigFileParameters& client : client_parameter) {
			std::filesystem::remove_all(client.getFileOrCreate().parent_path());
		}
	}

	SCENARIO("testing the connection between three peers in two networks") {
		ServerSocket::factory.reset(new FakeSocketFactory());
		NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
		NetPtr net_192_168_42 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

		std::map < std::string, NetPtr > fakeNetworks;
		std::string last_listen_ip = "";
		uint16_t last_listen_port = 0;

		ConfigFileParameters param_serv_router = createNewConfiguration();
		ServPtr serv_router = createPeerFakeNets(param_serv_router, { net_192_168_0, net_192_168_42 }, "0.0.0.1", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		ConfigFileParameters param_serv_res0 = createNewConfiguration();
		addEntryPoint(param_serv_res0, "192.168.0.1", 4242);
		ServPtr serv_res0 = createPeerFakeNet(param_serv_res0, net_192_168_0, "192.168.0.2", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		ConfigFileParameters param_serv_res42 = createNewConfiguration();
		addEntryPoint(param_serv_res42, "192.168.42.1", 4242);
		ServPtr serv_res42 = createPeerFakeNet(param_serv_res42, net_192_168_42, "192.168.42.2", 4242);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));


		GIVEN(("connect second")) {
			std::shared_ptr<WaitConnection> waiter0 = WaitConnection::create(serv_router, 2);
			std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv_res0, 1);
			serv_res0->connect();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv_res42, 1);
			waiter2->startWait();
			serv_res42->connect();
			THEN("connect fully in less than 100ms") {
				//it has 100ms to connect.
				//size_t milis = 0;
				//bool success = false;
				//for (; milis < 10000 && !success; milis += 1) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(1));
				//	success = serv->getState().isConnected();
				//	success = success && client_software[0]->getState().isConnected();
				//	success = success && client_software[1]->getState().isConnected();
				//}
				//std::cout << "connection in " << milis << "milis\n";
				//std::this_thread::sleep_for(std::chrono::milliseconds(10));
				//check serv
				DateTime waiting_serv = waiter0->startWait().waitConnection(std::chrono::milliseconds(10000));
				REQUIRE(waiting_serv > 0);
				//REQUIRE(waiting_serv < 100);
				REQUIRE(serv_router->getState().isConnected());
				REQUIRE(serv_router->getPeer()->isConnected());
				//check client_software[0]
				DateTime waiting_cli1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
				REQUIRE(waiting_cli1 > 0);
				//REQUIRE(waiting_cli1 < 100);
				REQUIRE(serv_res0->getState().isConnected());
				REQUIRE(serv_res0->getPeer()->isConnected());
				//check client_software[1]
				DateTime waiting_cli2 = waiter2->waitConnection(std::chrono::milliseconds(10000));
				REQUIRE(waiting_cli2 > 0);
				//REQUIRE(waiting_cli2 < 100);
				REQUIRE(serv_res42->getState().isConnected());
				REQUIRE(serv_res42->getPeer()->isConnected());

				//wait a bit for clusterAdminMessageManager to be emitted
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));

				//connected, stop message read
				std::vector<std::lock_guard<std::mutex>*> locks;
				for (PeerPtr peer : serv_router->getPeersCopy()) {
					locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
				}
				for (PeerPtr peer : serv_res0->getPeersCopy()) {
					locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
				}
				for (PeerPtr peer : serv_res42->getPeersCopy()) {
					locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
				}

				REQUIRE(serv_res42->getState().isConnected());
				REQUIRE(serv_res42->getComputerId() != 0);
				REQUIRE(serv_res42->getPeerId() != 0);
				REQUIRE(serv_res42->getComputerId() != NO_COMPUTER_ID);
				REQUIRE(serv_res42->getPeerId() != NO_PEER_ID);
				REQUIRE(serv_res42->getNbPeers() >= 1);
				REQUIRE(serv_res42->getNbPeers() >= 1);
				REQUIRE(serv_router->getPeersCopy().front()->isConnected());
				REQUIRE(serv_res42->getPeersCopy().front()->isConnected());
				REQUIRE(serv_router->getIdentityManager().getLoadedPeers().size() == 2); // first & second
				REQUIRE((serv_router->getIdentityManager().getLoadedPeers().front()->getComputerId() == serv_res42->getComputerId()
					|| serv_router->getIdentityManager().getLoadedPeers().back()->getComputerId() == serv_res42->getComputerId()));
				REQUIRE(serv_res0->getIdentityManager().getLoadedPeers().size() == 2); // see the router, and a knowledge about the other connected peer (becasue they have a published interface)
				REQUIRE(serv_res42->getIdentityManager().getLoadedPeers().size() == 2);
				REQUIRE(serv_res0->getIdentityManager().getLoadedPeers().front()->getComputerId() == serv_router->getComputerId());
				REQUIRE(serv_res42->getIdentityManager().getLoadedPeers().front()->getComputerId() == serv_router->getComputerId());
				//REQUIRE(serv->getIdentityManager().);

				for (auto lock : locks) {
					delete lock;
				}
			}

		}

		serv_router->close();
		serv_res0->close();
		serv_res42->close();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));


		std::filesystem::remove_all(param_serv_router.getFileOrCreate().parent_path());
		std::filesystem::remove_all(param_serv_res0.getFileOrCreate().parent_path());
		std::filesystem::remove_all(param_serv_res42.getFileOrCreate().parent_path());
	}

SCENARIO("testing the simple connection between clients and a server in the same network") {
	size_t nb = 3;
	ServerSocket::factory.reset(new FakeSocketFactory());
	NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

	std::map < std::string, NetPtr > fakeNetworks;
	std::string last_listen_ip = "";
	uint16_t last_listen_port = 0;

	std::vector<ConfigFileParameters> parameters;
	std::vector<ServPtr> computers;
	for (int i = 0; i < nb; i++) {
		parameters.push_back(createNewConfiguration());
		if (i > 0) addEntryPoint(parameters.back(), "192.168.0.1", 4242);
		computers.push_back(createPeerFakeNet(parameters.back(), net_192_168_0, "192.168.0." + std::to_string(i + 1), i == 0 ? 4242 : 0));
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	GIVEN(("connect all others, sequentially")) {
		for (int i = 1; i < computers.size(); i++) {
			msg(std::string(" === connect client ") + i + " ====\n");
			std::shared_ptr<WaitConnection> waiter = WaitConnection::create(computers[i], 1); // can only connect to the server
			waiter->startWait();
			computers[i]->connect();
			DateTime waiting_serv = waiter->waitConnection(std::chrono::milliseconds(10000));
			REQUIRE(waiting_serv > 0);
			//REQUIRE(waiting_serv < 100);
			REQUIRE(computers[i]->getState().isConnected());
			REQUIRE(computers[i]->getPeer()->isConnected());
			msg( std::string(" === client ") + i + " connected in " + waiting_serv + " ====\n");
		}
		msg(" === connected => going to next step ====\n");

		//connected, stop message read
		std::vector<std::lock_guard<std::mutex>*> locks;
		for (auto& comp : computers) {
			for (PeerPtr peer : comp->getPeersCopy()) {
				locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
			}
		}

		//{
		//	std::cout << " === wait connection between the two 'clients' ====\n";
		//	size_t milis = 0;
		//	for (bool success = false
		//		; milis < 10000 && 
		//		(!computers[1]->getPeerPtr(computers[2]->getPeerId()) || !computers[1]->getPeerPtr(computers[2]->getPeerId())->isConnected())
		//		; milis += 10) {
		//		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		//	}
		//	std::this_thread::sleep_for(std::chrono::milliseconds(100));
		//	std::cout << " === FULLY connected in " << milis << " ====\n";
		//	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		//}

		THEN("test") {
			{std::lock_guard lock{ *loglock() };
			for (size_t check = 0; check < computers.size(); check++) {
				REQUIRE(computers[check]->getState().isConnected());
				REQUIRE(computers[check]->getComputerId() != 0);
				REQUIRE(computers[check]->getPeerId() != 0);
				REQUIRE(computers[check]->getComputerId() != NO_COMPUTER_ID);
				REQUIRE(computers[check]->getPeerId() != NO_PEER_ID);
				REQUIRE(computers[check]->getPeersCopy().size() == (check == 0 ? 2 : 1));
				// IdentityManager contains connected peers & known peers we can't conenct to
				//REQUIRE(computers[check]->getIdentityManager().getLoadedPeers().size() == computers[check]->getPeersCopy().size());
			}
			}

			// stop locking messages;
			for (auto lock : locks) {
				delete lock;
			}
			//give some time to exchange peer list
			std::this_thread::sleep_for(std::chrono::milliseconds(10000));

			{std::lock_guard lock{ *loglock() };
			for (size_t check = 0; check < computers.size(); check++) {
				REQUIRE(computers[check]->getPeersCopy().size() == (check == 0 ? 2 : 1));
				REQUIRE(computers[check]->getIdentityManager().getLoadedPeers().size() == computers.size() - 1);
			}
			}

			for (size_t check = 0; check < computers.size(); check++) {
				for (size_t with = check + 1; with < computers.size(); with++) {
					auto data_with = computers[check]->getIdentityManager().getPeerData(computers[check]->getIdentityManager().getLoadedPeer(computers[with]->getComputerId()));
					REQUIRE(bool(data_with.peer));
					REQUIRE(!data_with.rsa_public_key.raw_data.empty());
					//REQUIRE(data_with.private_interface.has_value());
				}
			}
		}
	}

	for (ServPtr& client : computers) {
		client->close();
	}

	for (ConfigFileParameters& client : parameters) {
		std::filesystem::remove_all(client.getFileOrCreate().parent_path());
	}
}

	SCENARIO("testing the connection between three servers in the same network, with only one entry point") {
		size_t nb = 3;
		ServerSocket::factory.reset(new FakeSocketFactory());
		NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };

		std::map < std::string, NetPtr > fakeNetworks;
		std::string last_listen_ip = "";
		uint16_t last_listen_port = 0;

		std::vector<ConfigFileParameters> parameters;
		std::vector<ServPtr> computers;
		for (int i = 0; i < nb; i++) {
			parameters.push_back(createNewConfiguration());
			if (i > 0)addEntryPoint(parameters.back(), "192.168.0.1", 4242);
			computers.push_back(createPeerFakeNet(parameters.back(), net_192_168_0, "192.168.0." + std::to_string(i + 1), 4242));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		GIVEN(("connect all others, sequentially")) {
			for (int i = 1; i < computers.size(); i++) {
				std::cout << " === connect client " << i << " ====\n";
				std::shared_ptr<WaitConnection> waiter = WaitConnection::create(computers[i], i);
				waiter->startWait();
				computers[i]->connect();
				DateTime waiting_connection_time = waiter->waitConnection(std::chrono::milliseconds(i*10000));
				REQUIRE(waiting_connection_time > 0);
				//REQUIRE(waiting_connection_time < 1000);
				REQUIRE(computers[i]->getState().isConnected());
				REQUIRE(computers[i]->getPeer()->isConnected());
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				std::cout << " === client " << i << " connected in " << waiting_connection_time << " ====\n";
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				std::cout << " === connected => going to next step ====\n";
			}

			//connected, stop message read
			std::vector<std::lock_guard<std::mutex>*> locks;
			for (auto& comp : computers) {
				for (PeerPtr peer : comp->getPeersCopy()) {
					locks.push_back(new std::lock_guard{ peer->lockSocketRead() });
				}
			}

			THEN("test") {

				for (size_t check = 0; check < computers.size(); check++) {
					PeerList peers = computers[check]->getPeersCopy();
					foreach(peer_it, peers) { if (!(*peer_it)->isConnected()) peer_it.erase(); }
					std::vector<PeerPtr> loaded = computers[check]->getIdentityManager().getLoadedPeers();
					computers[check]->log_peers();
					{
						std::lock_guard lock{ *loglock() };
						REQUIRE(computers[check]->getState().isConnected());
						REQUIRE(computers[check]->getComputerId() != 0);
						REQUIRE(computers[check]->getPeerId() != 0);
						REQUIRE(computers[check]->getComputerId() != NO_COMPUTER_ID);
						REQUIRE(computers[check]->getPeerId() != NO_PEER_ID);
						REQUIRE(peers.size() == 2);
						REQUIRE(loaded.size() == computers.size() - 1);
					}
				}
				for (size_t check = 0; check < computers.size(); check++) {
					for (size_t with = check + 1; with < computers.size(); with++) {
						auto data_with = computers[check]->getIdentityManager().getPeerData(computers[check]->getIdentityManager().getLoadedPeer(computers[with]->getComputerId()));
						std::lock_guard lock{ *loglock() };
						REQUIRE(bool(data_with.peer));
						REQUIRE(!data_with.rsa_public_key.raw_data.empty());
						REQUIRE(data_with.private_interface.has_value());
					}
				}
			}

			// stop locking messages;
			for (auto lock : locks) {
				delete lock;
			}
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
		for (ServPtr& client : computers) {
			client->close();
		}

		for (ConfigFileParameters& client : parameters) {
			std::filesystem::remove_all(client.getFileOrCreate().parent_path());
		}
		//FIXME: a mean to wait for all threads to close.
		{std::lock_guard lock{ *loglock() };
		std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}
