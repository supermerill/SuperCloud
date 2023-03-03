#pragma once

#include "ClusterManager.hpp"
#include "IdentityManager.hpp"

#include <map>
#include <mutex>
#include <unordered_set>
#include <string>

namespace supercloud {

    class Peer;
    class IdentityManager;

    /// <summary>
    /// Used to manage the cluster connections.
    /// It initiate the connection to other peers (after the first)
    /// It emits pings, and speedtest (TODO)
    /// It keeps a graph of the network(TODO)
    /// </summary>
    class ClusterAdminMessageManager : public AbstractMessageManager, public std::enable_shared_from_this<ClusterAdminMessageManager>{
    public:

    protected:

        class Try {
        public:
            Try(IdentityManager::PeerData& d, IdentityManager::PeerConnection& c, std::future<bool>&& r)
                : data(d), connection(c), result(std::move(r))
            {}
            Try(const Try&) = delete; // can't copy
            Try& operator=(const Try&) = delete; // can't copy

            IdentityManager::PeerData data;
            IdentityManager::PeerConnection connection;
            std::future<bool> result;
        };
        std::mutex tries_mutex;
        std::vector<std::shared_ptr<Try>> tries;

        ClusterManager& clusterManager;

        struct TryConnectData {
            int64_t time;
            IdentityManager::PeerConnection data_used;
        };

        std::map<PeerPtr, std::vector<TryConnectData>> m_already_tried;



        ClusterAdminMessageManager(ClusterManager& physicalServer) : clusterManager(physicalServer) {}
        //launch multiple connection from the information we have in our database.
        void tryConnect(size_t max);
        //launch a connection
        std::optional<std::future<bool>> tryConnect(const IdentityManager::PeerData& peer_data, const IdentityManager::PeerConnection& to_test_and_launch);
        // get the fail/success result from the connections we launched, and update our database
        // (mostly update 'success_from' and 'firs hand information' if success).
        void updateConnectionResults();
        void register_listener();
    public:

        //factory
        [[nodiscard]] static std::shared_ptr<ClusterAdminMessageManager> create(ClusterManager& physicalServer) {
            std::shared_ptr<ClusterAdminMessageManager> pointer = std::shared_ptr<ClusterAdminMessageManager>{ new ClusterAdminMessageManager(physicalServer) };
            pointer->register_listener();
            return pointer;
        }

        std::shared_ptr<ClusterAdminMessageManager> ptr() {
            return shared_from_this();
        }


        void receiveMessage(PeerPtr peer, uint8_t messageId, const ByteBuff& message) override;


        // internal stuff. Public for testing

        struct DataSendServerDatabaseItem {
            ComputerId computer_id;
            PeerId last_peer_id;
            PublicKey rsa_public_key;
            Peer::ConnectionState current_state;
            //interface = pair ip-port
            std::vector<std::pair<std::string, uint16_t>> published_interfaces;
            std::optional<std::pair<std::string, uint16_t>> last_private_interface;
        };
        //void emitServerDatabase(Peer& sendTo, const std::vector<PeerPtr>& registered);
        std::vector<DataSendServerDatabaseItem> createSendServerDatabase(const std::vector<PeerPtr>& sendTo);
        ByteBuff createSendServerDatabaseMessage(const std::vector<DataSendServerDatabaseItem>& data);
        std::vector<DataSendServerDatabaseItem> readSendServerDatabaseMessage(const ByteBuff& msg);
        // populate the identitymanager database
        // return peers that have a unknown private/public interface to try to connect to.
        std::vector<PeerPtr> useServerDatabaseMessage(PeerPtr& sender, std::vector<DataSendServerDatabaseItem> data);

        //TODO, when udp is added.
        //ask a peer to create a hole punching to a peer we can't connect with.
        // ByteBuff createGetUpdHoleMessage();
        //A peer want to connect directly with a peer he can't join.
        // only if i'm not behind a nat.
        // If you can initiate a new connection to him, do so and send createSendUpdHoleMessage when the socket is open.
        // ByteBuff readGetTcpUpdMessage();
        //give the two peers their Udp ip&hole information (only works if my visibel ip are not behind a nat)
        // ByteBuff createSendUpdHoleMessage();
        // get the udp information and try to establish an udp connection.
        // ByteBuff readSendUpdHoleMessage();
    };
} // namespace supercloud
