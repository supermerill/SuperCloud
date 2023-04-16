
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
#include "synch/ExchangeChunkMessageManager.hpp"
#include "synch/SynchroDb.hpp"

namespace supercloud::test::syncchunk {
    typedef std::shared_ptr<ExchangeChunkMessageManager> MsgManaPtr;

    FsID newid() {
        return FsElt::createId(FsType::CHUNK, rand_u63(), ComputerId(rand_u63() & COMPUTER_ID_MASK));
    }

    typedef std::shared_ptr<PhysicalServer> ServPtr;
    typedef std::shared_ptr<FakeLocalNetwork> NetPtr;
    typedef std::shared_ptr<FsStorage> FsStoragePtr;
    typedef std::shared_ptr<SynchroDb> SynchPtr;
    typedef std::shared_ptr<ExchangeChunkMessageManager> ChunkManaPtr;

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
    public:
        virtual DateTime getCurrentTime() override { return this->get_current_time_milis(); }
    };
    std::tuple< FsStoragePtr, SynchPtr, ChunkManaPtr> addFileSystem(ServPtr serv) {
        FsStoragePtr fs = FsStoragePtr{ new FsStorageInMemory{ serv->getComputerId(), serv } };
        //create synch
        SynchPtr synch = SynchroDb::create();
        synch->init(fs, serv);
        //sych->launch();
        //create chunk message manager
        ChunkManaPtr chunk_mana = ExchangeChunkMessageManager::create(serv, fs, synch);
        return std::tuple< FsStoragePtr, SynchPtr, ChunkManaPtr>{fs, synch, chunk_mana};
    }

    // for connecting to an existing cluster
    void addEntryPoint(InMemoryParameters& param, const std::string& ip, uint16_t port) {
        param.setString("PeerIp", ip);
        param.setInt("PeerPort", port);
        param.setBool("FirstConnection", true);
    }


    SCENARIO("Test ExchangeChunkMessageManager message write/read") {

        std::shared_ptr<PhysicalServer> serv = PhysicalServer::createForTests();
        ServerConnectionState m_state;
        MsgManaPtr messageManager = ExchangeChunkMessageManager::create(serv, {}, {});

        GIVEN("GET_CHUNK_AVAILABILITY") {
            ExchangeChunkMessageManager::ChunkAvailabilityRequest data_1{ newid(), rand_u8() % 2 == 1 };
            ByteBuff buff_1 = messageManager->writeChunkAvailabilityRequest(data_1);
            ExchangeChunkMessageManager::ChunkAvailabilityRequest data_2 = messageManager->readChunkAvailabilityRequest(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeChunkAvailabilityRequest(data_2);

            REQUIRE(data_1.elt_root == data_2.elt_root);
            REQUIRE(data_1.only_you == data_2.only_you);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());

        }


        GIVEN("SEND_CHUNK_AVAILABILITY") {
            //std::unordered_map<FsID, std::vector<ChunkAvailabilityComputer>> availability;
            /*		struct ChunkAvailabilityComputer {
            ComputerId cid; // who?
            bool has_chunk; // false if it's a 'deletion' (deletion storage has a timeout)
            Date since; //when this chunk has been added/deleted from the computer
            Date last_fetched; //when the informatio has been refreshed in the peer that send this ChunkAvailabilityChunk .*/
            ExchangeChunkMessageManager::ChunkAvailabilityAnswer data_1;
            for (int i = 0; i < 100; i++) {
                std::vector< ExchangeChunkMessageManager::ChunkAvailabilityComputer>& vec = data_1.availability[newid()];
                size_t size_vec = rand_u8();
                for (int j = 0; j < size_vec; j++) {
                    vec.push_back(ExchangeChunkMessageManager::ChunkAvailabilityComputer{ ComputerId(rand_u63() & COMPUTER_ID_MASK), rand_u8() % 2 == 1, Date(rand_u63()), Date(rand_u63()) });
                }
            }
            ByteBuff buff_1 = messageManager->writeChunkAvailabilityAnswer(data_1);
            ExchangeChunkMessageManager::ChunkAvailabilityAnswer data_2 = messageManager->readChunkAvailabilityAnswer(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeChunkAvailabilityAnswer(data_2);

            REQUIRE(data_1.availability == data_2.availability);

            //can't compare, as it's unordered_map
            //auto v1 = buff_1.rewind().getAll();
            //auto v2 = buff_2.rewind().getAll();
            //for (size_t i = 0; i < v1.size(); i++) {
            //    assert(v1[i] == v2[i]);
            //}
            //REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());

        }

        GIVEN("GET_CHUNK_REACHABLE") {
            ExchangeChunkMessageManager::ChunkReachableRequest data_1{ newid() };
            messageManager->registerChunkReachableRequest(data_1.chunk_or_file_id);
            ByteBuff buff_1 = messageManager->writeChunkReachableRequest(data_1);
            ExchangeChunkMessageManager::ChunkReachableRequest data_2 = messageManager->readChunkReachableRequest(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeChunkReachableRequest(data_2);

            REQUIRE(data_1.chunk_or_file_id == data_2.chunk_or_file_id);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());
        }

        GIVEN("SEND_CHUNK_REACHABLE") {
            //FsID chunk_or_file_id_request; // the  request file/chunk
            //std::unordered_map<FsID, ChunkReachablePath> paths; // best path for each chunk
            /*		struct ChunkReachablePath {
            ComputerId cid; // who has it
            uint8_t hops; // how many gateways
            uint8_t difficulty;*/
            ExchangeChunkMessageManager::ChunkReachableAnswer data_1;
            data_1.chunk_or_file_id_request = newid();
            for (int i = 0; i < 100; i++) {
                data_1.paths[newid()] = ExchangeChunkMessageManager::ChunkReachablePath{ {ComputerId(rand_u63() & COMPUTER_ID_MASK),ComputerId(rand_u63() & COMPUTER_ID_MASK)}, rand_u8() };
            }
            ByteBuff buff_1 = messageManager->writeChunkReachableAnswer(data_1);
            ExchangeChunkMessageManager::ChunkReachableAnswer data_2 = messageManager->readChunkReachableAnswer(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeChunkReachableAnswer(data_2);

            REQUIRE(data_1.chunk_or_file_id_request == data_2.chunk_or_file_id_request);
            REQUIRE(data_1.paths == data_2.paths);

            //uncomparable, as there is an unordered_map
            //REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());
        }

        GIVEN("GET_SINGLE_CHUNK") {
            ExchangeChunkMessageManager::SingleChunkRequest data_1{ newid() };
            messageManager->registerSingleChunkRequest(data_1.chunk_id);
            ByteBuff buff_1 = messageManager->writeSingleChunkRequest(data_1);
            ExchangeChunkMessageManager::SingleChunkRequest data_2 = messageManager->readSingleChunkRequest(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeSingleChunkRequest(data_2);

            REQUIRE(data_1.chunk_id == data_2.chunk_id);
            REQUIRE(data_1.need_gateway == data_2.need_gateway);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());
        }

        GIVEN("SEND_SINGLE_CHUNK") {
            // FsID chunk_id_requested; // the  request file/chunk
            // FsChunkTempPtr chunk; // std::optional, but easier to use
            // uint8_t hash_algo; // 0 = naive
            /*FsChunkTempPtr(FsID id, DateTime date, uint64_t hash,
				ByteBuff&& partial_data, size_t partial_offset, uint64_t partial_hash,
				size_t file_offset, size_t real_size)*/
            ExchangeChunkMessageManager::SingleChunkAnswer data_1;
            data_1.chunk_id_requested = newid();
            data_1.hash_algo = rand_u8()%1==1;
            ByteBuff data;
            size_t size = rand_u16();
            for (size_t i = 0; i < size; i++) {
                data.put(rand_u8());
            }
            data.flip();
            data_1.chunk = ExchangeChunkMessageManager::FsChunkTempPtr{ new ExchangeChunkMessageManager::FsChunkTempElt{data_1.chunk_id_requested, DateTime(rand_u63()), rand_u63(),
                std::move(data), 0, rand_u63(), rand_u63(), size} };
            ByteBuff buff_1 = messageManager->writeSingleChunkAnswer(data_1);
            ExchangeChunkMessageManager::SingleChunkAnswer data_2 = messageManager->readSingleChunkAnswer(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeSingleChunkAnswer(data_2);

            REQUIRE(data_1.chunk_id_requested == data_2.chunk_id_requested);
            REQUIRE(data_1.hash_algo == data_2.hash_algo);
            REQUIRE(data_1.chunk);
            REQUIRE(data_2.chunk);
            REQUIRE(*data_1.chunk == *data_2.chunk);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());

        }

    }

    SCENARIO("Test ExchangeChunkMessageManager AVAILABILITY with fake network") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_2 = NetPtr{ new FakeLocalNetwork{"192.168.2"} };
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
        addChunkToFile(fs2, fs2_fic2, ("fic2"));
        REQUIRE(fs2_fic2->getCurrent().size() == 1);
        FsFilePtr fs2_fic12 = fs2->createNewFile(fs2_dir1, "fic12", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic12, ("fic12"));
        FsFilePtr fs2_fic112 = fs2->createNewFile(fs2_dir11, "fic112", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic112, ("fic112"));
        FsFilePtr fs2_fic1112 = fs2->createNewFile(fs2_dir111, "fic1112", {}, CUGA_7777);
        addChunkToFile(fs2, fs2_fic1112, ("fic1112"));

        REQUIRE(!fs1->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));

        //now connected, test some simple chunk fetch
        auto future_chunk = chunkmana1->fetchChunk(fs2_fic2->getCurrent().front());
        std::future_status status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::shared_ptr<ExchangeChunkMessageManager::FsChunkTempElt> chunkptr = future_chunk.get();
        REQUIRE(chunkptr);
        std::string file2_content = toString(readAll(*chunkptr));
        std::cout << "read chunk: " << file2_content << "\n";
        REQUIRE(file2_content == std::string("fic2"));

        future_chunk = chunkmana1->fetchChunk(fs2_fic12->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file12_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file12_content == std::string("fic12"));

        future_chunk = chunkmana1->fetchChunk(fs2_fic112->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file112_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file112_content == std::string("fic112"));

        future_chunk = chunkmana1->fetchChunk(fs2_fic1112->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file1112_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file1112_content == std::string("fic1112"));

        //REQUIRE(fs1->hasLocally(fs2_fic2->getCurrent().front()));
        //REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));
    }


    SCENARIO("Test ExchangeChunkMessageManager AVAILABILITY with fake network and a gateway") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_2 = NetPtr{ new FakeLocalNetwork{"192.168.2"} }; //TODO use this one
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

        InMemoryParameters param_serv3 = createNewConfiguration();
        addEntryPoint(param_serv3, "192.168.0.1", 4242);
        ServPtr serv3 = createPeerFakeNet(param_serv3, net_192_168_0, "192.168.0.4");
        auto [fs3, synch3, chunkmana3] = addFileSystem(serv3);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));


        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2);
        std::shared_ptr<WaitConnection> waiter3 = WaitConnection::create(serv3);
        serv2->connect();
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
        REQUIRE(serv1->getPeersCopy().size() == 2);
        REQUIRE(serv2->getPeersCopy().size() == 1);
        REQUIRE(serv3->getPeersCopy().size() == 1);


        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());

        //now create  dirs & files in the serv2
        FsDirPtr fs2_root = ((FsStorageInMemory*)fs2.get())->createNewRoot();
        FsDirPtr fs2_dir1 = fs2->createNewDirectory(fs2->loadDirectory(fs2->getRoot()), "dir1", std::vector<FsID>{}, CUGA_7777);
        FsDirPtr fs2_dir11 = fs2->createNewDirectory(fs2_dir1, "dir11", std::vector<FsID> {}, CUGA_7777);
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

        REQUIRE(!fs1->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(!fs3->hasLocally(fs2_fic2->getCurrent().front()));

        //now connected, test some simple chunk fetch
        auto future_chunk = chunkmana3->fetchChunk(fs2_fic2->getCurrent().front());
        std::future_status status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::shared_ptr<ExchangeChunkMessageManager::FsChunkTempElt> chunkptr = future_chunk.get();
        REQUIRE(chunkptr);
        std::string file2_content = toString(readAll(*chunkptr));
        std::cout << "read chunk: " << file2_content << "\n";
        REQUIRE(file2_content == std::string("fic2"));

        future_chunk = chunkmana3->fetchChunk(fs2_fic12->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file12_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file12_content == std::string("fic12"));

        future_chunk = chunkmana3->fetchChunk(fs2_fic112->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file112_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file112_content == std::string("fic112"));

        future_chunk = chunkmana3->fetchChunk(fs2_fic1112->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file1112_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file1112_content == std::string("fic1112"));


        //REQUIRE(fs1->hasLocally(fs2_fic2->getCurrent().front()));
        //REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));
        //REQUIRE(fs3->hasLocally(fs2_fic2->getCurrent().front()));

    }

    SCENARIO("Test ExchangeChunkMessageManager AVAILABILITY with fake network and two gateway") {
        ServerSocket::factory.reset(new FakeSocketFactory());
        NetPtr net_192_168_0 = NetPtr{ new FakeLocalNetwork{"192.168.0"} };
        NetPtr net_192_168_42 = NetPtr{ new FakeLocalNetwork{"192.168.42"} }; //TODO use these ones
        NetPtr net_192_168_44 = NetPtr{ new FakeLocalNetwork{"192.168.44"} };
        std::map < std::string, NetPtr > fakeNetworks;
        std::string last_listen_ip = "";
        uint16_t last_listen_port = 0;

        //create 2 instance, network + fs + chunk manager (and synch object but not active)
        InMemoryParameters param_serv1 = createNewConfiguration();
        ServPtr serv1 = createPeerFakeNets(param_serv1, { net_192_168_0, net_192_168_42 }, "0.0.0.1", 4242);
        auto [fs1, synch1, chunkmana1] = addFileSystem(serv1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv2 = createNewConfiguration();
        addEntryPoint(param_serv2, "192.168.0.1", 4242);
        ServPtr serv2 = createPeerFakeNet(param_serv2, net_192_168_0, "192.168.0.2");
        auto [fs2, synch2, chunkmana2] = addFileSystem(serv2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv3 = createNewConfiguration();
        addEntryPoint(param_serv3, "192.168.42.1", 4242);
        ServPtr serv3 = createPeerFakeNets(param_serv3, { net_192_168_42, net_192_168_44 }, "0.0.0.3", 4242);
        auto [fs3, synch3, chunkmana3] = addFileSystem(serv3);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        InMemoryParameters param_serv4 = createNewConfiguration();
        addEntryPoint(param_serv4, "192.168.44.3", 4242);
        ServPtr serv4 = createPeerFakeNet(param_serv4, net_192_168_44, "192.168.44.4");
        auto [fs4, synch4, chunkmana4] = addFileSystem(serv4);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // 2 -> 1 <-> 3 <- 4


        // connect both computer
        std::shared_ptr<WaitConnection> waiter1 = WaitConnection::create(serv1, 2);
        std::shared_ptr<WaitConnection> waiter2 = WaitConnection::create(serv2);
        std::shared_ptr<WaitConnection> waiter3 = WaitConnection::create(serv3, 2);
        std::shared_ptr<WaitConnection> waiter4 = WaitConnection::create(serv4);
        serv2->connect();
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
        REQUIRE(serv1->getPeersCopy().size() == 2);
        REQUIRE(serv2->getPeersCopy().size() == 1);
        REQUIRE(serv3->getPeersCopy().size() == 2);
        REQUIRE(serv4->getPeersCopy().size() == 1);

        //update computerId;
        fs1->setMyComputerId(serv1->getComputerId());
        fs2->setMyComputerId(serv2->getComputerId());

        //now create  dirs & files in the serv2
        FsDirPtr fs2_root = ((FsStorageInMemory*)fs2.get())->createNewRoot();
        FsDirPtr fs2_dir1 = fs2->createNewDirectory(fs2->loadDirectory(fs2->getRoot()), "dir1", std::vector<FsID>{}, CUGA_7777);
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

        REQUIRE(!fs1->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(!fs3->hasLocally(fs2_fic2->getCurrent().front()));
        REQUIRE(!fs4->hasLocally(fs2_fic2->getCurrent().front()));

        //now connected, test some simple chunk fetch
        auto future_chunk = chunkmana4->fetchChunk(fs2_fic2->getCurrent().front());
        std::future_status status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::shared_ptr<ExchangeChunkMessageManager::FsChunkTempElt> chunkptr = future_chunk.get();
        REQUIRE(chunkptr);
        std::string file2_content = toString(readAll(*chunkptr));
        std::cout << "read chunk: " << file2_content << "\n";
        REQUIRE(file2_content == std::string("fic2"));

        future_chunk = chunkmana4->fetchChunk(fs2_fic12->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file12_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file12_content == std::string("fic12"));

        future_chunk = chunkmana4->fetchChunk(fs2_fic112->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file112_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file112_content == std::string("fic112"));

        future_chunk = chunkmana4->fetchChunk(fs2_fic1112->getCurrent().front());
        status = future_chunk.wait_for(std::chrono::milliseconds(10000));
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future_chunk.valid());
        std::string file1112_content = toString(readAll(*future_chunk.get()));
        REQUIRE(file1112_content == std::string("fic1112"));


        //REQUIRE(fs1->hasLocally(fs2_fic2->getCurrent().front()));
        //REQUIRE(fs2->hasLocally(fs2_fic2->getCurrent().front()));
        //REQUIRE(fs3->hasLocally(fs2_fic2->getCurrent().front()));

    }
}
