#pragma once

#include "ClusterManager.hpp"

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
        ClusterManager& clusterManager;

        ClusterAdminMessageManager(ClusterManager& physicalServer) : clusterManager(physicalServer) {}

        void emitServerDatabase(Peer& sendTo, const std::vector<PeerPtr>& registered);
        void readServerDatabaseMessage(ByteBuff& msg);

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

        void register_listener();

        void receiveMessage(PeerPtr peer, uint8_t messageId, ByteBuff message) override;

    };
} // namespace supercloud
