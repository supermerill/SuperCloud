#pragma once

#include <cstdint>
#include <filesystem>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>

#include "ClusterManager.hpp"
#include "../utils/ByteBuff.hpp"
#include "Peer.hpp"
#include "ConnectionMessageManager.hpp"

using boost::asio::ip::tcp;

namespace supercloud {

    class ServerIdDb;
    class FileSystemManager;

    /**
     *
     * ids: <br>
     * * clusterId (uint64_t) -> id of the cluster, ie the virtual drive. It's public<br>
     * * peerId (uint64_t)(serverId/connectionId/senderId) -> id of the program that is ending and recieving stuff ont he network. it's the working id, to identify the current connection.<br>
     * * computerId (uint16_t) -> id of an instance of the network. it's a verified public/private AES verifiedaprotected id to identify a peer/computer that can grab data and maybe make modifications.<br>
     *
     * <p>
     * note: this part is made from memory 1 year after coding, so take it with a grain of salt<br>
     * When connecting, we verify the clusterId, it's the public id for the network, and so the virtual drive.<br>
     * then the newly connected receive the list of conencted peers on the network and ask all of them their list of peers.<br>
     * With this list, he can now choose his peerId.<br>
     * If there are a peerId collision, this is resolved. note: maybe we should add protection for already-connected peers? (if same id as a connected peer -> disconnect it brutally)
     * Then we need to create a secure connection, so we exchange public keys.<br>
     * To verify, we send a uint16_t message back but public-encrytped.<br>
     * Then, we can speak in RSA.<br>
     * We exchange our computerId.<br>
     * We verify if the publickey/computerID pair is ok, if we already seen the computer.<br>
     *
     *
     * TODO: add cluster priv/pub key to be sure if somone has created an other cluster with the same id/password, he can't connect to our.
     *
     *
     * @author centai
     *
     */
    class PhysicalServer : public ConnectionMessageManagerServerInterface {
    public:

        enum ServerConnectionState : uint8_t {
            JUST_BORN,
            CONNECTING,
            CONNECTED,
            DISCONNECTED,

            SOLO,
            HAS_FRIENDS
        };

    protected:
        uint64_t myPeerId;

        ServerConnectionState myInformalState = ServerConnectionState::JUST_BORN;

        // should be inetAddress ut it's implier with this to test in 1 pc.
        // private Map<PeerKey ,Peer> peers = new HashMap<>();
        // private Semaphore peersSemaphore = new Semaphore(1);
        std::recursive_mutex peers_mutex;
        PeerList peers; // don't access this outside of semaphore. Use getters instead.
        std::unique_ptr<tcp::acceptor> mySocket; // server socket
        //std::unique_ptr<std::thread> socketListener; //just a pointer to the detached thread handle.
        //std::unique_ptr<std::thread> updaterThread;
        bool hasSocketListener = false;
        bool hasUpdaterThread = false;

        //pointer to other manager
        std::shared_ptr<FileSystemManager> myFs;
        std::shared_ptr<ServerIdDb> clusterIdMananger;
        //i'm the owner of this one
        std::shared_ptr<ConnectionMessageManager> messageManager;

        // each messageid has its vector of listeners 
        std::vector<std::shared_ptr<AbstractMessageManager>> listeners[256];

        //std::string jarFolder = "./jars";

        uint64_t lastDirUpdate = 0;

        PhysicalServer() {
            messageManager = (ConnectionMessageManager::create(*this));
        }

        /**
         * Init a connection with an other (hypothetical) peer.
         * @param peer peer to try to connect
         * @param sock our (client) socket to know what to tell him if he want to connect to us.
         * @return true if i am listening to him at the end, but do not trust this too much.
         * @throws InterruptedException
         * @throws IOException
         */
        bool initConnection(PeerPtr peer, std::shared_ptr<tcp::socket> sock);

    public:
        PhysicalServer(std::shared_ptr<FileSystemManager> fs, const std::filesystem::path& folderPath);

        void launchUpdater();

        void update();

        uint64_t getPeerId() override {
            return myPeerId;
        }

        uint16_t getComputerId() override;

        void listen(uint16_t port);


