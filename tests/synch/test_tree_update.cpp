
#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include <filesystem>
#include <functional>
#include <sstream>


#include "utils/Parameters.hpp"
#include "network/PhysicalServer.hpp"
#include "network/IdentityManager.hpp"
#include "FakeNetwork.hpp"
#include "WaitConnection.hpp"
#include "fs/base/FsStorage.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"
#include "fs/base/FsChunk.hpp"
#include "fs/inmemory/FsStorageInMemory.hpp"
#include "synch/SynchTreeMessageManager.hpp"
#include "synch/SynchroDb.hpp"
#include "synch/FsExternalInterface.hpp"

namespace supercloud::test::updateree {
    typedef std::shared_ptr<SynchTreeMessageManager> MsgManaPtr;

    FsID newid() {
        return FsElt::createId(rand_u16() % 1 == 0 ? FsType::FILE : FsType::DIRECTORY, rand_u63(), ComputerId(rand_u63() & COMPUTER_ID_MASK));
    }


    typedef std::shared_ptr<PhysicalServer> ServPtr;
    typedef std::shared_ptr<FakeLocalNetwork> NetPtr;
    typedef std::shared_ptr<FsStorage> FsStoragePtr;
    typedef std::shared_ptr<SynchroDb> SynchPtr;
    typedef std::shared_ptr<FsExternalInterface> FsInterfacePtr;

    InMemoryParameters createNewConfiguration() {
        std::filesystem::path tmp_dir_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        //create temp install dir
        std::filesystem::create_directories(tmp_dir_path);

        InMemoryParameters params_net;
        params_net.setLong("ClusterId", std::hash<std::string>{}("clusternumber 1"));
        params_net.setString("ClusterPassphrase", "passcluster1");
        params_net.setString("SecretKeyType", "NONE");
        params_net.setString("PubKeyType", "NONE");

        return params_net;
    }

