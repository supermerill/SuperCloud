
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

namespace supercloud::test::synchtree {
    typedef std::shared_ptr<SynchTreeMessageManager> MsgManaPtr;

    FsID newid() {
        return FsElt::createId(rand_u16() % 1 == 0 ? FsType::FILE : FsType::DIRECTORY, rand_u63(), ComputerId(rand_u63() & COMPUTER_ID_MASK));
    }


    typedef std::shared_ptr<PhysicalServer> ServPtr;
    typedef std::shared_ptr<FakeLocalNetwork> NetPtr;
    typedef std::shared_ptr<FsStorage> FsStoragePtr;
    typedef std::shared_ptr<SynchroDb> SynchPtr;

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
    std::tuple< FsStoragePtr, SynchPtr, MsgManaPtr> addFileSystem(ServPtr serv) {
        std::shared_ptr<MyClock> clock = std::make_shared<MyClock>(serv);
        FsStoragePtr fs = FsStoragePtr{ new FsStorageInMemory{ serv->getComputerId(), clock } };
        //create synch
        SynchPtr synch = SynchroDb::create();
        synch->init(fs, serv);
        //synch->launch(); //create message manager
        //create chunk message manager
        MsgManaPtr chunk_mana = SynchTreeMessageManager::create(serv, fs, synch);
        return std::tuple< FsStoragePtr, SynchPtr, MsgManaPtr>{fs, synch, chunk_mana};
    }

    // for connecting to an existing cluster
    void addEntryPoint(InMemoryParameters& param, const std::string& ip, uint16_t port) {
        param.setString("PeerIp", ip);
        param.setInt("PeerPort", port);
        param.setBool("FirstConnection", true);
    }
	SCENARIO("Test SynchTreeMessageManager message write/read") {

        std::shared_ptr<PhysicalServer> serv = PhysicalServer::createForTests();
        ServerConnectionState m_state;
        MsgManaPtr messageManager = SynchTreeMessageManager::create(serv, {}, {});

        GIVEN("GET_TREE") {
            SynchTreeMessageManager::TreeRequest data_1{ {newid(),newid(),newid()}, rand_u63()+1, rand_u63() + 1, get_current_time_milis() };
            ByteBuff buff_1 = messageManager->writeTreeRequestMessage(data_1);
            SynchTreeMessageManager::TreeRequest data_2 = messageManager->readTreeRequestMessage(buff_1);
            ByteBuff buff_2 = messageManager->writeTreeRequestMessage(data_2);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());

            REQUIRE(data_1.roots == data_2.roots);
            REQUIRE(data_1.depth == data_2.depth);
            REQUIRE(data_1.depth != 0);
            REQUIRE(data_1.last_fetch_time == data_2.last_fetch_time);
            REQUIRE(data_1.last_fetch_time != 0);
            REQUIRE(data_1.last_commit_received == data_2.last_commit_received);
            REQUIRE(data_1.last_commit_received != 0);
        }