        /**
         * Try to connect to a peer at this address/port
         * @param ip address
         * @param port port
         * @return true if it's maybe connected, false if it's maybe not connected
         */
         //@SuppressWarnings("resource") // because it's managed by the peer object, but created in this function.
        bool connectTo(const std::string& ip, uint16_t port);

        PeerList& getPeersUnsafe() { return peers; }
        PeerList getPeersCopy();

        uint16_t getListenPort() override {
            return mySocket->local_endpoint().port();
            //return mySocket.getLocalPort();
        }

        void removeExactPeer(Peer* peer);
        void removeExactPeer(PeerPtr& peer);

        //File getJar(std::string name) {

        //	return new File(jarFolder + "/" + name + ".jar");
        //}

        //File getOrCreateJar(std::string name) {
        //	// list available files
        //	java.io.File file = getJar(name);
        //	if (file == null) {
        //		file = new File(jarFolder + "/" + name + ".jar");
        //		try {
        //			file.createNewFile();
        //		}
        //		catch (IOException e) {
        //			e.printStackTrace();
        //		}
        //	}

        //	return file;
        //}

        //@SuppressWarnings("rawtypes")
        //	public void runAlgo(std::string jarName, std::string className) {
        //	// TODO: maybe use https://github.com/kamranzafar/JCL to isolate each run
        //	URLClassLoader child;
        //	try {
        //		child = new URLClassLoader(new URL[]{ getJar(jarName).toURI().toURL() }, this.getClass().getClassLoader());

        //		Class classToLoad = Class.forName(className, true, child);
        //		Object instance = classToLoad.newInstance();
        //		@SuppressWarnings("unchecked")
        //			Method method = classToLoad.getDeclaredMethod("run", PhysicalServer.class);
        //		/* Object result = */ method.invoke(instance, this);
        //	}
        //	catch (MalformedURLException | ClassNotFoundException | NoSuchMethodException | SecurityException
        //		| IllegalAccessException | IllegalArgumentException | InvocationTargetException
        //		| InstantiationException e) {
        //		e.printStackTrace();
        //	}

        //}

        FileSystemManager* getFileSystem();

        bool rechooseId();

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    protected:
        //void writeEmptyMessage(uint8_t messageId, std::ostream out);

        //void writeMessage(uint8_t messageId, OutputStream out, ByteBuff message) {
        //}

        //ByteBuff readMessage(std::istream in);

    public:
        //void requestUpdate(int64_t since) override {
        //    // TODO Auto-generated method stub

        //}


        //void requestChunk(uint64_t fileId, uint64_t chunkId) override {
        //    // TODO Auto-generated method stub

        //}

        //void propagateDirectoryChange(uint64_t directoryId, std::vector<uint8_t> changes) override {
        //    // TODO Auto-generated method stub

        //}

        //void propagateFileChange(uint64_t directoryId, std::vector<uint8_t> changes) override {
        //    // TODO Auto-generated method stub

        //}

        //void propagateChunkChange(uint64_t directoryId, std::vector<uint8_t> changes) override {
        //    // TODO Auto-generated method stub

        //}

        //int writeBroadcastMessage(uint8_t messageId, ByteBuff& message);

        //bool writeMessage(uint64_t senderId, uint8_t messageId, ByteBuff& message);

        /*void writeMessage(Peer& p, uint8_t messageId, ByteBuff& message) {
            p.writeMessage(messageId, message);
        }*/

        PeerPtr getPeerPtr(uint64_t senderId) override;
        Peer& getPeer(uint64_t senderId) override;

        void registerListener(uint8_t messageId, std::shared_ptr<AbstractMessageManager> listener);

        void propagateMessage(uint64_t senderId, uint8_t messageId, ByteBuff& message);

        ConnectionMessageManager& message();

        void init(uint16_t listenPort);

        bool connect(const std::string& path, uint16_t port);

        ServerIdDb& getServerIdDb() override;

        void chooseComputerId() override;

        void initializeNewCluster() override;

        uint16_t getComputerId(uint64_t senderId) override;

        uint64_t getPeerIdFromCompId(uint16_t compId) override;

        int connect() override;

        size_t getNbPeers() override;

        tcp::endpoint getListening() override{
            return mySocket->local_endpoint();
        }

        ServerConnectionState getState() {
            return myInformalState;
        }

        void close() override;

        bool isConnecting() override;

    };


} // namespace supercloud