    ServPtr createPeerFakeNet(InMemoryParameters& params_net, NetPtr& network, const std::string& my_ip, uint16_t listen_port = 0) {
        ((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, network);

        if (my_ip != "") {
            params_net.set("PublicIp", my_ip);
            params_net.setInt("PublicPort", listen_port);
        }

        //launch first peer
        ServPtr net = PhysicalServer::createAndInit(
            std::make_unique<InMemoryParameters>(),
            std::shared_ptr<InMemoryParameters>(new InMemoryParameters(params_net)));
        if (listen_port > 100) {
            net->listen(listen_port);
        }
        net->launchUpdater();

        return net;
    }

    ServPtr createPeerFakeNets(InMemoryParameters& params_net, std::vector<NetPtr> networks, const std::string& my_ip, uint16_t listen_port = 0) {
        ((FakeSocketFactory*)ServerSocket::factory.get())->setNextInstanceConfiguration(my_ip, networks);

        if (my_ip != "" && listen_port != 0) {
            params_net.set("PublicIp", my_ip);
            params_net.setInt("PublicPort", listen_port);
        }

        //launch first peer
        ServPtr net = PhysicalServer::createAndInit(
            std::make_unique<InMemoryParameters>(),
            std::shared_ptr<InMemoryParameters>(new InMemoryParameters(params_net)));
        if (listen_port > 100) {
            net->listen(listen_port);
        }
        net->launchUpdater();

        return net;
    }

    ByteBuff stringToBuff(const std::string& str) {
        return ByteBuff{ (uint8_t*)str.c_str(), str.size() };
    }

    void addChunkToFile(FsStoragePtr fs, FsFilePtr file, const std::string& str) {
        fs->addChunkToFile(file, (uint8_t*)&str[0], str.size());
    }

    std::string toString(ByteBuff&& buff) {
        std::string str;
        for (int i = 0; i < buff.limit(); i++) {
            str.push_back(*((char*)(buff.raw_array() + i)));
        }
        return str;
    }
    inline ByteBuff readAll(FsChunk& chunk) {
        ByteBuff buffer;
        buffer.expand(chunk.size());
        chunk.read(buffer.raw_array(), 0, chunk.size());
        buffer.position(buffer.position() + chunk.size());
        return buffer.flip();
    }

    class MyClock : public Clock {
        ServPtr m_serv;
    public:
        MyClock(ServPtr serv) : m_serv(serv) {}
        virtual DateTime getCurrrentTime() { return m_serv->getCurrentTime(); }
    };
    std::tuple < FsStoragePtr, SynchPtr, MsgManaPtr, FsInterfacePtr > addFileSystem(ServPtr serv) {
        std::shared_ptr<MyClock> clock = std::make_shared<MyClock>(serv);
        FsStoragePtr fs = FsStoragePtr{ new FsStorageInMemory{ serv->getComputerId(), clock } };
        //create synch
        SynchPtr synch = SynchroDb::create();
        synch->init(fs, serv);
        synch->launch(); //create & register message manager
        //create chunk message manager
        MsgManaPtr chunk_mana = SynchTreeMessageManager::create(serv, fs, synch);
        FsInterfacePtr fs_int = FsExternalInterface::create(synch);
        return std::tuple< FsStoragePtr, SynchPtr, MsgManaPtr, FsInterfacePtr>{fs, synch, chunk_mana, fs_int};
    }

    // for connecting to an existing cluster
    void addEntryPoint(InMemoryParameters& param, const std::string& ip, uint16_t port) {
        param.setString("PeerIp", ip);
        param.setInt("PeerPort", port);
        param.setBool("FirstConnection", true);
    }
	SCENARIO("Test SynchTreeMessageManager invalidating file") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_42 = NetPtr{ new FakeLocalNetwork{"192.168.42"} }; //TODO use these ones
        NetPtr net_192_168_44 = NetPtr{ new FakeLocalNetwork{"192.168.44"} };
        std::map < std::string, NetPtr > fakeNetworks;
        std::string last_listen_ip = "";
        uint16_t last_listen_port = 0;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        addEntryPoint(param_serv1, "192.168.0.2", 4242);
        ServPtr serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1");
        auto [fs1, synch1, chunkmana1, fsint1 ] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        ServPtr serv2 = createPeerFakeNets(param_serv2, { net_192_168_0, net_192_168_42 }, "0.0.0.2", 4242);
        auto [fs2, synch2, chunkmana2, fsint2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv3 = createNewConfiguration();
        addEntryPoint(param_serv3, "192.168.42.2", 4242);
        ServPtr serv3 = createPeerFakeNets(param_serv3, { net_192_168_42, net_192_168_44 }, "0.0.0.3", 4242);
        auto [fs3, synch3, chunkmana3, fsint3] = addFileSystem(serv3);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv4 = createNewConfiguration();
        addEntryPoint(param_serv4, "192.168.44.3", 4242);
        ServPtr serv4 = createPeerFakeNet(param_serv4, net_192_168_44, "192.168.44.4");
        auto [fs4, synch4, chunkmana4, fsint4] = addFileSystem(serv4);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // 1 -> 2 <- 3 <- 4

        // network creator create the root, so evryone began with it
        ((FsStorageInMemory*)fs2.get())->createNewRoot();

        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2, 2);
        std::shared_ptr<WaitConnection> waiter3 = WaitConnection::create(serv3, 2);
        std::shared_ptr<WaitConnection> waiter4 = WaitConnection::create(serv4);
        serv1->connect();
        serv3->connect();
        serv4->connect();

        //wait connection
        DateTime waiting1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting2 = waiter2->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting3 = waiter3->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting4 = waiter4->startWait().waitConnection(std::chrono::milliseconds(10000));
        REQUIRE(waiting1 > 0);
        REQUIRE(waiting2 > 0);
        REQUIRE(waiting3 > 0);
        REQUIRE(waiting4 > 0);
        REQUIRE(serv1->getState().isConnected());
        REQUIRE(serv2->getState().isConnected());
        REQUIRE(serv3->getState().isConnected());
        REQUIRE(serv4->getState().isConnected());
        REQUIRE(serv1->getPeer()->isConnected());
        REQUIRE(serv2->getPeer()->isConnected());
        REQUIRE(serv3->getPeer()->isConnected());
        REQUIRE(serv4->getPeer()->isConnected());
        REQUIRE(serv1->getPeersCopy().size() == 1);
        REQUIRE(serv2->getPeersCopy().size() == 2);
        REQUIRE(serv3->getPeersCopy().size() == 2);
        REQUIRE(serv4->getPeersCopy().size() == 1);

        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());
        fs3->setMyComputerId(serv3->getComputerId());
        fs4->setMyComputerId(serv4->getComputerId());

        //the net updater is manual in this test.
        //i will send some for cluster admin message manger
        for (size_t i = 0; i < 10; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            serv1->update();
            serv2->update();
            serv3->update();
            serv4->update();
        }

        REQUIRE(fs2->hasLocally(fs1->getRoot()));
        REQUIRE(!fs1->hasLocally(fs1->getRoot()));
        REQUIRE(!fs3->hasLocally(fs1->getRoot()));
        REQUIRE(!fs4->hasLocally(fs1->getRoot()));


