#pragma once

#include "SynchroDb.hpp"


namespace supercloud {

    class SynchroAtConnection : public AbstractMessageManager, public std::enable_shared_from_this<SynchroAtConnection> {
        std::shared_ptr<ClusterManager> m_network;
        std::shared_ptr<FsStorage> m_filesystem;
        std::shared_ptr<SynchTreeMessageManager> m_synch_tree;
        bool have_root = false;
        SynchroAtConnection() {}
    public:
        //factory
        [[nodiscard]] static std::shared_ptr<SynchroAtConnection> create(
                std::shared_ptr<ClusterManager> network,
                std::shared_ptr<FsStorage> filesystem,
                std::shared_ptr<SynchTreeMessageManager> synch_tree) {
            std::shared_ptr<SynchroAtConnection> pointer = std::shared_ptr<SynchroAtConnection>{ new SynchroAtConnection() };
            pointer->init(network, filesystem, synch_tree);
            return pointer;
        }
        std::shared_ptr<SynchroAtConnection> ptr() { return shared_from_this(); }


        void init(std::shared_ptr<ClusterManager> network,
            std::shared_ptr<FsStorage> filesystem,
            std::shared_ptr<SynchTreeMessageManager> synch_tree);

        void launch();

        virtual void receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) override;
    };

    typedef std::shared_ptr<SynchroAtConnection> SynchroAtConnectionPtr;

}