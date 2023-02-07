//#include "libsupercloud/utils/ByteBuff.hpp"
//#include <libsupercloud/utils/ByteBuff.hpp>
#include <filesystem>
#include <functional>

#include "utils/Parameters.hpp"
#include "network/PhysicalServer.hpp"
#include "network/BoostAsioNetwork.hpp"

using namespace supercloud;

uint64_t cluster_id = std::hash<std::string>{}("clusternumber 1");
std::string cluster_passphrase = "passcluster1";

std::shared_ptr<PhysicalServer> createPeer(const std::string& name, uint16_t listen_port = 4242) {
		
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


	//params_net.setString("PrivKey", "priv1");
	//params_net.setString("PubKey", "pub1");
		

    //launch first peer
	std::shared_ptr<PhysicalServer> net = PhysicalServer::createAndInit(tmp_dir_path);
	net->listen(listen_port);
	net->launchUpdater();

	return net;
}


std::shared_ptr<PhysicalServer> createPeer2(const std::string& name, uint16_t listen_port = 4243) {

	//create temp install dir
	std::filesystem::path tmp_dir_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
	std::filesystem::create_directories(tmp_dir_path);

	Parameters params_net(tmp_dir_path / "network.properties");
	params_net.setLong("ClusterId", cluster_id);
	params_net.setString("ClusterPassphrase", cluster_passphrase);

	// for connecting to an existing cluster
	params_net.setString("PeerIp", "127.0.0.1");
	params_net.setLong("PeerPort", 4242L);
	params_net.setBool("FirstConnection", true);

	//launch first peer
	std::shared_ptr<PhysicalServer> net = PhysicalServer::createAndInit(tmp_dir_path);
	net->listen(listen_port);
	net->launchUpdater();

	return net;
}

int main(int argc, char* argv[]) {
	ServerSocket::factory.reset(new BoostAsioSocketFactory());
		
	std::cout << "start main\n";
	auto net = createPeer("peer1");



	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	auto net2 = createPeer2("peer2");
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	net2->connect();

	std::this_thread::sleep_for(std::chrono::seconds(10));

	std::cout << "--- C L O S E !! ---\n";
	net->close();
	//net2->close();
	std::cout << "--- C L O S E D !! ---\n";

	std::this_thread::sleep_for(std::chrono::seconds(1000));
	return 0;
}
