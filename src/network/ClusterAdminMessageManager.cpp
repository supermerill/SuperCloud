#include "ClusterAdminMessageManager.hpp"

#include "IdentityManager.hpp"

#include <algorithm>

#define log //log

namespace supercloud{

    void ClusterAdminMessageManager::register_listener() {
        clusterManager.registerListener(*UnnencryptedMessageType::CONNECTION_CLOSED, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::NEW_CONNECTION, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::TIMER_MINUTE, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::GET_SERVER_DATABASE, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::SEND_SERVER_DATABASE, this->ptr());
    }

    void ClusterAdminMessageManager::receiveMessage(PeerPtr sender, uint8_t messageId, const ByteBuff& message) {
        //log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) receive message "+ messageId+" : "+ messageId_to_string(messageId));
        // answer to deletion
        if (!sender->isAlive()) {
            return;
        }
        if (messageId == *UnnencryptedMessageType::NEW_CONNECTION) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) receive NEW_CONNECTION");
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_DATABASE);
            

            // don't propagate this new information to other peers before receiving its database (to have its private & public interface)

            //untag this connection data as failed.
            auto& connections = this->m_already_tried[sender];
            if (!connections.empty()) {
                //ie delete last entry
                connections.erase(connections.end() - 1);
            }

            //TODO: if first, then try to connect to some more peers
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_DATABASE) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) receive GET_SERVER_DATABASE");
            // //already given our peerid, our computerid and public key. Our state is connected, but it's not the end of the workd to repeat
            PeerList allPeers = clusterManager.getIdentityManager().getLoadedPeers();
            // add us as the first entry
            allPeers.insert(allPeers.begin(), clusterManager.getIdentityManager().getSelfPeer());
            // remove current sender. he known himself
            auto it = std::find(allPeers.begin(), allPeers.end(), sender);
            if (it != allPeers.end()) {
                allPeers.erase(it);
            }
            //create & send
            std::vector<DataSendServerDatabaseItem> my_database_to_send = this->createSendServerDatabase(allPeers);
            if (!my_database_to_send.empty()) {
                sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_DATABASE, this->createSendServerDatabaseMessage(my_database_to_send));
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_DATABASE) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) received SEND_SERVER_DATABASE");
            PeerList try_to_connect_with = this->useServerDatabaseMessage(sender, this->readSendServerDatabaseMessage(message));
            //try to connect
            std::lock_guard lock{ tries_mutex };
            for (PeerPtr& peer : try_to_connect_with) {
                IdentityManager::PeerData data = clusterManager.getIdentityManager().getPeerData(peer);
                if (data.private_interface.has_value()) {
                    if (auto is_connected = tryConnect(data, *data.private_interface); is_connected.has_value()) {
                        this->tries.push_back(std::shared_ptr<Try>{new Try{ data, *data.private_interface, std::move(is_connected.value()) }});
                    }
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::TIMER_MINUTE) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + ((sender?sender->getPeerId():0) % 100) + " (ClusterAdminMessageManager) received TIMER_MINUTE");
            //max 100 connections ?
            int32_t available_conenction_slots = 100;
            tryConnect(100);
        }

        updateConnectionResults();
    }

    void ClusterAdminMessageManager::tryConnect(size_t max) {
        std::lock_guard lock{ tries_mutex };
        //try to connect to some nice peers that aren't connected yet
        //get the list of peers we are connected with / we try to connect currently
        PeerList peers = clusterManager.getPeersCopy();
        //get the list of peers we know
        PeerList database_peers = clusterManager.getIdentityManager().getLoadedPeers();
        //diff so we can have the list of peers that we aren't connected yet.
        for (PeerPtr& peer : peers) {
            foreach(peer_it, database_peers) {
                if (peer_it->get() == peer.get() || (peer_it->get()->getState() & Peer::ConnectionState::US)) {
                    peer_it.erase();
                }
            }
        }
        std::vector<IdentityManager::PeerData> database;
        for (PeerPtr& peer : database_peers) {
            database.push_back(clusterManager.getIdentityManager().getPeerData(peer));
        }
        // connect & tag peers we already succeed to connect in the past on the current network
        std::string our_current_network = this->clusterManager.getIdentityManager().getSelfPeer()->getLocalIPNetwork();
        for (IdentityManager::PeerData& entry : database) {
            if (entry.private_interface
                && (!entry.private_interface->success_from.empty()) && contains(entry.private_interface->success_from, our_current_network)
                && entry.private_interface->first_hand_information) {
                if (auto is_connected = tryConnect(entry, *entry.private_interface); is_connected.has_value()) {
                    tries.push_back(std::shared_ptr<Try>{ new Try{ entry, *entry.private_interface, std::move(is_connected.value()) } });
                }
            }
            if (tries.size() >= max) { break; }
            for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                if ((!pub.success_from.empty()) && contains(pub.success_from, our_current_network)
                    && pub.first_hand_information) {
                    if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                        tries.push_back(std::shared_ptr<Try>{ new Try{ entry, pub, std::move(is_connected.value()) }});
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
                        tries.emplace_back(new Try{ entry, *entry.private_interface, std::move(is_connected.value()) });
                    }
                }
                if (tries.size() >= max) { break; }
                for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                    if (!pub.success_from.empty() && contains(pub.success_from, our_current_network)) {
                        if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                            tries.emplace_back(new Try{ entry, pub, std::move(is_connected.value()) });
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
                        tries.emplace_back(new Try{ entry, *entry.private_interface, std::move(is_connected.value()) });
                    }
                }
                if (tries.size() >= max) { break; }
                for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                    if (!pub.success_from.empty() && pub.first_hand_information) {
                        if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                            tries.emplace_back(new Try{ entry, pub, std::move(is_connected.value()) });
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
                        tries.emplace_back(new Try{ entry, *entry.private_interface, std::move(is_connected.value()) });
                    }
                }
                if (tries.size() >= max) { break; }
                for (IdentityManager::PeerConnection pub : entry.public_interfaces) {
                    if (auto is_connected = tryConnect(entry, pub); is_connected.has_value()) {
                        tries.emplace_back(new Try{ entry, pub, std::move(is_connected.value()) });
                    }
                    if (tries.size() >= max) { break; }
                }
            }
        }

    }

    void ClusterAdminMessageManager::updateConnectionResults() {
        std::lock_guard lock{ tries_mutex };
        //wait for connections & store results
        foreach( it_try, tries) {
            Try& essai = **it_try;
            if (essai.result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                if (essai.result.get() && essai.data.peer->isAlive() && clusterManager.getIdentityManager().hasPeerData(essai.data.peer)) {
                    IdentityManager::PeerData old_data = clusterManager.getIdentityManager().getPeerData(essai.data.peer);
                    IdentityManager::PeerData new_data = old_data;
                    //find essai.connection
                    IdentityManager::PeerConnection* connection = nullptr;
                    if (new_data.private_interface && new_data.private_interface->address == essai.connection.address && new_data.private_interface->port == essai.connection.port) {
                        connection = &(*new_data.private_interface);
                    } else if (!new_data.public_interfaces.empty()) {
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
                } else {
                    //nothing
                }
                //i used it, now delete.
                it_try.erase();
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
            return clusterManager.connect(peer_data.peer, to_test_and_launch.address, to_test_and_launch.port, 1000);
        }
        return {};
    }

    std::mutex useServerDatabaseMessage_mutex;
    PeerList ClusterAdminMessageManager::useServerDatabaseMessage(PeerPtr& sender, std::vector<DataSendServerDatabaseItem> data) {
        if (data.empty()) {
            return {};
        }
        std::lock_guard lock{ useServerDatabaseMessage_mutex };
        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) useServerDatabaseMessage: " + data.size());
        // first entry: verify it's my sender peer
        DataSendServerDatabaseItem& first_item = data.front();
        size_t start_at = 0;
        IdentityManager::PeerData sender_data_old;
        IdentityManager::PeerData sender_data;
        std::vector<DataSendServerDatabaseItem> data_useful;
        PeerList try_to_connect_with;
        if (first_item.last_peer_id == sender->getPeerId()) {
            data_useful.push_back(first_item);
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) first entry is the sender");
            start_at = 1;
            do {
                sender_data_old = clusterManager.getIdentityManager().getPeerData(sender);
                sender_data = sender_data_old;
                if( first_item.computer_id != sender->getComputerId()
                    || first_item.rsa_public_key != sender_data.rsa_public_key) {
                    //problem, abord
                    error(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " Error, peer has send me a bad header for its ServerDatabaseMessage");
                    return {};
                } else {
                    //update our peer data
                    if (first_item.last_private_interface) {
                        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) update the priate interface");
                        sender_data.private_interface = IdentityManager::PeerConnection{first_item.last_private_interface->first, first_item.last_private_interface->second, true};
                        sender_data.private_interface->success_from.push_back(clusterManager.getIdentityManager().getSelfPeer()->getIP());
                    }
                }
            } while (!clusterManager.getIdentityManager().setPeerData(sender_data_old, sender_data));
        }

        //other entries: 
        for (auto it = data.begin() + start_at; it != data.end(); ++it) {
            bool useful = false;
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) next item");
            //find peer
            PeerPtr peer = clusterManager.getIdentityManager().getLoadedPeer(it->computer_id);
            //check
            if (peer) {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) peer exists");
                IdentityManager::PeerData old_peer_data = clusterManager.getIdentityManager().getPeerData(peer);
                IdentityManager::PeerData peer_data = old_peer_data;
                //found, update informations about pub & priv interface if not present
                if (peer_data.rsa_public_key != it->rsa_public_key) {
                    //error, different pub key than expected
                    error("Error, a public key is in conflict, abord useServerDatabaseMessage merge.");
                    continue;
                } else {
                    bool modified = false;
                    // if no private interface, set the last good known one.
                    if (!peer_data.private_interface && it->last_private_interface) {
                        modified = true;
                        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) 
                            + " (ClusterAdminMessageManager) create private interface with '"+ it->last_private_interface->first + ":" + it->last_private_interface->second);
                        peer_data.private_interface = { it->last_private_interface->first, it->last_private_interface->second, false };
                    }
                    log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) it has "+it->published_interfaces.size()+" public interfaces");
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
                        useful = true;
                        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) commit changes");
                        clusterManager.getIdentityManager().setPeerData(old_peer_data, peer_data);
                    }
                }
            } else {
                useful = true;
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) peer does not exists");
                //not found, just create the new entry
                IdentityManager::PeerData peer_data;
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) set public key");
                peer_data.rsa_public_key = it->rsa_public_key;
                if (it->last_private_interface) {
                    peer_data.private_interface = IdentityManager::PeerConnection{ it->last_private_interface->first, it->last_private_interface->second, false };
                    log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100)
                    +" (ClusterAdminMessageManager) create private interface with '" + it->last_private_interface->first + ":" + it->last_private_interface->second);
                }
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) it has " + it->published_interfaces.size() + " public interfaces");
                for (auto& pub_interface : it->published_interfaces) {
                    //peer_data.public_interfaces.emplace_back(pub_interface.first, pub_interface.second, false);
                    peer_data.public_interfaces.push_back(IdentityManager::PeerConnection{ pub_interface.first, pub_interface.second, false, {} });
                }
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) add new peer");
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
                    log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ClusterAdminMessageManager) new state = " + new_peer->getState());
                }

                if (peer_data.private_interface && Socket::get_ip_network(peer_data.private_interface->address) == clusterManager.getLocalIPNetwork()) {
                    try_to_connect_with.push_back(new_peer);
                }
            }
            if (useful) {
                data_useful.push_back(*it);
            }
        }

        //propagate our modified entries to other peers
        if(!data_useful.empty()){
            PeerList peers = clusterManager.getPeersCopy();
            ByteBuff buffer = this->createSendServerDatabaseMessage(data_useful);
            for (PeerPtr& peer : peers) {
                if (peer && peer->isAlive() && peer->isConnected()
                    && peer != sender && peer->getComputerId() != sender->getComputerId()
                    && peer != clusterManager.getIdentityManager().getSelfPeer()) {
                    peer->writeMessage(*UnnencryptedMessageType::SEND_SERVER_DATABASE, buffer.rewind());
                }
            }
        }

        return try_to_connect_with;
    }

    std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ClusterAdminMessageManager::createSendServerDatabase(const PeerList& all_peers) {
        std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ret_list;
        // //just give our provate interface and our public ones.
        for (const PeerPtr& peer : all_peers) {
            //double-check the entries, there may be temprorary ones from the clusterManager list.
            // only use the ones that are in our db (or us)
            if (peer->getComputerId() != 0 && peer->getComputerId() != NO_COMPUTER_ID 
                && 0 != (peer->getState() & (Peer::ConnectionState::DATABASE | Peer::ConnectionState::US))) {
                IdentityManager::PeerData data = clusterManager.getIdentityManager().getPeerData(peer);
                ClusterAdminMessageManager::DataSendServerDatabaseItem& item = ret_list.emplace_back();
                item.computer_id = peer->getComputerId();
                item.last_peer_id = peer->getPeerId();
                item.current_state = peer->getState();
                auto aeff_test = clusterManager.getIdentityManager().getSelfPeerData();
                item.rsa_public_key = data.rsa_public_key;
                assert(!item.rsa_public_key.empty());
                if (data.private_interface) {
                    item.last_private_interface = { data.private_interface->address, data.private_interface->port };
                }
                for (const IdentityManager::PeerConnection& connection : data.public_interfaces) {
                    item.published_interfaces.emplace_back(connection.address, connection.port);
                }

                std::string rsa_log_str;
                for (uint8_t c : data.rsa_public_key) {
                    rsa_log_str += u8_hex(c);
                }
                log(std::to_string(clusterManager.getPeerId() % 100) + " (ClusterAdminMessageManager) write database entry: pid:" + (item.last_peer_id % 100)
                    + " cid=" + item.computer_id + " ip=" + (data.private_interface? data.private_interface->address:"") + " port=" + (data.private_interface ? data.private_interface->port : 0) + " pubkey=" + rsa_log_str+ "(" + data.rsa_public_key.size() + ")\n");
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
            buff.serializePeerId(peer.last_peer_id);
            //computer id, to identify the peer.
            buff.serializeComputerId(peer.computer_id);
            //the rsa key to identify it
            buff.putSize(peer.rsa_public_key.size());
            buff.put(peer.rsa_public_key);
            assert(!peer.rsa_public_key.empty());
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

    std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ClusterAdminMessageManager::readSendServerDatabaseMessage(const ByteBuff& buff) {
        std::vector<ClusterAdminMessageManager::DataSendServerDatabaseItem> ret_list;

        std::mutex useServerDatabaseMessage_mutex;
        const size_t nb_peers = buff.getSize();
        log(std::to_string(clusterManager.getPeerId() % 100) + " (ClusterAdminMessageManager) DATABASE entries: " + nb_peers);
        for (size_t idx_peer = 0; idx_peer < nb_peers; ++idx_peer) {
            DataSendServerDatabaseItem& peer = ret_list.emplace_back();
            // the peer id, not that useful, but can help to match with already connected peers
            peer.last_peer_id = buff.deserializePeerId();
            //computer id, to identify the peer.
            peer.computer_id = buff.deserializeComputerId();
            //the rsa key to identify it
            size_t pub_size = buff.getSize();
            peer.rsa_public_key = buff.get(pub_size);
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

            std::string rsa_log_str;
            for (uint8_t c : peer.rsa_public_key) {
                rsa_log_str += u8_hex(c);
            }
            log(std::to_string(clusterManager.getPeerId() % 100) + " (ClusterAdminMessageManager) read database entry: pid:"+ (peer.last_peer_id%100)
                +" cid="+ peer.computer_id+" ip="+ priv_addr+" port="+ priv_port+" pubkey="+ rsa_log_str +"("+ peer.rsa_public_key.size() +")\n");
        }
        return ret_list;
    }

    //void changeOurComputerid() {

    //    || (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::TEMPORARY
    //        && std::find(registered_computer_id.begin(), registered_computer_id.end(), clusterManager->getIdentityManager().getComputerId()) != registered_computer_id.end()
    //}

} // namespace supercloud