        GIVEN("SEND_TREE") {
            /*
            * 

        struct TreeAnswerElt {
            FsID elt_id;
            uint16_t elt_depth;
            size_t elt_size;
            FsID last_commit_id;
            DateTime last_commit_time;
        };
        struct TreeAnswerEltChange : TreeAnswerElt {
            std::vector<FsID> state;
        };
        struct TreeAnswerEltDeleted : TreeAnswerElt {
            FsID renamed_to; //can be 0 if just deleted
        };
            */
            SynchTreeMessageManager::TreeAnswer data_1{ ComputerId(rand_u63() & COMPUTER_ID_MASK), get_current_time_milis(), {}, {}, {} };
            for (uint16_t i = 0; i < uint16_t(100); i++) {
                if (rand_u8() % 4 == 1) {
                    //(FsID id, uint16_t depth, size_t size, DateTime date, std::string name, CUGA puga, FsID parent, uint32_t group, std::vector<FsID> state)
                    data_1.created.push_back(SynchTreeMessageManager::FsObjectTreeAnswerPtr{ new SynchTreeMessageManager::FsObjectTreeAnswer{
                        newid(), rand_u16(), rand_u63(), get_current_time_milis(), "test", rand_u16(), newid(), uint32_t(rand_u63()), std::vector<FsID>{ newid(), newid() }} });
                    if (rand_u8() % 2 == 1) {
                        data_1.created.back()->setCommit(rand_u63(), rand_u63());
                    }
                }
                if (rand_u8() % 4 == 2) {
                    /*
            FsID elt_id;
            uint16_t elt_depth;
            size_t elt_size;
            FsID last_commit_id;
            DateTime last_commit_time;
            std::vector<FsID> state;*/
                    data_1.modified.push_back(SynchTreeMessageManager::TreeAnswerEltChange{ 
                        newid(), rand_u16(), rand_u63(), rand_u63(), get_current_time_milis(), std::vector<FsID>{ newid() ,newid() ,newid() ,newid() } });
                }
                if (rand_u8() % 4 == 3) {
                    /*
            sID elt_id;
            uint16_t elt_depth;
            size_t elt_size;
            FsID last_commit_id;
            DateTime last_commit_time;
            FsID renamed_to;*/
                    data_1.deleted.push_back(SynchTreeMessageManager::TreeAnswerEltDeleted{ 
                        newid(), rand_u16(), rand_u63(), rand_u63(), get_current_time_milis(), newid() });
                }
            }
            ByteBuff buff_1 = messageManager->writeTreeAnswerMessage(data_1);
            SynchTreeMessageManager::TreeAnswer data_2 = messageManager->readTreeAnswerMessage(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeTreeAnswerMessage(data_2);

            REQUIRE(data_1.from == data_2.from);
            REQUIRE(data_1.answer_time == data_2.answer_time);
            REQUIRE(data_1.created.size() == data_2.created.size());
            for (int i = 0; i < data_1.created.size(); ++i) {
                REQUIRE(*data_1.created[i] == *data_2.created[i]);
            }
            REQUIRE(data_1.modified == data_2.modified);
            REQUIRE(data_1.deleted == data_2.deleted);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());
        }

	}


    SCENARIO("Test SynchTreeMessageManager getalll with fake network") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        std::map < std::string, NetPtr > fakeNetworks;
        std::string last_listen_ip = "";
        uint16_t last_listen_port = 0;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        ServPtr serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1", 4242);
        auto [fs1, synch1, chunkmana1] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        addEntryPoint(param_serv2, "192.168.0.1", 4242);
        ServPtr serv2 = createPeerFakeNet(param_serv2, net_192_168_0, "192.168.0.2");
        auto [fs2, synch2, chunkmana2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        //init fs
        //we don't init the root in the fs1, so we can test the "synch from uninit peer"

        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2);
        serv2->connect();

        //wait connection
        DateTime waiting1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting2 = waiter2->startWait().waitConnection(std::chrono::milliseconds(10000));
        REQUIRE(waiting1 > 0);
        REQUIRE(waiting2 > 0);
        REQUIRE(serv1->getState().isConnected());
        REQUIRE(serv2->getState().isConnected());
        REQUIRE(serv1->getPeer()->isConnected());
        REQUIRE(serv2->getPeer()->isConnected());

        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());

        //now create  dirs & files in the serv2
        FsDirPtr fs2_root = ((FsStorageInMemory*)fs2.get())->createNewRoot();
        FsDirPtr fs2_dir1 = fs2->createNewDirectory(fs2->loadDirectory(fs2->getRoot()), "dir1", std::vector<FsID>{}, CUGA_7777);
        FsDirPtr fs2_dir11 = fs2->createNewDirectory(fs2_dir1, "dir11", std::vector<FsID>{}, CUGA_7777);
        FsDirPtr fs2_dir111 = fs2->createNewDirectory(fs2_dir11, "dir111", std::vector<FsID>{}, CUGA_7777);
        FsFilePtr fs2_fic2 = fs2->createNewFile(fs2->loadDirectory(fs2->getRoot()), "fic2", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic2, "fic2") ;
        REQUIRE(fs2_fic2->getCurrent().size() == 1);
        FsFilePtr fs2_fic12 = fs2->createNewFile(fs2_dir1, "fic12", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic12, ("fic12"));
        FsFilePtr fs2_fic112 = fs2->createNewFile(fs2_dir11, "fic112", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic112, ("fic112"));
        FsFilePtr fs2_fic1112 = fs2->createNewFile(fs2_dir111, "fic1112", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic1112, ("fic1112"));

        REQUIRE(!fs1->hasLocally(fs2_dir11->getId()));
        REQUIRE(fs2->hasLocally(fs2_dir11->getId()));
        REQUIRE(!fs1->hasLocally(fs2_fic2->getId()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getId()));
        REQUIRE(!fs1->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));

        //now connected, test a simple tree fetch
        auto future_tree_answer = chunkmana1->fetchTree(fs1->getRoot());
        std::future_status status = future_tree_answer.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_tree_answer.valid());
        SynchTreeMessageManager::TreeAnswerPtr treeChanges = future_tree_answer.get();
        REQUIRE(treeChanges);
        REQUIRE(!treeChanges->created.empty());
        REQUIRE(treeChanges->modified.empty());
        REQUIRE(treeChanges->deleted.empty());

        REQUIRE(fs1->hasLocally(fs2_dir11->getId()));
        REQUIRE(fs2->hasLocally(fs2_dir11->getId()));
        REQUIRE(fs1->hasLocally(fs2_fic2->getId()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getId()));

        REQUIRE(fs1->checkFilesystem());
        REQUIRE(fs2->checkFilesystem());
    }

    SCENARIO("Test SynchTreeMessageManager get sub-tree only (after first get) with fake network") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        std::map < std::string, NetPtr > fakeNetworks;
        std::string last_listen_ip = "";
        uint16_t last_listen_port = 0;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        ServPtr serv1 = createPeerFakeNet(param_serv1, net_192_168_0, "192.168.0.1", 4242);
        auto [fs1, synch1, chunkmana1] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        addEntryPoint(param_serv2, "192.168.0.1", 4242);
        ServPtr serv2 = createPeerFakeNet(param_serv2, net_192_168_0, "192.168.0.2");
        auto [fs2, synch2, chunkmana2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        //init fs
        //we don't init the root in the fs1, so we can test the "synch from uninit peer"

        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2);
        serv2->connect();

        //wait connection
        DateTime waiting1 = waiter1->startWait().waitConnection(std::chrono::milliseconds(10000));
        DateTime waiting2 = waiter2->startWait().waitConnection(std::chrono::milliseconds(10000));
        REQUIRE(waiting1 > 0);
        REQUIRE(waiting2 > 0);
        REQUIRE(serv1->getState().isConnected());
        REQUIRE(serv2->getState().isConnected());
        REQUIRE(serv1->getPeer()->isConnected());
        REQUIRE(serv2->getPeer()->isConnected());

        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());

        //now create  dirs & files in the serv2
        FsDirPtr fs2_root = ((FsStorageInMemory*)fs2.get())->createNewRoot();
        FsDirPtr fs2_dir1 = fs2->createNewDirectory(fs2->loadDirectory(fs2->getRoot()), "dir1", std::vector<FsID>{}, CUGA_7777);

        REQUIRE(!fs1->hasLocally(fs2_dir1->getId()));
        REQUIRE(fs2->hasLocally(fs2_dir1->getId()));

        //now connected, test a simple tree fetch
        auto future_tree_answer = chunkmana1->fetchTree(fs1->getRoot());
        std::future_status status = future_tree_answer.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_tree_answer.valid());
        SynchTreeMessageManager::TreeAnswerPtr treeChanges = future_tree_answer.get();
        REQUIRE(treeChanges);
        REQUIRE(!treeChanges->created.empty());
        REQUIRE(treeChanges->modified.empty());
        REQUIRE(treeChanges->deleted.empty());

        REQUIRE(fs1->hasLocally(fs2_dir1->getId()));
        REQUIRE(fs2->hasLocally(fs2_dir1->getId()));

        //create more directories & files
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        FsDirPtr fs2_dir11 = fs2->createNewDirectory(fs2_dir1, "dir11", std::vector<FsID>{}, CUGA_7777);
        FsDirPtr fs2_dir111 = fs2->createNewDirectory(fs2_dir11, "dir111", std::vector<FsID>{}, CUGA_7777);
        FsFilePtr fs2_fic2 = fs2->createNewFile(fs2->loadDirectory(fs2->getRoot()), "fic2", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic2, ("fic2"));
        REQUIRE(fs2_fic2->getCurrent().size() == 1);
        FsFilePtr fs2_fic12 = fs2->createNewFile(fs2_dir1, "fic12", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic12, ("fic12"));
        FsFilePtr fs2_fic112 = fs2->createNewFile(fs2_dir11, "fic112", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic112, ("fic112"));
        FsFilePtr fs2_fic1112 = fs2->createNewFile(fs2_dir111, "fic1112", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic1112, ("fic1112"));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        //now connected, test a sub-tree fetch
        future_tree_answer = chunkmana1->fetchTree(fs2_dir1->getId());
        status = future_tree_answer.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_tree_answer.valid());
        treeChanges = future_tree_answer.get();
        REQUIRE(treeChanges);
        REQUIRE(!treeChanges->created.empty());
        REQUIRE(!treeChanges->modified.empty());
        REQUIRE(treeChanges->deleted.empty());

        REQUIRE(fs1->hasLocally(fs2_dir11->getId()));
        REQUIRE(fs2->hasLocally(fs2_dir11->getId()));
        REQUIRE(!fs1->hasLocally(fs2_fic2->getId()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getId()));

        REQUIRE(fs1->checkFilesystem());
        REQUIRE(fs2->checkFilesystem());
    }
}
