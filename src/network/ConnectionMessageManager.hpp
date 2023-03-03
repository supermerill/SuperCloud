#pragma once

#include "ClusterManager.hpp"

#include <map>
#include <mutex>
#include <unordered_set>
#include <string>

namespace supercloud {

    class Peer;
    class IdentityManager;
    class PhysicalServer;

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

        //can be set to false to verify that it still establish connection if evrything is symetrical, and verbose.
        static inline bool QUICKER_CONNECTION = true;

        enum class ConnectionStep: uint8_t {
            BORN,
            ID,
            SERVER_LIST,
            RSA,
            IDENTITY_VERIFIED,
            AES,
            CONNECTED,
        };

        struct ConnectionStatus {
            // our connection state
            ConnectionStep current = ConnectionStep::BORN;
            //last request emitted
            ConnectionStep last_request = ConnectionStep::BORN;
            //last time we emitted a request (to know if it's time to do it again)
            int64_t last_update = 0;
        };

        struct Data_SEND_SERVER_ID {
            PeerId peer_id;
            uint64_t cluster_id;
            uint16_t port;
        };

        struct Data_SEND_SERVER_LIST {
            std::unordered_set<ComputerId> registered_computer_id;
            std::unordered_set<ComputerId> connected_computer_id;
            std::unordered_set<PeerId> registered_peer_id;
            std::unordered_map<PeerId, ComputerId> connected_peer_id;
            std::unordered_map<ComputerId, PublicKey> our_keys;
        };
    protected:
        std::shared_ptr<PhysicalServer> clusterManager;
        mutable std::mutex m_status_mutex;
        std::map<PeerPtr, ConnectionStatus> status;
        ServerConnectionState& m_connection_state;

        ConnectionMessageManager(std::shared_ptr<PhysicalServer> physicalServer, ServerConnectionState& state) : clusterManager(physicalServer), m_connection_state(state) {}

        void reconnectWithNewComputerId();
        void setStatus(const PeerPtr& peer, const ConnectionStep new_status);
        void chooseComputerId(const std::unordered_set<ComputerId>& registered_computer_id,
            const std::unordered_set<ComputerId>& connected_computer_id, const std::unordered_map<ComputerId, PublicKey>& known_keys);
        void choosePeerId(const std::unordered_set<PeerId>& registered_peer_id, const std::unordered_map<PeerId, ComputerId>& connected_peer_id);

    public:

        //factory
        [[nodiscard]] static std::shared_ptr<ConnectionMessageManager> create(std::shared_ptr<PhysicalServer> physicalServer, ServerConnectionState& connection_state) {
            std::shared_ptr<ConnectionMessageManager> pointer = std::shared_ptr<ConnectionMessageManager>{ new ConnectionMessageManager(physicalServer, connection_state) };
            pointer->register_listener();
            return pointer;
        }

        std::shared_ptr<ConnectionMessageManager> ptr() {
            return shared_from_this();
        }

        void register_listener();

        void requestCurrentStep(PeerPtr sender, bool enforce);

        void receiveMessage(PeerPtr peer, uint8_t messageId, const ByteBuff& message) override;


     //only for tests, should be protected
        ByteBuff create_SEND_SERVER_ID_msg(Data_SEND_SERVER_ID& data);
        Data_SEND_SERVER_ID get_SEND_SERVER_ID_msg(const ByteBuff& msg);

        Data_SEND_SERVER_LIST create_Data_SEND_SERVER_LIST(PeerPtr other);
        ByteBuff create_SEND_SERVER_LIST_msg(Data_SEND_SERVER_LIST& data);
        Data_SEND_SERVER_LIST get_SEND_SERVER_LIST_msg(const ByteBuff& msg);

    };
} // namespace supercloud
