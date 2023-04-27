#include "SynchroAtConnection.hpp"

#include "SynchTreeMessageManager.hpp"
#include "fs/base/FsStorage.hpp"
#include "network/Peer.hpp"
#include "network/ClusterManager.hpp"

#include <fstream>

namespace supercloud {


    void SynchroAtConnection::init(std::shared_ptr<ClusterManager> network,
            std::shared_ptr<FsStorage> filesystem,
            std::shared_ptr<SynchTreeMessageManager> synch_tree) {
        m_network = network;
        m_filesystem = filesystem;
        m_synch_tree = synch_tree;
    }


    void SynchroAtConnection::launch() {
        assert(m_network);
        assert(m_filesystem);
        assert(m_synch_tree);
        // to ask for root / changes since last time
        m_network->registerListener(*UnnencryptedMessageType::NEW_CONNECTION, this->ptr());
        // ask for root if received invalidation and i have nothing
        m_network->registerListener(*SynchMessagetype::SEND_INVALIDATE_ELT, this->ptr());
    }

    void SynchroAtConnection::receiveMessage(PeerPtr sender, uint8_t message_id, const ByteBuff& message) {
        if (!have_root) {
            have_root = this->m_filesystem->hasLocally(this->m_filesystem->getRoot());
        }
        if (message_id == *UnnencryptedMessageType::NEW_CONNECTION) {
            //check if we have a root
            if (!have_root) {
                this->m_synch_tree->fetchTree(this->m_filesystem->getRoot(), sender->getComputerId());
            }
            //ask for modification
            //TODO
            //this->m_synch_tree->get_invalidation_since(sender);

        } else if (message_id == *SynchMessagetype::SEND_INVALIDATE_ELT) {
            //if I reveived an invalidation and i don't have a root yet, ask for it.
            if (!have_root) {
                m_synch_tree->fetchTree(m_filesystem->getRoot(), sender->getComputerId());
            }
            //TODO: if i reveived an unknown invalidation, ask for the tree.
        }
    }
}// namespace