        //get the root in serv1
        FsDirPtr fs1_root;
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/" };
            path_request.make_preferred();
            auto future = fsint1->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(fut_res.error_code == 0);
            REQUIRE(!fut_res.is_file);
            fs1_root = FsElt::toDirectory(fut_res.object);
            REQUIRE(fs1_root.get() != nullptr);
            REQUIRE(fs1->hasLocally(fs1->getRoot()));
        }

        //now create  dirs & files in the serv1
        FsDirPtr fs1_dir1;
        { // using the external interface for the synchronization to occur
            int res = fsint1->createDirectory(fs1->loadDirectory(fs1->getRoot()), "dir1", CUGA_7777);
            REQUIRE(res == 0);
            std::filesystem::path path_request{ "/dir1" };
            path_request.make_preferred();
            auto future = fsint1->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(!fut_res.is_file);
            REQUIRE(fut_res.error_code == 0);
            fs1_dir1 = FsElt::toDirectory(fut_res.object);
        }
        //using the fs method for the rest, as it's easier.
        FsDirPtr fs1_dir11 = fs1->createNewDirectory(fs1_dir1, "dir11", std::vector<FsID>{}, CUGA_7777);
        FsDirPtr fs1_dir111 = fs1->createNewDirectory(fs1_dir11, "dir111", std::vector<FsID>{}, CUGA_7777);
        FsFilePtr fs1_fic2 = fs1->createNewFile(fs1->loadDirectory(fs1->getRoot()), "fic2", {}, CUGA_7777);
        addChunkToFile(fs1, fs1_fic2, ("fic2"));
        REQUIRE(fs1_fic2->getCurrent().size() == 1);
        FsFilePtr fs1_fic12 = fs1->createNewFile(fs1_dir1, "fic12", {}, CUGA_7777);
        addChunkToFile(fs1, fs1_fic12, ("datafic12"));
        FsFilePtr fs1_fic112 = fs1->createNewFile(fs1_dir11, "fic112", {}, CUGA_7777);
        addChunkToFile(fs1, fs1_fic112, ("datafic112"));
        FsFilePtr fs1_fic1112 = fs1->createNewFile(fs1_dir111, "fic1112", {}, CUGA_7777);
        addChunkToFile(fs1, fs1_fic1112, ("datafic1112"));

        REQUIRE(fs1->hasLocally(fs1_fic2->getCurrent().front()));
        REQUIRE(!fs2->hasLocally(fs1_fic2->getCurrent().front()));
        REQUIRE(!fs3->hasLocally(fs1_fic2->getCurrent().front()));
        REQUIRE(!fs4->hasLocally(fs1_fic2->getCurrent().front()));

        std::shared_ptr<FsExternalInterface> fse1 = FsExternalInterface::create(synch1);
        std::future<FsExternalInterface::ObjectRequestAnswer> fic112_future = fse1->get(std::filesystem::path{ "/dir1/dir11/fic112" }.make_preferred());
        std::future_status ok = fic112_future.wait_for(std::chrono::seconds(1));
        REQUIRE(std::future_status::ready == ok);
        FsExternalInterface::ObjectRequestAnswer answer = fic112_future.get();
        REQUIRE(answer.object);
        REQUIRE(answer.is_file);
        REQUIRE(answer.object->getId() == fs1_fic112->getId());
        REQUIRE(answer.object->getCurrent() == fs1_fic112->getCurrent());
        synch1->test_emit_invalidation_quick();


        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // the invalidation is transmitted after the future answer
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(!synch1->get_test_wait_current_invalidation().empty());

        //the net updater is manual in this test.
        //so i will send an update sequence, and this first one should allow serv1 to emit an invalidate to serv2 for the root.
        log("--- update to send the invalidation");
        serv1->update();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // now serv2 should have the root as "invalidated"
        REQUIRE(synch1->get_test_wait_current_invalidation().empty());
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(synch2->isInvalidated(3));

        // also update serv2 to transmit the invalidation to serv3 (and not to serv1)
        serv4->update();
        serv3->update();
        serv2->update();
        serv1->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(synch2->isInvalidated(3));
        REQUIRE(synch3->isInvalidated(3));
        REQUIRE(!synch4->isInvalidated(3));

        //update to transmit to serv4
        serv4->update();
        serv3->update();
        serv2->update();
        serv1->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(synch2->isInvalidated(3));
        REQUIRE(synch3->isInvalidated(3));
        REQUIRE(synch4->isInvalidated(3));

        REQUIRE(!fs4->hasLocally(fs4->getRoot()));

        // get filesystem in serv4
        FsFilePtr fs12_serv4;
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/dir1/fic12" };
            path_request.make_preferred();
            auto future = fsint4->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(fut_res.error_code == 0);
            REQUIRE(fut_res.is_file);
            fs12_serv4 = FsElt::toFile(fut_res.object);
            REQUIRE(fs12_serv4.get() != nullptr);
        }
        // get file data
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/dir1/fic12" };
            path_request.make_preferred();
            ByteBuff data_buffer{ fs12_serv4->size() };
            auto future = fsint4->getData(fs12_serv4, data_buffer.raw_array(), 0, fs12_serv4->size());
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(fut_res.error_code == 0);
            REQUIRE(toString(std::move(data_buffer)) == "datafic12");
        }


    }
}
