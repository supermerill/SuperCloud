#pragma once

#include "ClusterManager.hpp"

#include <string>
#include <mutex>

namespace supercloud {

    class PhysicalServer;
    class Peer;
    class ServerIdDb;

    class ConnectionMessageManagerServerSpecializedInterface {
    public:
        virtual ServerIdDb& getServerIdDb() = 0;
        //virtual std::mutex& getServerIdDbMutex() = 0;
        virtual std::shared_ptr<Peer> getPeerPtr(uint64_t senderId) = 0;
        virtual Peer& getPeer(uint64_t senderId) = 0;
        virtual uint16_t getListenPort() = 0;
        virtual void chooseComputerId() = 0;
        virtual uint64_t getPeerId() = 0;
    };
    class ConnectionMessageManagerServerInterface : public ConnectionMessageManagerServerSpecializedInterface,
        public ClusterManager
    {

    };

    class ConnectionMessageManager : public AbstractMessageManager {
    protected:
        ConnectionMessageManagerServerInterface& clusterManager;
        std::shared_ptr<ConnectionMessageManager> me;

        ConnectionMessageManager(ConnectionMessageManagerServerInterface& physicalServer) : clusterManager(physicalServer) {}

    public:

        static std::shared_ptr<ConnectionMessageManager> create(ConnectionMessageManagerServerInterface& physicalServer) {
            ConnectionMessageManager* pointer = new ConnectionMessageManager(physicalServer);
            pointer->me = std::shared_ptr<ConnectionMessageManager>{ pointer };
            pointer->register_listener();
            return pointer->me;
        }

        void register_listener();

        void receiveMessage(uint64_t senderId, uint8_t messageId, ByteBuff& message) override;

        void sendServerList(uint64_t sendTo, std::vector<std::shared_ptr<Peer>> list);

        void sendServerId(Peer& peer);

        void sendListenPort(Peer& peer);

    };
} // namespace supercloud
