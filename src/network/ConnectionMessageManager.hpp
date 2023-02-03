#pragma once

#include "ClusterManager.hpp"

#include <map>
#include <mutex>
#include <unordered_set>
#include <string>

namespace supercloud {

    class PhysicalServer;
    class Peer;
    class ServerIdDb;

    class ConnectionMessageManagerServerSpecializedInterface {
    public:
        virtual ServerIdDb& getServerIdDb() = 0;
        //virtual std::mutex& getServerIdDbMutex() = 0;
        virtual PeerList getPeersCopy() = 0;
        virtual std::shared_ptr<Peer> getPeerPtr(uint64_t senderId) = 0;
        virtual Peer& getPeer(uint64_t senderId) = 0;
        virtual uint16_t getListenPort() = 0;
        virtual uint64_t getPeerId() = 0;
        virtual void setPeerId(uint64_t new_peer_id) = 0;
        virtual bool hasPeerId() = 0;
    };
    class ConnectionMessageManagerServerInterface : public ConnectionMessageManagerServerSpecializedInterface,
        public ClusterManager
    {

    };

    //struct Connection_data{};

    /// <summary>
    /// Used to manage the connection with a peer (an the network if it's the first peer you connect to).
    /// steps:
    /// 1) exchange your cluster_id, peerId (if any) and listen port (_SERVER_ID)
    /// If the cluster_id are not the same, then disconnect this peer, you have nothing to do with it.
    /// Store the port for future use.
    /// 2) exchange your server list (all computerId ever and the connected peerid and computerid, no ip/port)
    /// If you don't have a computerId yet, choose one that isn't taken yet.
    /// If you find that your computer_id is already connected, you should emit an error message ("already connected") and quit. You can continue in read-only mode, but be careful.
    /// If you're not connected yet, then choose a peerId that isn't taken 
    ///     (at least in the connected list. The peers ids in the registered part are more a "nice touch" to avoid too much conflict if someone reconnect).
    /// 3) exchange your computer_id, your peer_id and your RSA public key (_SERVER_PUBLIC_KEY)
    /// If you find that the other peer has the same computerId as you, disconnect.
    /// If you find that the other one has a peerid that is already connected, then send him again the (_SERVER_LIST) and then disconnect. 
    ///     Maybe two peers conencted at the same time and choose the same thing, but the most probable is just that it's a bad peer.
    /// 4) exchange an message, encrypted with your RSA private and his public key, to verify he really has his private key. (_VERIFY_IDENTITY)
    /// if it fails, then it's a bad peer. Erase it from your list and disconnect
    /// 5) exchange (encrypted with RSA) an AES key so you can encrypt message faster (_SERVER_AES_KEY)
    /// 6) TODO: verify the aes can decrypt
    /// if it fails, then it's a bad peer. Erase it from your list and disconnect 
    /// After that, all messae exchange are encrypted with AES (unless deactivated)
    /// You're now considered connected to the network
    /// 
    /// Note: if after beeing conencted, you find another peer that has the same computerId, I advise to disconnect yourself with an error to avoid conflict, or to stay in a 'read-only' mode.
    /// Because modification from another peer with the same computer id may not be sent to you, and the opposite is true.
    /// Note that other peers different than your sibling won't evict you.
    /// 
    /// Note: if after beeing conencted, you find another peer that has the same peerId, most of the other peers will evict you or your sibling.
    /// If you have the lowest string(ip+port), then the other peers may leave you inside the network.
    /// Overwise You'll have to reconnect quickly with a new random unsued peerid.
    /// </summary>
    class ConnectionMessageManager : public AbstractMessageManager, public std::enable_shared_from_this<ConnectionMessageManager>{
    public:

        enum class ConnectionStep: uint8_t {
            BORN,
            ID,
            SERVER_LIST,
            RSA,
            IDENTITY_VERIFIED,
            AES,
        };

        struct ConnectionStatus {
            ConnectionStep recv = ConnectionStep::BORN;
            ConnectionStep last_request = ConnectionStep::BORN;
        };
    protected:
        ConnectionMessageManagerServerInterface& clusterManager;

        ConnectionMessageManager(ConnectionMessageManagerServerInterface& physicalServer) : clusterManager(physicalServer) {}

        void chooseComputerId(const std::unordered_set<uint16_t>& registered_computer_id, const std::unordered_set<uint16_t>& connected_computer_id);
        void choosePeerId(const std::unordered_set<uint64_t>& registered_peer_id, const std::unordered_set<uint64_t>& connected_peer_id);

        std::map<PeerPtr, ConnectionStatus> status;
    public:

        //factory
        [[nodiscard]] static std::shared_ptr<ConnectionMessageManager> create(ConnectionMessageManagerServerInterface& physicalServer) {
            std::shared_ptr<ConnectionMessageManager> pointer = std::shared_ptr<ConnectionMessageManager>{ new ConnectionMessageManager(physicalServer) };
            pointer->register_listener();
            return pointer;
        }

        std::shared_ptr<ConnectionMessageManager> ptr() {
            return shared_from_this();
        }

        void register_listener();

        void requestCurrentStep(PeerPtr sender, bool enforce = true);

        void receiveMessage(PeerPtr peer, uint8_t messageId, ByteBuff message) override;

        void sendServerList(Peer& sendTo, const std::vector<PeerPtr>& registered, const PeerList& connected);

        void sendServerId(Peer& peer);

        void useServerList(PeerPtr sender, ByteBuff& message);

    };
} // namespace supercloud
