
//#define CATCH_CONFIG_DISABLE

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

    FsID newid() {
        return FsElt::createId(rand_u16() % 1 == 0 ? FsType::FILE : FsType::DIRECTORY, rand_u63(), ComputerId(rand_u63() & COMPUTER_ID_MASK));
    }


    typedef std::shared_ptr<PhysicalServer> ServPtr;
    typedef std::shared_ptr<FakeLocalNetwork> NetPtr;
    typedef std::shared_ptr<FsStorage> FsStoragePtr;
    typedef std::shared_ptr<SynchroDb> SynchPtr;
    typedef std::shared_ptr<SynchTreeMessageManager> TreeManaPtr;
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
        //the net updater is manual in this test.
        //net->launchUpdater();

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
        //the net updater is manual in this test.
        //net->launchUpdater();

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

    //class MyClock : public Clock {
    //    ServPtr m_serv;
    //public:
    //    DateTime offset = 0;
    //    MyClock(ServPtr serv) : m_serv(serv) {}
    //    virtual DateTime getCurrrentTime() { return serv->getCurrrentTime() + offset; }
    //};
    class MyClock : public Clock {
    public:
        DateTime offset = 0;
        virtual DateTime getCurrentTime() override { return this->get_current_time_milis() + offset; }
    };
    typedef std::shared_ptr<MyClock> ClockPtr;

    //class GlobalClock {
    //public:
    //    std::vector<ClockPtr> all;
    //    GlobalClock& addClock(ClockPtr& clock) { all.push_back(clock); return *this; }

    //    void addMilis(DateTime milis) {
    //        for(auto& clock : all) clock->offset += milis;
    //    };
    //};

    std::tuple < FsStoragePtr, SynchPtr, TreeManaPtr, FsInterfacePtr> addFileSystem(ServPtr serv) {
        FsStoragePtr fs = FsStoragePtr{ new FsStorageInMemory{ serv->getComputerId(), serv } };
        //create synch
        SynchPtr synch = SynchroDb::create();
        synch->init(fs, serv);
        synch->launch(); //create & register synch, tree & chunk manager
        FsInterfacePtr fs_int = FsExternalInterface::create(synch);
        return std::tuple< FsStoragePtr, SynchPtr, TreeManaPtr, FsInterfacePtr>{fs, synch, synch->test_treeManager(), fs_int};
    }

    // for connecting to an existing cluster
    void addEntryPoint(InMemoryParameters& param, const std::string& ip, uint16_t port) {
        param.setString("PeerIp", ip);
        param.setInt("PeerPort", port);
        param.setBool("FirstConnection", true);
    }

    // B create root
    // A B C D connected
    // A get root
    // A create many dirs
    // many updates -> BCD get invalidation for root, one tick at a time.
    // D want a new file -> ask to C -> ask to B -> ask to A
    // D get the new tree & the file data.
	SCENARIO("Test SynchTreeMessageManager invalidating file") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_42 = NetPtr{ new FakeLocalNetwork{"192.168.42"} }; //TODO use these ones
        NetPtr net_192_168_44 = NetPtr{ new FakeLocalNetwork{"192.168.44"} };
        std::map < std::string, NetPtr > fakeNetworks;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        addEntryPoint(param_serv1, "192.168.0.2", 4242);
        ServPtr serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1");
        auto [fs1, synch1, treesynch1, fsint1 ] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        ServPtr serv2 = createPeerFakeNets(param_serv2, { net_192_168_0, net_192_168_42 }, "0.0.0.2", 4242);
        auto [fs2, synch2, treesynch2, fsint2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv3 = createNewConfiguration();
        addEntryPoint(param_serv3, "192.168.42.2", 4242);
        ServPtr serv3 = createPeerFakeNets(param_serv3, { net_192_168_42, net_192_168_44 }, "0.0.0.3", 4242);
        auto [fs3, synch3, treesynch3, fsint3] = addFileSystem(serv3);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv4 = createNewConfiguration();
        addEntryPoint(param_serv4, "192.168.44.3", 4242);
        ServPtr serv4 = createPeerFakeNet(param_serv4, net_192_168_44, "192.168.44.4");
        auto [fs4, synch4, treesynch4, fsint4] = addFileSystem(serv4);
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

    //TODO: scenario 
    // A create a dir
    // B & C is notified of a change
    // A disconnect
    // C want the change, ask B
    // B can't contact A -> answer 'invalid', removing the pending change.
    SCENARIO("Test SynchTreeMessageManager invalidation in disconnected peer") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_42 = NetPtr{ new FakeLocalNetwork{"192.168.42"} };
        std::map < std::string, NetPtr > fakeNetworks;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        addEntryPoint(param_serv1, "192.168.0.2", 4242);
        ServPtr serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1");
        auto [fs1, synch1, treesynch1, fsint1] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        ServPtr serv2 = createPeerFakeNets(param_serv2, { net_192_168_0, net_192_168_42 }, "0.0.0.2", 4242);
        auto [fs2, synch2, treesynch2, fsint2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv3 = createNewConfiguration();
        addEntryPoint(param_serv3, "192.168.42.2", 4242);
        ServPtr serv3 = createPeerFakeNet(param_serv3, net_192_168_42, "192.168.42.3");
        auto [fs3, synch3, treesynch3, fsint3] = addFileSystem(serv3);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // 1 -> 2 <- 3

        // network creator create the root, so evryone began with it
        ((FsStorageInMemory*)fs2.get())->createNewRoot();

        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2, 2);
        std::shared_ptr<WaitConnection> waiter3 = WaitConnection::create(serv3);
        serv1->connect();
        serv3->connect();

        //wait connection
        DateTime waiting1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting2 = waiter2->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting3 = waiter3->startWait().waitConnection(std::chrono::milliseconds(10000));
        REQUIRE(waiting1 > 0);
        REQUIRE(waiting2 > 0);
        REQUIRE(waiting3 > 0);
        REQUIRE(serv1->getState().isConnected());
        REQUIRE(serv2->getState().isConnected());
        REQUIRE(serv3->getState().isConnected());
        REQUIRE(serv1->getPeer()->isConnected());
        REQUIRE(serv2->getPeer()->isConnected());
        REQUIRE(serv3->getPeer()->isConnected());
        REQUIRE(serv1->getPeersCopy().size() == 1);
        REQUIRE(serv2->getPeersCopy().size() == 2);
        REQUIRE(serv3->getPeersCopy().size() == 1);

        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());
        fs3->setMyComputerId(serv3->getComputerId());

        //the net updater is manual in this test.
        //i will send some for cluster admin message manger
        for (size_t i = 0; i < 10; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            serv1->update();
            serv2->update();
            serv3->update();
        }

        REQUIRE(fs2->hasLocally(fs1->getRoot()));
        REQUIRE(!fs1->hasLocally(fs1->getRoot()));
        REQUIRE(!fs3->hasLocally(fs1->getRoot()));

        //share the root
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/" };
            path_request.make_preferred();
            auto future = fsint1->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(!fut_res.is_file);
            REQUIRE(fut_res.error_code == 0);
            future = fsint3->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            fut_res = future.get();
            REQUIRE(!fut_res.is_file);
            REQUIRE(fut_res.error_code == 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        REQUIRE(fs2->hasLocally(fs1->getRoot()));
        REQUIRE(fs1->hasLocally(fs1->getRoot()));
        REQUIRE(fs3->hasLocally(fs1->getRoot()));

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
        serv3->update();
        serv2->update();
        serv1->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(synch2->isInvalidated(3));
        REQUIRE(synch3->isInvalidated(3));

        // now remove serv1 from network
        serv1->close();
        for (int i = 0; i < 3; i++) {
            serv1->update();
            serv2->update();
            serv3->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(!serv1->getState().isConnected());
        REQUIRE(serv2->getState().isConnected());
        REQUIRE(serv3->getState().isConnected());
        REQUIRE(!serv1->getPeer()->isConnected());
        REQUIRE(serv2->getPeer()->isConnected());
        REQUIRE(serv3->getPeer()->isConnected());
        REQUIRE(serv1->getPeersCopy().size() == 0);
        REQUIRE(serv2->getPeersCopy().size() == 1);
        REQUIRE(serv3->getPeersCopy().size() == 1);


        // get filesystem in serv3
        FsFilePtr fs12_serv4;
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/dir1/fic12" };
            path_request.make_preferred();
            auto future = fsint3->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(fut_res.error_code != 0);
            log(fut_res.error_message);
        }
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(!synch3->isInvalidated(3));

    }



    //TODO: scenario 
    // A create a dir
    // B & C is notified of a change
    // +5minutes
    // B update -> no more changes possible
    // C ask B for changes
    // B answer with the same boring thing 
    // C accept the boring thing, removing the invalidation
    // A update -> it send the changes by itself
    // B receive the changes.
    // C is notified of the changes.
    // 5 minutes pass
    // B update -> no change send (not its changes)
    // C update -> request the changes.
    // B correctly send the new changes, A B and C aare now in synch
    SCENARIO("Test SynchTreeMessageManager auto sending & fetching tree changes") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_42 = NetPtr{ new FakeLocalNetwork{"192.168.42"} };
        std::map < std::string, NetPtr > fakeNetworks;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        addEntryPoint(param_serv1, "192.168.0.2", 4242);
        ServPtr serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1");
        auto [fs1, synch1, treesynch1, fsint1] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        ServPtr serv2 = createPeerFakeNets(param_serv2, { net_192_168_0, net_192_168_42 }, "0.0.0.2", 4242);
        auto [fs2, synch2, treesynch2, fsint2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv3 = createNewConfiguration();
        addEntryPoint(param_serv3, "192.168.42.2", 4242);
        ServPtr serv3 = createPeerFakeNet(param_serv3, net_192_168_42, "192.168.42.3");
        auto [fs3, synch3, treesynch3, fsint3] = addFileSystem(serv3);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        ClockPtr global_clock = ClockPtr{ new MyClock() };
        serv1->changeClock(global_clock);
        serv2->changeClock(global_clock);
        serv3->changeClock(global_clock);
        // 1 -> 2 <- 3

        // network creator create the root, so evryone began with it
        ((FsStorageInMemory*)fs2.get())->createNewRoot();

        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2, 2);
        std::shared_ptr<WaitConnection> waiter3 = WaitConnection::create(serv3);
        serv1->connect();
        serv3->connect();

        //wait connection
        DateTime waiting1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting2 = waiter2->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting3 = waiter3->startWait().waitConnection(std::chrono::milliseconds(10000));
        REQUIRE(waiting1 > 0);
        REQUIRE(waiting2 > 0);
        REQUIRE(waiting3 > 0);
        REQUIRE(serv1->getState().isConnected());
        REQUIRE(serv2->getState().isConnected());
        REQUIRE(serv3->getState().isConnected());
        REQUIRE(serv1->getPeer()->isConnected());
        REQUIRE(serv2->getPeer()->isConnected());
        REQUIRE(serv3->getPeer()->isConnected());
        REQUIRE(serv1->getPeersCopy().size() == 1);
        REQUIRE(serv2->getPeersCopy().size() == 2);
        REQUIRE(serv3->getPeersCopy().size() == 1);

        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());
        fs3->setMyComputerId(serv3->getComputerId());

        //the net updater is manual in this test.
        //i will send some for cluster admin message manger
        for (size_t i = 0; i < 10; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            serv1->update();
            serv2->update();
            serv3->update();
        }
        DateTime old = serv1->getCurrentTime();

        REQUIRE(fs2->hasLocally(fs1->getRoot()));
        REQUIRE(!fs1->hasLocally(fs1->getRoot()));
        REQUIRE(!fs3->hasLocally(fs1->getRoot()));

        //share the root
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/" };
            path_request.make_preferred();
            auto future = fsint1->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(!fut_res.is_file);
            REQUIRE(fut_res.error_code == 0);
            future = fsint3->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            fut_res = future.get();
            REQUIRE(!fut_res.is_file);
            REQUIRE(fut_res.error_code == 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        //SynchroDb::test_trigger = true;
        REQUIRE(fs2->hasLocally(fs1->getRoot()));
        REQUIRE(fs1->hasLocally(fs1->getRoot()));
        REQUIRE(fs3->hasLocally(fs1->getRoot()));

        // ==STEP== serv1 create dir
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

        //  +5seconds to allow synchrodb to send invalidation again (do it after the fsint1, as it refresh the duration if too far away)
        global_clock->offset += (1000 * 5 + 10);

        REQUIRE((bool)fs1_dir1);
        REQUIRE(fs1->hasLocally(fs1_dir1->getId()));
        REQUIRE(!fs2->hasLocally(fs1_dir1->getId()));
        REQUIRE(!fs3->hasLocally(fs1_dir1->getId()));
        // ==STEP==  B & C is notified of a change
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // the invalidation is transmitted after the future answer
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(!synch3->isInvalidated(3));
        REQUIRE(!synch1->get_test_wait_current_invalidation().empty());
        DateTime current = serv1->getCurrentTime();
        serv1->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));// have to wait a bit the network process to transmit information
        serv2->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));// have to wait a bit the network process to transmit information
        serv3->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));// have to wait a bit the network process to transmit information
        // now serv2 should have the root as "invalidated"
        REQUIRE(synch1->get_test_wait_current_invalidation().empty());
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(synch2->isInvalidated(3));
        REQUIRE(synch3->isInvalidated(3));

        // ==STEP== +1minute
        global_clock->offset += (1000 * 60 + 1000);

        // ==STEP== B update -> no more changes possible
        serv2->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));// have to wait a bit the network process to transmit information
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(synch3->isInvalidated(3));
        REQUIRE(fs1->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs2->loadDirectory(3)->getCurrent().size() == 0);
        REQUIRE(fs3->loadDirectory(3)->getCurrent().size() == 0);

        // ==STEP== C ask B for changes
        // -> B answer with the same boring thing 
        // -> C accept the boring thing, removing the invalidation
        { // using the external interface for the synchronization to occur
            std::filesystem::path path_request{ "/" };
            path_request.make_preferred();
            auto future = fsint3->get(path_request);
            future.wait();
            REQUIRE(future.valid());
            auto fut_res = future.get();
            REQUIRE(!fut_res.is_file);
            REQUIRE(fut_res.error_code == 0);
           FsDirPtr rootdir = FsElt::toDirectory(fut_res.object);
           REQUIRE((bool)rootdir);
           REQUIRE(rootdir->getCurrent().size() == 0);
        }
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(!synch3->isInvalidated(3));
        REQUIRE(fs1->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs2->loadDirectory(3)->getCurrent().size() == 0);
        REQUIRE(fs3->loadDirectory(3)->getCurrent().size() == 0);

        
        // ==STEP== A update -> it send the changes by itself
        // -> B receive the changes.
        // -> C is notified of the changes?
        serv1->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));// have to wait a bit the network process to transmit information
        serv2->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(fs1->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs2->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs3->loadDirectory(3)->getCurrent().size() == 0);
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(!synch3->isInvalidated(3));
        
        
        // ==STEP== time minutes pass
        global_clock->offset += (1000 * 60 + 1000);
        
        // ==STEP== B update -> no change send (not its changes, to avoid exponential loop flooding)
        serv2->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(fs1->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs2->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs3->loadDirectory(3)->getCurrent().size() == 0);
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(synch3->isInvalidated(3));
        
        // ==STEP== C update -> request the changes.
        // -> B correctly send the new changes, A B and C aare now in synch
        serv3->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(fs1->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs2->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(fs3->loadDirectory(3)->getCurrent().size() == 1);
        REQUIRE(!synch1->isInvalidated(3));
        REQUIRE(!synch2->isInvalidated(3));
        REQUIRE(!synch3->isInvalidated(3));
        

    }

    //TODO: scenario 
    // ABC in synch
    // C disconnect
    // A create a DIR
    // 5 minutes later
    // B update -> get dir info from A
    // C reconnect to B -> request the current state -> see it has many invalidation -> request evrything new
    // A B C in synch


    //TODO: scenario - check merging changes on directories-
    // -- check mergeable change in same directory --
    // ==STEP== ABC in synch (/dir1/fso1 & /dir1/fso3 & /dir1/fso)
    // ==STEP== A add fs1 & fs1bis in dir1 and delete fso1 & fso
    // ==STEP== C add fs3 in dir1 and delete fso3 & fso
    // ==STEP== updates: B is notified of the changes, and also both A and C
    // ==STEP== B fetch change -> get both from A & C
    // -> B merge the commit without any problems, with a new state with fs1 and fs3.
    // -> B emit a change notification to A & C
    // ==STEP== A & C fetch change -> B give them the new state
    // -> A B C in synch
    // -- check unmergeable changes in same directory --
    // ==STEP== A add fs in dir1 and modify fs1 & fs3
    // ==STEP== C add fs in dir1 and delete fs1, and modify fs3
    // ==STEP== updates: B is notified of the changes, and also both A and C
    // ==STEP== B fetch change -> get both from A & C
    // -> B merge the commit without problems, keep fs fs3 from C, delete fs1 from C as C commit(s) is more recent
    // -> B don't emit a change notification as it doesn't change the C commit
    // -> only A has invalidation
    // ==STEP== A fetch change -> change its state to the C one
    // -> A keep its old state in the history.
    // -> A B C in synch
    // -- check partially mergeable changes in same directory --
    // ==STEP== A add fsbis in dir1 and modify fs3 & fs and delete fs1bis
    // ==STEP== C modify fs3
    // ==STEP== updates: B is notified of the changes, and also both A and C
    // ==STEP== B fetch change -> get both from A & C
    // -> B merge the commit without problems, keep fs3 from C, and keep fsbis & fs & !fs1bis from A
    // -> B emit a change notification to A & C
    // ==STEP== A & C fetch change -> B give them the new state
    // -> A B C in synch


    //TODO: scenario - check merging changes on files-
    // only last version is kept, whatever.
    //TODO: notify the external interface that a remote change has erased our modifications.

}
