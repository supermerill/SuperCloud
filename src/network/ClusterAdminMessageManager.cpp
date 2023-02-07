#include "ClusterAdminMessageManager.hpp"

#include "IdentityManager.hpp"

#include <algorithm>

namespace supercloud{

    void ClusterAdminMessageManager::register_listener() {
        clusterManager.registerListener(*UnnencryptedMessageType::CONNECTION_CLOSED, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::NEW_CONNECTION, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::TIMER_MINUTE, this->ptr());
    }

    void ClusterAdminMessageManager::receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) {
        // answer to deletion
        if (!sender->isAlive()) {
            return;
        }
        if (messageId == *UnnencryptedMessageType::NEW_CONNECTION) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_DATABASE);
            //find if this peer isn't already loaded
            clusterManager.getIdentityManager().fusionWithConnectedPeer(sender);
            if (sender->initiatedByMe()){
                // save this successful connection
                IdentityManager::PeerData old_peer_data = clusterManager.getIdentityManager().getPeerData(sender->getComputerId());
                IdentityManager::PeerData peer_data = old_peer_data;
                // get local ip
                peer_data.private_interface = IdentityManager::PeerConnection{ sender->getIP(), sender->getPort(), true, {sender->getLocalIPNetwork()} };
                clusterManager.getIdentityManager().setPeerData(old_peer_data, peer_data);
            }

            //propagate this new information to other peers
            std::vector<PeerPtr> peers = clusterManager.getPeersCopy();
            ByteBuff buffer = this->createSendServerDatabaseMessage(this->createSendServerDatabase(std::vector<PeerPtr>{ sender }));
            for (PeerPtr& peer : peers) {
                if (peer && peer->isAlive() && peer->isConnected() && peer != sender && peer->getComputerId() != sender->getComputerId()) {
                    peer->writeMessage(*UnnencryptedMessageType::SEND_SERVER_DATABASE, buffer);
                    buffer.flip(); // reset the buffer
                }
            }

            //untag this connection data as failed.
            auto& connections = this->m_already_tried[sender];
            if (!connections.empty()) {
                //ie delete last entry
                connections.erase(connections.end() - 1);
            }

            //TODO: if first, then try to connect to some more peers
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_DATABASE) {
            // //already given our peerid, our computerid and public key. Our state is connected, but it's not the end of the workd to repeat
            std::vector<PeerPtr> allPeers = clusterManager.getIdentityManager().getLoadedPeers();
            // add us as the first entry
            allPeers.insert(allPeers.begin(), clusterManager.getIdentityManager().getSelfPeer());
            // remove current sender. he known himself
            allPeers.erase(std::find(allPeers.begin(), allPeers.end(), sender));
            //create & send
            sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_DATABASE,
                this->createSendServerDatabaseMessage(this->createSendServerDatabase(allPeers)));
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_DATABASE) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " received SEND_SERVER_LIST");
            this->useServerDatabaseMessage(sender, this->readSendServerDatabaseMessage(message));
        }
        if (messageId == *UnnencryptedMessageType::TIMER_MINUTE) {
            //max 100 connections ?
            int32_t available_conenction_slots = 100;
            tryConnect(100);
        }
    }

    void ClusterAdminMessageManager::tryConnect(size_t max) {
        struct Try {
            Try(IdentityManager::PeerData& d, IdentityManager::PeerConnection& c, std::future<bool>&& r)
                : data(d), connection(c), result(std::move(r))
            {}
            IdentityManager::PeerData& data;
            IdentityManager::PeerConnection& connection;
            std::future<bool> result;
        };
        //try to connect to some nice peers that aren't connected yet
        //get the list of peers we are connected with / we try to connect currently
        PeerList peers = clusterManager.getPeersCopy();
        //get the list of peers we know
        std::vector<PeerPtr> database_peers = clusterManager.getIdentityManager().getLoadedPeers();
        //diff so we can have the list of peers that we aren't connected yet.
        for (PeerPtr& peer : peers) {
            foreach(peer_it, database_peers) {
                if (peer_it->get() == peer.get()) {
                    peer_it.erase();
                }
            }
        }
        std::vector<IdentityManager::PeerData> database;
        for (PeerPtr& peer : database_peers) {
            database.push_back(clusterManager.getIdentityManager().getPeerData(peer->getComputerId()));
        }
        std::vector<Try> tries;
        // connect & tag peers we already succeed to connect in the past on the current network
        std::string our_current_network = this->clusterManager.getIdentityManager().getSelfPeer()->getLocalIPNetwork();
        for (IdentityManager::PeerData& entry : database) {
            if (entry.private_interface
                && (!entry.private_interface->success_from.empty()) && contains(entry.private_interface->success_from, our_current_network)
                && entry.private_interface->first_hand_information) {
                if (auto is_connected = tryConnect(entry, *entry.private_interface); is_connected.has_value()) {
                    tries.push_back(Try{ entry, *entry.private_interface, std::move(is_connected.value()) });
                }
            }
            if (tries.size() >= max) { break; }
            for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                if ((!pub.success_from.empty()) && contains(pub.success_from, our_current_network)
                    && pub.first_hand_information) {
                    if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                        tries.push_back(Try{ entry, pub, std::move(is_connected.value()) });
                    }
                }
                if (tries.size() >= max) { break; }
            }
        }
        // connect & tag peers that have an entry for our current network.
        if (tries.size() < max) {
            for (IdentityManager::PeerData& entry : database) {
                if (entry.private_interface
                    && !entry.private_interface->success_from.empty() && contains(entry.private_interface->success_from, our_current_network)) {
                    if (auto is_connected = tryConnect(entry, *entry.private_interface); is_connected.has_value()) {
                        tries.emplace_back(entry, *entry.private_interface, std::move(is_connected.value()));
                    }
                }
                if (tries.size() >= max) { break; }
                for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                    if (!pub.success_from.empty() && contains(pub.success_from, our_current_network)) {
                        if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                            tries.emplace_back(entry, pub, std::move(is_connected.value()));
                        }
                    }
                    if (tries.size() >= max) { break; }
                }
            }
        }

        // connect & tag peers we already connect with.
        if (tries.size() < max) {
            for (IdentityManager::PeerData& entry : database) {
                if (entry.private_interface && entry.private_interface->first_hand_information
                    && !entry.private_interface->success_from.empty()) {
                    if (auto is_connected = tryConnect(entry, *entry.private_interface); is_connected.has_value()) {
                        tries.emplace_back(entry, *entry.private_interface, std::move(is_connected.value()));
                    }
                }
                if (tries.size() >= max) { break; }
                for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                    if (!pub.success_from.empty() && pub.first_hand_information) {
                        if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                            tries.emplace_back(entry, pub, std::move(is_connected.value()));
                        }
                    }
                    if (tries.size() >= max) { break; }
                }
            }
        }

        // connect & tag peers with the most recent entry..
        if (tries.size() < max) {
            for (IdentityManager::PeerData& entry : database) {
                if (entry.private_interface && entry.private_interface->first_hand_information
                    && !entry.private_interface->success_from.empty()) {
                    if (auto is_connected = tryConnect(entry, *entry.private_interface); is_connected.has_value()) {
                        tries.emplace_back(entry, *entry.private_interface, std::move(is_connected.value()));
                    }
                }
                if (tries.size() >= max) { break; }
                for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                    if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                        tries.emplace_back(entry, pub, std::move(is_connected.value()));
                    }
                    if (tries.size() >= max) { break; }
                }
            }
        }

        //wait for connections & store results
        for (Try & essai : tries) {
            if (essai.result.get()) {
                IdentityManager::PeerData old_data = clusterManager.getIdentityManager().getPeerData(essai.data.peer->getComputerId());
                IdentityManager::PeerData new_data = old_data;
                //find essai.connection
                IdentityManager::PeerConnection* connection = nullptr;
                if (new_data.private_interface && new_data.private_interface->address == essai.connection.address && new_data.private_interface->port == essai.connection.port) {
                    connection = &(*new_data.private_interface);
                } else if(!new_data.public_interfaces.empty()){
                    auto it = std::find_if(new_data.public_interfaces.begin(), new_data.public_interfaces.end(), [&essai](const IdentityManager::PeerConnection& pc)
                        { return pc.address == essai.connection.address && pc.port == essai.connection.port; });
                    if (it != new_data.public_interfaces.end()) {
                        connection = &(*it);
                    }
                }
                if (connection != nullptr) {
                    connection->first_hand_information = true;
                    if (!contains(connection->success_from, essai.data.peer->getLocalIPNetwork())) {
                        connection->success_from.push_back(essai.data.peer->getLocalIPNetwork());
                    }
                    clusterManager.getIdentityManager().setPeerData(old_data, new_data); // osef while
                } else {
                    log("warn: can't write the success of a connection, because it' snot anymore in the database.");
                }
            }
        }

    }

    std::optional<std::future<bool>> ClusterAdminMessageManager::tryConnect(const IdentityManager::PeerData& peer_data, const IdentityManager::PeerConnection& to_test_and_launch) {
        //test that it isn't tried yet
        bool ok = !peer_data.peer->isAlive() && !peer_data.peer->isConnected();
        if (ok) {
            if (auto it = m_already_tried.find(peer_data.peer); it != m_already_tried.end()) {
                for (auto& data : it->second) {
                    //TODO: use the time to retry if long enough?
                    if (data.data_used == to_test_and_launch) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (ok) {
            m_already_tried[peer_data.peer].push_back(TryConnectData{ get_current_time_milis(), to_test_and_launch });
            return clusterManager.connect(to_test_and_launch.address, to_test_and_launch.port, 1000);
        }
        return {};
    }

    void ClusterAdminMessageManager::useServerDatabaseMessage(PeerPtr& sender, std::vector<DataSendServerDatabaseItem> data) {
        // first entry: verify it's my sender peer
        DataSendServerDatabaseItem& first_item = data.front();
        IdentityManager::PeerData sender_data_old;
        IdentityManager::PeerData sender_data;
        do {
            sender_data_old = clusterManager.getIdentityManager().getPeerData(sender->getComputerId());
            sender_data = sender_data_old;
            if (first_item.last_peer_id != sender->getPeerId()
                || first_item.computer_id != sender->getComputerId()
                || first_item.rsa_public_key != sender_data.rsa_public_key) {
                //problem, abord
                error(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " Error, peer has send me a bad header for its ServerDatabaseMessage");
                return;
            } else {
                //update our peer data
                if (first_item.last_private_interface) {
                    sender_data.private_interface = IdentityManager::PeerConnection{first_item.last_private_interface->first, first_item.last_private_interface->second, true};
                    sender_data.private_interface->success_from.push_back(clusterManager.getIdentityManager().getSelfPeer()->getIP());
                }
            }
        } while (!clusterManager.getIdentityManager().setPeerData(sender_data, sender_data));

        //other entries: 
        for (auto it = data.begin() + 1; it != data.end(); ++it) {
            //check data
            IdentityManager::PeerData old_peer_data = clusterManager.getIdentityManager().getPeerData(it->computer_id);
            IdentityManager::PeerData peer_data = old_peer_data;
            if (peer_data.peer) {
                //found, update informations about pub & priv interface if not present
                if (peer_data.rsa_public_key != it->rsa_public_key) {
                    //error, different pub key than expected
                    error("Error, a public key is in conflict, abord useServerDatabaseMessage merge.");
                    continue;
                } else {
                    bool modified = false;
                    if (!peer_data.private_interface) {
                        modified = true;
                        peer_data.private_interface = { it->last_private_interface->first, it->last_private_interface->second, false };
                    }
                    for (auto& pub_interface : it->published_interfaces) {
                        bool found = false;
                        for (auto& my_pub_interface : peer_data.public_interfaces) {
                            if (pub_interface.first == my_pub_interface.address && pub_interface.second == my_pub_interface.port) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            modified = true;
                            //peer_data.public_interfaces.emplace_back(pub_interface.first, pub_interface.second, false);
                            peer_data.public_interfaces.push_back(IdentityManager::PeerConnection{ pub_interface.first, pub_interface.second, false, {} });
                        }
                    }
                    if (modified) {
                        clusterManager.getIdentityManager().setPeerData(old_peer_data, peer_data);
                    }
                }
            } else {
                //not found, just create the new entry
                peer_data.rsa_public_key = it->rsa_public_key;
                if (it->last_private_interface) {
                    peer_data.private_interface = IdentityManager::PeerConnection{ it->last_private_interface->first, it->last_private_interface->second, false };
                }
                for (auto& pub_interface : it->published_interfaces) {
                    //peer_data.public_interfaces.emplace_back(pub_interface.first, pub_interface.second, false);
                    peer_data.public_interfaces.push_back(IdentityManager::PeerConnection{ pub_interface.first, pub_interface.second, false, {} });
                }
                PeerPtr new_peer = clusterManager.getIdentityManager().addNewPeer(it->computer_id, peer_data);
                //change peer state
                {
                    std::lock_guard lock{ new_peer->synchronize() };
                    Peer::ConnectionState state = new_peer->getState();
                    bool changed = false;
                    if (0 != (it->current_state & Peer::ConnectionState::CONNECTED) || 0 != (it->current_state & Peer::ConnectionState::ALIVE_IN_NETWORK)) {
                        state |= Peer::ConnectionState::ALIVE_IN_NETWORK;
                        changed = true;
                    }
                    if (changed) {
                        new_peer->setState(state);
                    }
                }
            }
        }
    }

    std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ClusterAdminMessageManager::createSendServerDatabase(const std::vector<PeerPtr>& all_peers) {
        std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ret_list;
        // //just give our provate interface and our public ones.
        for (const PeerPtr& peer : all_peers) {
            IdentityManager::PeerData data = clusterManager.getIdentityManager().getPeerData(peer->getComputerId());
            ClusterAdminMessageManager::DataSendServerDatabaseItem& item = ret_list.emplace_back();
            item.computer_id = peer->getComputerId();
            item.last_peer_id = peer->getPeerId();
            item.current_state = peer->getState();
            item.rsa_public_key = data.rsa_public_key;
            if (data.private_interface) {
                item.last_private_interface = { data.private_interface->address, data.private_interface->port };
            }
            for (const IdentityManager::PeerConnection& connection : data.public_interfaces) {
                item.published_interfaces.emplace_back(connection.address, connection.port);
            }
        }
        return ret_list;
    }

    ByteBuff ClusterAdminMessageManager::createSendServerDatabaseMessage(const std::vector<DataSendServerDatabaseItem>& data) {
        //for each peer in my db
        //emit it's last peerid
        //emit it's last computer id 

        ByteBuff buff;
        // == ourself ==
        // == our database ==
        buff.putSize(data.size());
        for (const DataSendServerDatabaseItem& peer : data) {
            // the peer id, not that useful, but can help to match with already connected peers
            buff.putULong(peer.last_peer_id);
            //computer id, to identify the peer.
            buff.putUShort(peer.computer_id);
            //the rsa key to identify it
            buff.putUTF8(peer.rsa_public_key);
            //the known current state of this computer in the network
            buff.put(peer.current_state);
            //the private interface (if any)
            if (peer.last_private_interface) {
                buff.putUTF8(peer.last_private_interface->first);
                buff.putUShort(peer.last_private_interface->second);
            } else {
                buff.putUTF8("");
                buff.putUShort(0);
            }
            buff.putSize(peer.published_interfaces.size());
            for (const auto& connection : peer.published_interfaces) {
                buff.putUTF8(connection.first);
                buff.putUShort(connection.second);
            }
        }
        buff.flip();
        return buff;
    }

    std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ClusterAdminMessageManager::readSendServerDatabaseMessage(ByteBuff& buff) {
        std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ret_list;

        const size_t nb_peers = buff.getSize();
        for (size_t idx_peer = 0; idx_peer < nb_peers; ++idx_peer) {
            DataSendServerDatabaseItem& peer = ret_list.emplace_back();
            // the peer id, not that useful, but can help to match with already connected peers
            peer.last_peer_id = buff.getULong();
            //computer id, to identify the peer.
            peer.computer_id = buff.getUShort();
            //the rsa key to identify it
            peer.rsa_public_key = buff.getUTF8();
            //the known current state of this computer in the network
            peer.current_state = Peer::ConnectionState(buff.get());
            //the private interface (if any)
            std::string priv_addr = buff.getUTF8();
            uint16_t priv_port = buff.getUShort();
            if (priv_port != 0 || priv_addr != "") {
                peer.last_private_interface = { priv_addr, priv_port };
            }
            const size_t nb_public_interfaces = buff.getSize();
            for (size_t idx_interface = 0; idx_interface < nb_public_interfaces; ++idx_interface) {
                std::string pub_addr = buff.getUTF8();
                uint16_t pub_port = buff.getUShort();
                peer.published_interfaces.emplace_back(pub_addr, pub_port);
            }
        }
        return ret_list;
    }

} // namespace supercloud
