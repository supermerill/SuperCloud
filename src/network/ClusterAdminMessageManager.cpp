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
            {
                // save this successful connection
                IdentityManager::PeerData old_peer_data = clusterManager.getIdentityManager().getPeerData(sender->getComputerId());
                IdentityManager::PeerData peer_data = old_peer_data;
                peer_data.peer = sender; //already done by fusionWithConnectedPeer
                peer_data.net2validAddress[sender->getIPNetwork()].push_back(IdentityManager::PeerConnection{ sender->getIP(), sender->getPort(), true, sender->initiatedByMe() });
                //TODO tidy net2validAddress (remove duplicate, etc)
                clusterManager.getIdentityManager().setPeerData(old_peer_data, peer_data);
            }

            //propagate this new information to other peers
            std::vector<PeerPtr> peers = clusterManager.getPeersCopy();
            for (PeerPtr& peer : peers) {
                if (peer && peer->isAlive() && peer->isConnected() && peer != sender && peer->getComputerId() != sender->getComputerId()) {
                    emitServerDatabase(*peer, {sender});
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_DATABASE) {
            emitServerDatabase(*sender, this->clusterManager.getIdentityManager().getLoadedPeers());
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_DATABASE) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " received SEND_SERVER_LIST");
            readServerDatabaseMessage(message);
        }
        if (messageId == *UnnencryptedMessageType::TIMER_MINUTE) {
            //try to connect to some nice peers that aren't connected yet
            //TODO

        }
    }

    void ClusterAdminMessageManager::readServerDatabaseMessage(ByteBuff& msg) {

        std::vector<PeerPtr> loaded_peers = clusterManager.getIdentityManager().getLoadedPeers();
        size_t nb_entries = msg.getSize();
        for (size_t idx = 0; idx < nb_entries; ++idx) {
            uint64_t peerid = msg.getULong();
            uint16_t computerId = msg.getUShort();
            PublicKey rsa_pub_key = msg.getUTF8();
            std::vector<std::pair<std::string, IdentityManager::PeerConnection>> connections;
            size_t nb_conn = msg.getSize();
            for (size_t idx_conn = 0; idx_conn < nb_conn; ++idx_conn) {
                std::string network = msg.getUTF8();
                std::string ip = msg.getUTF8();
                uint16_t port = msg.getShort();
                uint8_t flags = msg.get();
                connections.push_back({ network, IdentityManager::PeerConnection{ip, port, (flags & 1)!=0, (flags & 2) != 0} });
            }
            // check if we have this peer already in our database
            Peer* found = nullptr;
            for(PeerPtr& peer: loaded_peers) {
                if (peer->getComputerId() == computerId) {
                    //we have it
                    found = peer.get();
                    break;
                }
            }
            if (found) {
                // check if the rsa_pub_key is the same
                IdentityManager::PeerData old_peer_data = clusterManager.getIdentityManager().getPeerData(found->getComputerId());
                IdentityManager::PeerData peer_data = old_peer_data;
                if (peer_data.peer && peer_data.peer.get() == found) {
                    if (peer_data.rsa_public_key == rsa_pub_key) {
                        //merge the networks
                        for (auto& connection : connections) {
                            //check if we have this network
                            if (peer_data.net2validAddress.find(connection.first) != peer_data.net2validAddress.end()) {
                                //we have it, only add it if it's first-class information
                                if (connection.second.first_hand_information && connection.second.intitiated_by_me) {
                                    // add it if not already here
                                    std::vector<IdentityManager::PeerConnection>& stored = peer_data.net2validAddress[connection.first];
                                    auto& found_it = std::find_if(stored.begin(), stored.end(), [&connection](IdentityManager::PeerConnection& obj) {return obj.address == connection.second.address && obj.port == connection.second.port; });
                                    if (found_it == stored.end()) {
                                        stored.push_back(IdentityManager::PeerConnection{ connection.second.address, connection.second.port, false, false });
                                    }
                                }
                            } else {
                                //add it
                                peer_data.net2validAddress[connection.first].push_back(IdentityManager::PeerConnection{ connection.second.address, connection.second.port, false, false });
                            }
                        }
                    } else {
                        //not the same pub key => not the real computer!
                        error(std::string("warn: can't add information of computer '") + computerId + "' in our database because it has a different public rsa key. It's not the same!");
                        //TODO if more than 50% of the peers aggree that it's the right one, update our database to remove our wrong pubkey-computerid association.
                    }
                } else {
                    // bad, skip
                    error(std::string("warn: can't add information of computer '") + computerId+"' in our database because it is in weird state");
                }
            } else {
                //create it
                IdentityManager::PeerData data;
                data.rsa_public_key = rsa_pub_key;
                for (auto& connection : connections) {
                    //create the net2validAddress entry
                    data.net2validAddress[connection.first].push_back(IdentityManager::PeerConnection{ connection.second.address, connection.second.port, false, false });
                }
                clusterManager.getIdentityManager().addNewPeer(computerId, data);
            }
        }

    }

    void ClusterAdminMessageManager::emitServerDatabase(Peer& sendTo, const std::vector<PeerPtr>& registered) {
        ByteBuff buff;
        // == our database ==
        buff.putSize(registered.size());
        for (const PeerPtr& peer : registered) {
            // the peer id, not that useful, but can help to match with already connected peers
            buff.putULong(peer->getPeerId());
            //computer id, to identify the peer.
            buff.putUShort(peer->getComputerId());
            IdentityManager::PeerData peer_data = clusterManager.getIdentityManager().getPeerData(peer->getComputerId());
            //the rsa key to identify it
            buff.putUTF8(peer_data.rsa_public_key);
            //only send data we succeeed with
            std::vector<std::pair<std::string, IdentityManager::PeerConnection>> toSend;
            for (const auto& net2connections : peer_data.net2validAddress) {
                auto& it = net2connections.second.end();
                while (it > net2connections.second.begin()) {
                    //reverse iteration to find the most recent information first.
                    --it;
                    if (it->first_hand_information && it->intitiated_by_me) {
                        toSend.push_back({ net2connections.first, *it });
                        //only the best information first
                        break;
                    }
                }
            }
            if (toSend.empty()) {
                //send at least one thing, even if we don't tested it
                for (const auto& net2connections : peer_data.net2validAddress) {
                    auto& it = net2connections.second.end();
                    while (it > net2connections.second.begin()) {
                        --it;
                        if (it->first_hand_information) {
                            toSend.push_back({ net2connections.first, *it });
                            break;
                        }
                    }
                    if (!toSend.empty()) break;
                }
            }
            if (toSend.empty()) {
                //send at least one thing, even if we are not the one who get it.
                for (const auto& net2connections : peer_data.net2validAddress) {
                    auto& it = net2connections.second.end();
                    while (it > net2connections.second.begin()) {
                        --it;
                        toSend.push_back({ net2connections.first, *it });
                        break;
                    }
                    if (!toSend.empty()) break;
                }
            }
            buff.putSize(toSend.size());
            for (std::pair<std::string, IdentityManager::PeerConnection>& net2ip : toSend) {
                buff.putUTF8(net2ip.first);
                buff.putUTF8(net2ip.second.address);
                buff.putShort(net2ip.second.port);
                buff.put(uint8_t(net2ip.second.first_hand_information?1:0 + net2ip.second.intitiated_by_me ? 2 : 0));
            }
        }
        buff.flip();
        sendTo.writeMessage(*UnnencryptedMessageType::SEND_SERVER_DATABASE, buff);
    }


    //			System.out.println(p.getMyServer().getId()%100+" read "+myId+" for "+p.getKey().getOtherServerId()%100);
    //add this id in our list, to be sure we didn't use it and we can transmit it.
    //{
    //    std::lock_guard lock(this->clusterManager.getIdentityManager().synchronize());
    //    log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive peer computerId:  " + senderComputerId );
    //    this->clusterManager.getIdentityManager().addPeer(senderComputerId);
    //}
    //size_t nb = message.getTrailInt();
    //for (int i = 0; i < nb; i++) {
    //    long id = message.getLong();
    //    std::string ip = message.getUTF8();
    //    int port = message.getTrailInt();
    //    short computerId = message.getShort();

    //    //add this id in our list, to be sure we didn't use it and we can transmit it.
    //    {
    //        std::lock_guard lock(this->clusterManager.getIdentityManager().synchronize());
    //        log(std::to_string(clusterManager.getPeerId() % 100) + " receive a distant computerId:  " + computerId + " of " + id % 100 );
    //        this->clusterManager.getIdentityManager().addPeer(computerId);
    //    }
    //    //				System.out.println(p.getMyServer().getId()%100+" i have found "+ip+":"+ port);


    //    //					InetSocketAddress addr = new InetSocketAddress(ip,port);
    //    if (computerId > 0 && computerId != clusterManager.getComputerId() && computerId != senderComputerId
    //        && id != clusterManager.getPeerId() && id != sender->getPeerId()) {
    //        //note: connectTO can throw exception if it can't be joinable.
    //        //new Thread(()->clusterManager.connectTo(ip, port)).start();
    //        //std::thread t([clusterManager&, ip&, port]() { clusterManager.connectTo(ip, port); });
    //        std::thread t([this, ip, port]() { this->clusterManager.connect(ip, port); });
    //        t.detach();
    //    }
    //}
    //clusterManager.getIdentityManager().addToReceivedServerList(sender);

} // namespace supercloud
