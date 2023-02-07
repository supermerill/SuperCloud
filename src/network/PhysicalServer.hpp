#pragma once

#include <cstdint>
#include <filesystem>
#include <thread>
#include <mutex>

#include "ClusterManager.hpp"
#include "../utils/ByteBuff.hpp"
#include "Peer.hpp"
#include "networkAdapter.hpp"

namespace supercloud {

    class IdentityManager;
    class ClusterAdminMessageManager;
    class ConnectionMessageManager;

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
    class PhysicalServer : public ClusterManager, public std::enable_shared_from_this<PhysicalServer>{
    protected:
        bool has_peer_id = false;

        ServerConnectionState m_state;

        // should be inetAddress ut it's implier with this to test in 1 pc.
        // private Map<PeerKey ,Peer> peers = new HashMap<>();
        // private Semaphore peersSemaphore = new Semaphore(1);
        mutable std::recursive_mutex peers_mutex;
        PeerList peers; // don't access this outside of semaphore. Use getters instead.
        std::shared_ptr<ServerSocket> m_listen_socket; // server socket
        //std::unique_ptr<std::thread> socketListener; //just a pointer to the detached thread handle.
        //std::unique_ptr<std::thread> updaterThread;
        bool hasSocketListener = false;
        bool hasUpdaterThread = false;

        //pointer to other net managers
        //i'm the "owner" of these ones
        std::shared_ptr<ConnectionMessageManager> m_connection_manager;
        std::shared_ptr<ClusterAdminMessageManager> m_cluster_admin_manager;
        std::shared_ptr<IdentityManager> m_cluster_id_mananger;

        // each messageid has its vector of listeners
        mutable std::mutex listeners_mutex;
        std::map<uint8_t, std::vector<std::shared_ptr<AbstractMessageManager>>> listeners;


        // for update() method
        int64_t m_last_minute_update = 0;

        /**
         * Init a connection with an other (hypothetical) peer.
         * @param peer peer to try to connect
         * @param sock our (client) socket to know what to tell him if he want to connect to us.
         * @return true if i am listening to him at the end, but do not trust this too much.
         * @throws InterruptedException
         * @throws IOException
         */
        bool initConnection(PeerPtr peer, std::shared_ptr<Socket> sock, bool initiated_by_me);

        PhysicalServer();
    public:
        //not private to allow tests
        static std::shared_ptr<PhysicalServer> createForTests() {
            return std::shared_ptr<PhysicalServer>{ new PhysicalServer{} };
        }

        //factory
        [[nodiscard]] static std::shared_ptr<PhysicalServer> createAndInit(const std::filesystem::path& folderPath);

        std::shared_ptr<PhysicalServer> ptr() {
            return shared_from_this();
        }

        /// <summary>
        /// launch a thread that update() each seconds.
        /// </summary>
        void launchUpdater();
        /// <summary>
        /// The updater function, it emit some timers and keeps the list of peers tidy.
        /// </summary>
        virtual void update();

        /// <summary>
        /// If not started, create a new thread and listen on th eport for new connections.
        /// </summary>
        /// <param name="port"> the port to listne for.</param>
        virtual void listen(uint16_t port);
        /// <summary>
        /// Call listen(listenPort)
        /// </summary>
        void init(uint16_t listenPort);

        uint64_t getPeerId() const override;
        void setPeerId(uint64_t new_peer_id);
        bool hasPeerId() const { return has_peer_id && getPeerId() != 0 && getPeerId() != NO_PEER_ID; }
        uint16_t getComputerId() const override;

        const ServerConnectionState& getState() { return m_state; }

        /// <summary>
        /// get a peer. it's discouraged to use that method, as you should have better ways to get it.
        /// Check it, as it can be empty if the peer isn't connected.
        /// Can return closed/unconnected peers.
        /// </summary>
        PeerPtr getPeerPtr(uint64_t senderId) const;
        // same, prefer use your peer object if any
        uint16_t getComputerId(uint64_t senderId) const override;
        // same, prefer use your peer object if any
        uint64_t getPeerIdFromCompId(uint16_t compId) const override;
        // really useful?
        size_t getNbPeers() const override;
        // don't use that
        PeerList& getPeersUnsafe() { return peers; }
        // the current list of possibly connected peers.
        PeerList getPeersCopy() const override;

        /// <summary>
        /// create a new thread and call connectTo()
        /// </summary>
        /// <param name="ip">address</param>
        /// <param name="port">port</param>
        std::future<bool> connect(const std::string& path, uint16_t port, int64_t timeout_milis) override;

        /// <summary>
        /// Try to connect to a peer at this address/port.
        /// If the connection is successful, the thread will continue to listen to the socket until the end of it.
        /// </summary>
        /// <param name="ip">address</param>
        /// <param name="port">port</param>
        /// <returns>false if the connection can't be establish</returns>
        virtual bool connectTo(const std::string& ip, uint16_t port, int64_t timeout_milis, std::shared_ptr<std::promise<bool>> notify_socket_connection = {});


        uint16_t getListenPort() {
            return m_listen_socket->endpoint().port();
            //return mySocket.getLocalPort();
        }

        void removeExactPeer(Peer* peer);
        void removeExactPeer(PeerPtr& peer);


        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


        /// <summary>
        /// Register a listener to a message id to react to peers.
        /// When answering a message, please not lock the thread for too long (no sleep / read or other blocking method, use async or pop a thread).
        /// </summary>
        void registerListener(uint8_t messageId, std::shared_ptr<AbstractMessageManager> listener);

        void unregisterListener(uint8_t messageId, std::shared_ptr<AbstractMessageManager> listener);

        /// <summary>
        /// Propgate a message to listners.
        /// Be sure to have closed all your locks.
        /// The thread will be taken by the listeners for an unknown amount of time.
        /// </summary>
        /// <returns></returns>
        void propagateMessage(PeerPtr sender, uint8_t messageId, ByteBuff& message);


        IdentityManager& getIdentityManager() override;

        
        virtual void initializeNewCluster() override;

        void connect() override;

        /// <summary>
        /// Close current connections (peers) and try to reconnect them
        /// </summary>
        bool reconnect() override;

        virtual void close() override;

    };


} // namespace supercloud
