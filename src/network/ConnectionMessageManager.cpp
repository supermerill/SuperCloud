#include "ConnectionMessageManager.hpp"

#include "ClusterManager.hpp"
#include "Peer.hpp"
#include "IdentityManager.hpp"
#include "PhysicalServer.hpp"

namespace supercloud{

    void ConnectionMessageManager::register_listener() {
        clusterManager->registerListener(*UnnencryptedMessageType::CONNECTION_CLOSED, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::GET_SERVER_ID, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::GET_SERVER_LIST, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::GET_VERIFY_IDENTITY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::GET_SERVER_AES_KEY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::GET_CONNECTION_ESTABLISHED, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_ID, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_LIST, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_VERIFY_IDENTITY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_CONNECTION_ESTABLISHED, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::TIMER_SECOND, this->ptr());
    }

    void ConnectionMessageManager::requestCurrentStep(PeerPtr sender, bool enforce) {
        if (!sender->isAlive()) { return; }
        ConnectionStep currentStep = status[sender].current;
        if (currentStep == ConnectionStep::BORN && (enforce || status[sender].last_request != ConnectionStep::ID)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_ID);
            {std::lock_guard lock{ m_status_mutex };
            status[sender].last_request = ConnectionStep::ID;
            status[sender].last_update = get_current_time_milis();
            }
        }
        else if (currentStep == ConnectionStep::ID && (enforce || status[sender].last_request != ConnectionStep::SERVER_LIST)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_LIST);
            {std::lock_guard lock{ m_status_mutex };
            status[sender].last_request = ConnectionStep::SERVER_LIST;
            status[sender].last_update = get_current_time_milis();
            }
        }
        else if (currentStep == ConnectionStep::SERVER_LIST && (enforce || status[sender].last_request != ConnectionStep::RSA)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
            {std::lock_guard lock{ m_status_mutex };
            status[sender].last_request = ConnectionStep::RSA;
            status[sender].last_update = get_current_time_milis();
            }
        }
        else if (currentStep == ConnectionStep::RSA && (enforce || status[sender].last_request != ConnectionStep::IDENTITY_VERIFIED)) {
            IdentityManager::Identityresult result = clusterManager->getIdentityManager().sendIdentity(sender, clusterManager->getIdentityManager().createMessageForIdentityCheck(*sender, false), true);
            if (result == IdentityManager::Identityresult::NO_PUB) {
                sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                {std::lock_guard lock{ m_status_mutex };
                status[sender].last_request = ConnectionStep::RSA;
                status[sender].last_update = get_current_time_milis();
                }
            } else if (result == IdentityManager::Identityresult::BAD) {
                sender->close();
            } else if (result == IdentityManager::Identityresult::OK) {
                {std::lock_guard lock{ m_status_mutex };
                status[sender].last_request = ConnectionStep::IDENTITY_VERIFIED;
                status[sender].last_update = get_current_time_milis();
                }
            }
        }
        else if (currentStep == ConnectionStep::IDENTITY_VERIFIED && (enforce || status[sender].last_request != ConnectionStep::AES)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_AES_KEY);
            {std::lock_guard lock{ m_status_mutex };
            status[sender].last_request = ConnectionStep::AES;
            status[sender].last_update = get_current_time_milis();
            }
        }
        else if (currentStep == ConnectionStep::AES && (enforce || status[sender].last_request != ConnectionStep::CONNECTED)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_CONNECTION_ESTABLISHED);
            {std::lock_guard lock{ m_status_mutex };
            status[sender].last_request = ConnectionStep::CONNECTED;
            status[sender].last_update = get_current_time_milis();
            }
        }
    }

    void ConnectionMessageManager::setStatus(const PeerPtr& peer, const ConnectionStep new_status) {
        std::lock_guard lock{ m_status_mutex };
        if (status[peer].current == ConnectionStep::BORN && new_status > ConnectionStep::BORN) {
            this->m_connection_state.beganConnection();
        }
        status[peer].current = std::max(status[peer].current, new_status);
    }

    void ConnectionMessageManager::receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) {
        log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive message " + messageId + " : " + messageId_to_string(messageId) + "(ConnectionMessageManager)");
        // answer to deletion
        if (!sender->isAlive() || messageId == *UnnencryptedMessageType::CONNECTION_CLOSED) {
            {std::lock_guard lock{ m_status_mutex };
                if (auto status_it = status.find(sender); status_it != status.end()) {
                    bool was_connected = false;
                    if (status_it->second.current == ConnectionStep::CONNECTED) {
                        this->m_connection_state.removeConnection(sender->getComputerId());
                        was_connected = true;
                    } else if(status_it->second.current > ConnectionStep::BORN) {
                        this->m_connection_state.abordConnection();
                    }
                    status.erase(sender);
                } 
            }
            return;
        } else
        if (messageId == *UnnencryptedMessageType::TIMER_SECOND) {
            if (!sender->isConnected()) {
                ConnectionStatus& step = status[sender];
                //if it takes too long, ask again
                int64_t now = get_current_time_milis();
                int64_t diff = now - step.last_update;
                if (diff < 500) {
                    requestCurrentStep(sender, false);
                }else {
                    requestCurrentStep(sender, true);
                }//TODO: if really too long, then abord.
            }
        } else
        if (messageId == *UnnencryptedMessageType::GET_SERVER_ID) {
            assert(sender->getState() & Peer::ConnectionState::CONNECTING);
            // special case, give the peer object directly.
            log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) read GET_SERVER_ID : now sending my peerId");
            sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_ID, create_SEND_SERVER_ID_msg(
                Data_SEND_SERVER_ID{ clusterManager->getPeerId() , clusterManager->getIdentityManager().getClusterId() , clusterManager->getListenPort() }));
        } else
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_ID) {
            assert(sender->getState() & Peer::ConnectionState::CONNECTING);
            //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + "read GET_SERVER_ID : now sending my peerId");
            // add it to the map, if not already here
            if (status.find(sender) == status.end()) { status[sender].current = ConnectionStep::BORN; }
            Data_SEND_SERVER_ID data_msg = get_SEND_SERVER_ID_msg(message);
            // special case, give the peer object directly.
            sender->setPeerId(data_msg.peer_id);
            //get listen port
            sender->setPort(data_msg.port);
            //Verify that the cluster Id is good
            if (data_msg.cluster_id > 0 && clusterManager->getIdentityManager().getClusterId() < 0) {
                //set our cluster id
//						myServer.getIdentityManager().setClusterId(clusterId);
                throw std::runtime_error("Error, we haven't a clusterid !! Can we pick one from an existing network? : not anymore!");
                //						changeState(PeerConnectionState::HAS_ID, true);
            } else if (data_msg.cluster_id > 0 && clusterManager->getIdentityManager().getClusterId() != data_msg.cluster_id) {
                //error, not my cluster!
                std::cerr << std::to_string(clusterManager->getPeerId() % 100) << " Error, trying to connect with " << (sender->getPeerId() % 100) << " but his cluster is " << data_msg.cluster_id << " and mine is "
                    << clusterManager->getIdentityManager().getClusterId() << " => closing connection\n";
                sender->close();
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) read SEND_SERVER_ID : get their id: " + sender->getPeerId());

                setStatus(sender, ConnectionStep::ID);
                //ask for next step
                if (QUICKER_CONNECTION) {
                    if (status[sender].current == ConnectionStep::ID) {
                        sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_LIST,
                            this->create_SEND_SERVER_LIST_msg(this->create_Data_SEND_SERVER_LIST(sender)));
                    }
                } else {
                    requestCurrentStep(sender, false);
                }
            }
        } else
        if (messageId == *UnnencryptedMessageType::GET_SERVER_LIST) {
            if (status[sender].current < ConnectionStep::ID) { // still don't have your id, please give them to me beforehand
                requestCurrentStep(sender, true);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + "he want my server list");
                sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_LIST, 
                    this->create_SEND_SERVER_LIST_msg(this->create_Data_SEND_SERVER_LIST(sender)));
            }
        } else
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_LIST) {
            if (status[sender].current < ConnectionStep::ID) { // still don't have your id, please give them to me beforehand
                requestCurrentStep(sender, true);
            } else {
                assert(sender->getPeerId() != 0);
                assert(sender->getPeerId() != NO_PEER_ID);
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive SEND_SERVER_LIST");
                Data_SEND_SERVER_LIST data_msg = get_SEND_SERVER_LIST_msg(message);
                //now choose computer & peer id
                chooseComputerId(data_msg.registered_computer_id, data_msg.connected_computer_id, data_msg.our_keys);
                choosePeerId(data_msg.registered_peer_id, data_msg.connected_peer_id);
                if (sender->isAlive()) { 
                    setStatus(sender, ConnectionStep::SERVER_LIST);
                    //ask for next step
                    if (QUICKER_CONNECTION) {
                        if (status[sender].current == ConnectionStep::SERVER_LIST) {
                            clusterManager->getIdentityManager().sendPublicKey(sender);
                        }
                    } else {
                        requestCurrentStep(sender, false);
                    }
                }
            }
        } else
        if (messageId == *UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY) {
            if (status[sender].current < ConnectionStep::SERVER_LIST) {
                requestCurrentStep(sender, true);
            } else {
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive GET_SERVER_PUBLIC_KEY");
                clusterManager->getIdentityManager().sendPublicKey(sender);
            }
        } else
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY) {
            if (status[sender].current < ConnectionStep::SERVER_LIST) {
                requestCurrentStep(sender, true);
            } else {
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive SEND_SERVER_PUBLIC_KEY");
                clusterManager->getIdentityManager().receivePublicKey(sender, message);
                if (sender->isAlive()) { setStatus(sender, ConnectionStep::RSA); }
                //ask for next step
                requestCurrentStep(sender, false);
            }
        } else
        if (messageId == *UnnencryptedMessageType::GET_VERIFY_IDENTITY) {
            if (status[sender].current < ConnectionStep::RSA) {
                requestCurrentStep(sender, true);
            } else {
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive GET_VERIFY_IDENTITY");
                IdentityManager::Identityresult result = clusterManager->getIdentityManager().answerIdentity(sender, message);
                if (result == IdentityManager::Identityresult::NO_PUB) {
                    sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                    status[sender].last_request = ConnectionStep::RSA;
                    status[sender].last_update = get_current_time_milis();
                } else if (result == IdentityManager::Identityresult::BAD) {
                    sender->close();
                } else if (result == IdentityManager::Identityresult::OK) {
                    if (sender->isAlive()) { setStatus(sender, ConnectionStep::IDENTITY_VERIFIED); }
                    status[sender].last_request = ConnectionStep::IDENTITY_VERIFIED;
                    status[sender].last_update = get_current_time_milis();
                    // answer is sent inside the  'answerIdentity' method, as it has to reuse the decoded message content.
                }
            }
        } else
        if (messageId == *UnnencryptedMessageType::SEND_VERIFY_IDENTITY) {
            if (status[sender].current < ConnectionStep::RSA) {
                requestCurrentStep(sender, true);
            } else {
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive SEND_VERIFY_IDENTITY");
                IdentityManager::Identityresult result = clusterManager->getIdentityManager().receiveIdentity(sender, message);
                if (result == IdentityManager::Identityresult::NO_PUB) {
                    sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                    status[sender].last_request = ConnectionStep::RSA;
                    status[sender].last_update = get_current_time_milis();
                } else if (result == IdentityManager::Identityresult::BAD) {
                    sender->close();
                } else if (result == IdentityManager::Identityresult::OK) {
                    if (sender->isAlive()) { setStatus(sender, ConnectionStep::IDENTITY_VERIFIED); }
                    //ask for next step
                    std::string rsa_log_str;
                    for (uint8_t c : clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key) {
                        rsa_log_str += u8_hex(c);
                    }
                    log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) rsa ok: '"+ rsa_log_str +"'");
                    assert(!clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key.empty());
                    //ask for next step (only if i'm feeling it, to avoid unneeded conflicts and connection delays)
                    if (QUICKER_CONNECTION && sender->getPeerId() < clusterManager->getPeerId()) {
                        log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager)  I AM THE LEADER? I WILL ASK FOR PUB KEY");
                        requestCurrentStep(sender, false);
                    }//else, even if nothing is send by the other peer, the timer event should continue the connection.
                    else {
                        log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager)  i am not the leader, i will shut up");

                    }
                }
            }
        } else
        if (messageId == *UnnencryptedMessageType::GET_SERVER_AES_KEY) {
            assert(sender->getComputerId() != 0);
            assert(sender->getComputerId() != NO_COMPUTER_ID);
            assert(!clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key.empty());
            if (status[sender].current < ConnectionStep::IDENTITY_VERIFIED || sender->getComputerId() == 0
                || sender->getComputerId() == NO_COMPUTER_ID || clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key.empty()) {
                requestCurrentStep(sender, true);
            } else {
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive GET_SERVER_AES_KEY");
                clusterManager->getIdentityManager().sendAesKey(sender, IdentityManager::AES_PROPOSAL);
            }
        } else
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_AES_KEY) {
            if (status[sender].current < ConnectionStep::IDENTITY_VERIFIED) {
                requestCurrentStep(sender, true);
            } else {
                //log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) receive SEND_SERVER_AES_KEY");
                bool valid = clusterManager->getIdentityManager().receiveAesKey(sender, message);
                if (valid && sender->isAlive()) { 
                    log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) AES ok!");
                    assert(!clusterManager->getIdentityManager().getPeerData(sender).aes_key.empty());
                    //update this manager status for this peer (track the connection progress)
                    this->setStatus(sender, ConnectionStep::AES);
                    //ask for next step
                    if (QUICKER_CONNECTION) {
                        if (status[sender].current == ConnectionStep::AES) {
                            sender->writeMessage(*UnnencryptedMessageType::SEND_CONNECTION_ESTABLISHED);
                        }
                    } else {
                        requestCurrentStep(sender, false);
                    }
                }
            }
        } else
        if (messageId == *UnnencryptedMessageType::GET_CONNECTION_ESTABLISHED) {
            if (status[sender].current < ConnectionStep::AES) {
                requestCurrentStep(sender, true);
            } else if (sender->getComputerId() == 0 || sender->getComputerId() == NO_COMPUTER_ID
                || clusterManager->getIdentityManager().getPeerData(sender).aes_key.size() == 0) {
                status[sender].current = ConnectionStep::RSA;
                requestCurrentStep(sender, true);
            }else{
                sender->writeMessage(*UnnencryptedMessageType::SEND_CONNECTION_ESTABLISHED);
            }
        } else
        if (messageId == *UnnencryptedMessageType::SEND_CONNECTION_ESTABLISHED) {
            if (status[sender].current < ConnectionStep::AES) {
                requestCurrentStep(sender, true);
            } else if(!sender->isConnected()){
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " (ConnectionMessageManager) SUCESSFUL connection: receive confirmation of aes.");
                status[sender].current = ConnectionStep::CONNECTED;
                auto aeff_test = clusterManager->getIdentityManager().getPeerData(sender);
                assert(!clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key.empty());
                assert(!clusterManager->getIdentityManager().getPeerData(sender).aes_key.empty());
                assert(clusterManager->getIdentityManager().getPeerData(sender).peer == sender);
                //update the clusterManager connection status (track the connection of our computer to the network)
                // clusterManager.state.finishConnection(sender->getComputerId())
                this->m_connection_state.finishConnection(sender->getComputerId());

                //find if this peer isn't already loaded
                clusterManager->getIdentityManager().fusionWithConnectedPeer(sender);
                assert(!clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key.empty());
                assert(!clusterManager->getIdentityManager().getPeerData(sender).aes_key.empty());
                assert(clusterManager->getIdentityManager().getPeerData(sender).peer == sender);
                if (sender->initiatedByMe()) {
                    // save this successful connection
                    IdentityManager::PeerData old_peer_data = clusterManager->getIdentityManager().getPeerData(sender);
                    IdentityManager::PeerData peer_data = old_peer_data;
                    // get local ip
                    peer_data.private_interface = IdentityManager::PeerConnection{ sender->getIP(), sender->getPort(), true, {sender->getLocalIPNetwork()} };
                    clusterManager->getIdentityManager().setPeerData(old_peer_data, peer_data);
                }
                assert(!clusterManager->getIdentityManager().getPeerData(sender).rsa_public_key.empty());
                assert(!clusterManager->getIdentityManager().getPeerData(sender).aes_key.empty());
                assert(clusterManager->getIdentityManager().getPeerData(sender).peer == sender);

                //update the peer connection status (track the connectivity of a single peer)
                // now other manager can emit message to the peer, and be notified by timers.
                {
                    std::lock_guard lock{ sender->synchronize() };
                    Peer::ConnectionState state = sender->getState();
                    assert(!(state & Peer::ConnectionState::US));
                    assert(state & Peer::ConnectionState::CONNECTING);
                    //from connecting to connected
                    state = (state & ~Peer::ConnectionState::CONNECTING) | Peer::ConnectionState::CONNECTED;
                    //from temporary to 'inside the db'
                    state = (state & ~Peer::ConnectionState::TEMPORARY) | Peer::ConnectionState::DATABASE;
                    sender->setState(state);
                }

                //notify peers that a connection is established.
                clusterManager->propagateMessage(sender, *UnnencryptedMessageType::NEW_CONNECTION, ByteBuff{});
            }
        }
    }

    void ConnectionMessageManager::chooseComputerId(const std::unordered_set<ComputerId>& registered_computer_id, const std::unordered_set<ComputerId>& connected_computer_id,
        const std::unordered_map<ComputerId, PublicKey>& known_keys) {
        log(std::to_string(clusterManager->getPeerId() % 100) + " Start chooseComputerId=" + clusterManager->getComputerId() + " " + clusterManager->getIdentityManager().getComputerIdState());
        // do not run this method in multi-thread (i use getIdentityManager() because i'm the only one to use it, to avoid possible dead sync issues)
        //std::lock_guard choosecomplock(getIdentityManager().synchronize());
         std::lock_guard choosecomplock(clusterManager->getIdentityManager().synchronize());

         const ComputerId my_current_computer_id = clusterManager->getComputerId();
        // check if we have a computerId
         bool need_new_id = clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::NOT_CHOOSEN 
             || my_current_computer_id == NO_COMPUTER_ID || my_current_computer_id == 0;
         bool has_confict = false;
         if (!need_new_id) {
             need_new_id = clusterManager->getIdentityManager().getPublicKey().empty();
         }
         if (!need_new_id) {
             bool our_id_is_here = std::find(registered_computer_id.begin(), registered_computer_id.end(), my_current_computer_id) != registered_computer_id.end();
             //check for conflict & our is "temporary" (ie recent & not used to do anything yet)
             if (auto mykey2mypub = known_keys.find(my_current_computer_id); our_id_is_here && mykey2mypub != known_keys.end()) {
                 has_confict = mykey2mypub->second != clusterManager->getIdentityManager().getPublicKey();
                 if (has_confict) {
                     log(std::to_string(clusterManager->getPeerId() % 100) + " I need a new ComputerId=" + clusterManager->getComputerId()
                         + " because it's already choosen by a peer with a pub key of" + to_hex(mykey2mypub->second)+" and mine is "+ to_hex(clusterManager->getIdentityManager().getPublicKey()));
                 }
             } else {
                 //check if our computerid is inside the list, but without public keys
                 ///wathever, ignore it. We won't change our key & computer id just for a fluke.
                 log(std::to_string(clusterManager->getPeerId() % 100) + " I don't need a new ComputerId=" + clusterManager->getComputerId() + " " + clusterManager->getIdentityManager().getComputerIdState());
             }
         } else {
             log(std::to_string(clusterManager->getPeerId() % 100) + " I need a new ComputerId=" + clusterManager->getComputerId() + " " + clusterManager->getIdentityManager().getComputerIdState());
             if (clusterManager->getIdentityManager().getComputerIdState() != IdentityManager::ComputerIdState::NOT_CHOOSEN) {
                 error("you shouldn't already need to change your computer id... ");
                 has_confict = true;
             }
         }
        if (need_new_id || (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::TEMPORARY && has_confict)) {
            //choose a random one and set our to "TEMPORARY"

            ComputerId choosenId = ComputerId(rand_u63());
            // while it's already taken
            const size_t MAX_ITERATION = 1000;
            size_t iteration = 0;
            //first, try to get a random one
            while ( (choosenId == 0 || choosenId == NO_COMPUTER_ID || std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end())
                && iteration < MAX_ITERATION) {
                log(std::to_string(clusterManager->getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = ComputerId(rand_u63());
                iteration++;
            }
            //if still not good
            if (std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end()) {
                //choose the first one
                for (choosenId = 0;
                    choosenId < std::numeric_limits<ComputerId>::max() - 2
                    && (choosenId == 0 || choosenId == NO_COMPUTER_ID || std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end());
                    ++choosenId) {
                }
            }
            if (choosenId >= std::numeric_limits<ComputerId>::max() - 2) {
                if (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::TEMPORARY) {
                    error(std::to_string(clusterManager->getPeerId() % 100) + " ERROR! my ComputerId is already taken, and I can't change as they are all taken.\n");
                    throw std::runtime_error("ERROR! my ComputerId is already taken, and I can't change as they are all taken.. Please destroy this instance/peer/server and reuse another one.");
                } else {
                    error(std::to_string(clusterManager->getPeerId() % 100) + " ERROR! no computerId left to be taken.\n");
                    throw std::runtime_error("ERROR! no computerId left to be taken. Please reuse one instead.");
                }
            }
            if (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::TEMPORARY) {
                clusterManager->getIdentityManager().setComputerId(choosenId, IdentityManager::ComputerIdState::TEMPORARY);
                //disconnect and reconnect already connected peers, so we cand change our computerid
                reconnectWithNewComputerId();
            } else {
                clusterManager->getIdentityManager().setComputerId(choosenId, IdentityManager::ComputerIdState::TEMPORARY);
            }
            log(std::to_string(clusterManager->getPeerId() % 100) + " Choose my new ComputerId=" + clusterManager->getComputerId() + "\n");

            if (has_confict) {
                //reconnect
                error(std::to_string(clusterManager->getPeerId() % 100) + " ERROR! my ComputerId was already taken. Trying with a reconnect to get back on track.\n");
                clusterManager->reconnect();
            }
        } 
        else {
            if (has_confict) {
                m_connection_state.setOurComputerIdInvalid();
                clusterManager->close();
                //disconnect and try again with a reconnect
                error(std::to_string(clusterManager->getPeerId() % 100) + " ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.\n");
                throw std::runtime_error("ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.");
            }
        }

    }

    void ConnectionMessageManager::reconnectWithNewComputerId() {
        //in all connect connection (shouldn't be many, as itt's still a temporary one)
        PeerList peers = clusterManager->getPeersCopy();
        for (PeerPtr& peer : peers) {
            if (peer->isConnected()) {
                //TODO: emit REVOKE_COMPUTER_ID.
                //for now, we just ask for reconnect or we reconnect ourself.
                if (peer->initiatedByMe()) {
                    const uint16_t port = peer->getPort();
                    const std::string address = peer->getIP();
                    peer->close();
                    const std::shared_ptr<PhysicalServer> ptr = clusterManager;
                    std::thread reconnectThread([ptr, peer, address, port]() {
                        //wait a bit for the disconnect to be in effect
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        //reconnect
                        ptr->connectTo(peer, address, port, 2000);
                    });
                    reconnectThread.detach();
                }
            }
        }
    }

    void ConnectionMessageManager::choosePeerId(const std::unordered_set<PeerId>& registered_peer_id, const std::unordered_map<PeerId, ComputerId>& connected_pid_2_cid) {
        //do not run in multi-thread
        std::lock_guard choosecomplock(clusterManager->getIdentityManager().synchronize());

        bool need_new_id = !clusterManager->hasPeerId();
        bool need_to_reconnect = need_new_id;
        if (!need_new_id){
            if (auto it = connected_pid_2_cid.find(clusterManager->getPeerId()); it != connected_pid_2_cid.end()) {
                //is it me?
                if (it->second != clusterManager->getComputerId()) {
                    error("collision: i choose a peer id already taken");
                    need_new_id = true;
                    need_to_reconnect = true;
                }
            }
        }
        if (need_new_id) {

            PeerId choosenId = clusterManager->getPeerId();
            // while it's already taken
            const size_t MAX_ITERATION = 1000;
            size_t iteration = 0;
            //first, try to get a random one
            while ( (choosenId == 0 || choosenId== NO_PEER_ID || registered_peer_id.find(choosenId) != registered_peer_id.end())
                && iteration < MAX_ITERATION) {
                log(std::to_string(clusterManager->getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = rand_u63();
                iteration++;
            }
            //then, get a random one (hopefully, the random generator can output all values)
            while (choosenId == 0 || choosenId == NO_PEER_ID || connected_pid_2_cid.find(choosenId) != connected_pid_2_cid.end()) {
                log(std::to_string(clusterManager->getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = rand_u63();
            }

            clusterManager->setPeerId(choosenId);
        }
        if (need_to_reconnect) {
            clusterManager->reconnect();
        }
    }

    ConnectionMessageManager::Data_SEND_SERVER_LIST ConnectionMessageManager::get_SEND_SERVER_LIST_msg(ByteBuff& message) {
        Data_SEND_SERVER_LIST data;
        // == the connected pid->cid  ==
        size_t nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            PeerId pid = message.deserializePeerId();
            ComputerId cid = message.deserializeComputerId();
            data.connected_peer_id[pid] = cid;
            data.connected_computer_id.insert(cid);
        }
        // == then, the registered peerIds ==
        nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            data.registered_peer_id.insert(message.deserializePeerId());
        }
        // == then, the registered computerIds ==
        nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            data.registered_computer_id.insert(message.deserializeComputerId());
        }
        // == last, the public keys of interest (our and the peer's one if we have a computerid).
        nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            ComputerId cid = message.deserializeComputerId();
            size_t keysize = message.getSize();
            data.our_keys[cid] = message.get(keysize);
        }
        return data;
    }

    ConnectionMessageManager::Data_SEND_SERVER_LIST ConnectionMessageManager::create_Data_SEND_SERVER_LIST(PeerPtr other) {
        IdentityManager& idm = this->clusterManager->getIdentityManager();
        const std::vector<PeerPtr>& registered = idm.getLoadedPeers();
        const PeerList& connected = this->clusterManager->getPeersCopy();
        Data_SEND_SERVER_LIST data;
        // == put our id if any ==
        //TODO: use getComputerIdState instead of NO_COMPUTER_ID
        bool has_id = idm.getComputerIdState() != IdentityManager::ComputerIdState::NOT_CHOOSEN || clusterManager->getComputerId() != NO_COMPUTER_ID;
        // == first, the registered computer (and peer) ids (also with me if i was/is connected) ==
        if (has_id) {
            assert(clusterManager->getComputerId() != 0);
            assert(clusterManager->getComputerId() != NO_COMPUTER_ID);
            assert(clusterManager->getPeerId() != 0);
            assert(clusterManager->getPeerId() != NO_PEER_ID);
            data.registered_computer_id.insert(clusterManager->getComputerId());
            data.registered_peer_id.insert(clusterManager->getPeerId());
        }
        for (const PeerPtr& peer : registered) {
            // also send entry points? i don't think if it's something desirable.
            if (!(peer->getState() & Peer::ENTRY_POINT)) {
                assert(peer->getComputerId() != 0);
                assert(peer->getComputerId() != NO_COMPUTER_ID);
                assert(peer->getState() & Peer::DATABASE);
                assert(!(peer->getState() & Peer::TEMPORARY));
                assert(!(peer->getState() & Peer::US));
                data.registered_computer_id.insert(peer->getComputerId());
                //the peer can have no pid if it's not connected.
                if (peer->getPeerId() != 0 && peer->getPeerId() != NO_PEER_ID) {
                    data.registered_peer_id.insert(peer->getPeerId());
                }
            }
        }
        // == then, the connected peerIds ==
        if (has_id && clusterManager->getState().isConnected()) {
            assert(clusterManager->getPeerId() != 0);
            assert(clusterManager->getPeerId() != NO_PEER_ID);
            // my cid can be bad, if it's my first connection
            data.registered_computer_id.insert(clusterManager->getComputerId());
            data.registered_peer_id.insert(clusterManager->getPeerId());
        }
        for (const PeerPtr& peer : connected) {
            if (peer->isConnected()) {
                assert(peer->getPeerId() != 0);
                assert(peer->getPeerId() != NO_PEER_ID);
                //cid can be bad, if the peer isn't connected yet.
                data.connected_computer_id.insert(peer->getComputerId());
                data.connected_peer_id[peer->getPeerId()] = peer->getComputerId();
            }
        }
        //add our key
        if (clusterManager->getComputerId() != 0 && clusterManager->getComputerId() != NO_COMPUTER_ID && !idm.getPublicKey().empty() 
            && clusterManager->getIdentityManager().getComputerIdState() != IdentityManager::ComputerIdState::NOT_CHOOSEN) {
            data.our_keys[clusterManager->getComputerId()] = idm.getPublicKey();
        }
        //add its key
        if (other && other->getComputerId() != 0 && other->getComputerId() != NO_COMPUTER_ID) {
            PeerPtr other_in_our_db = idm.getLoadedPeer(other->getComputerId()); // should be the same as 'other', but maybe not if they initiated the connection.
            if (other_in_our_db && idm.hasPeerData(other_in_our_db)) {
                IdentityManager::PeerData other_data = idm.getPeerData(other_in_our_db);
                if (!other_data.rsa_public_key.empty()) {
                    data.our_keys[clusterManager->getComputerId()] = idm.getPublicKey();
                }
            }
        }
        return data;
    }

    ByteBuff ConnectionMessageManager::create_SEND_SERVER_LIST_msg(Data_SEND_SERVER_LIST& data) {
        ByteBuff buff;
        // == the connected peerIds->computerids  ==
        buff.putSize(data.connected_peer_id.size());
        for (const auto& pid2cid : data.connected_peer_id) {
            buff.serializePeerId(pid2cid.first);
            buff.serializeComputerId(pid2cid.second);
        }
        // == then, the registered peerIds ==
        buff.putSize(data.registered_peer_id.size());
        for (PeerId peerid : data.registered_peer_id) {
            buff.serializePeerId(peerid);
        }
        // == then, the registered computerIds ==
        buff.putSize(data.registered_computer_id.size());
        for (ComputerId computerid : data.registered_computer_id) {
            buff.serializeComputerId(computerid);
        }
        // == last, the public keys of interest (our and the peer's one if we have a computerid).
        buff.putSize(data.our_keys.size());
        for (const auto& entry : data.our_keys) {
            buff.serializeComputerId(entry.first);
            buff.putSize(entry.second.size()).put(entry.second);
        }
        buff.flip();
        return buff;
    }


    //void ConnectionMessageManager::sendServerId(Peer& peer) {
    ByteBuff ConnectionMessageManager::create_SEND_SERVER_ID_msg(ConnectionMessageManager::Data_SEND_SERVER_ID& data) {
        ByteBuff buff;
        buff.serializePeerId(data.peer_id);
        buff.putULong(data.cluster_id);
        buff.putInt(data.port);
        return buff.flip();
    }
    ConnectionMessageManager::Data_SEND_SERVER_ID ConnectionMessageManager::get_SEND_SERVER_ID_msg(ByteBuff& message) {
        Data_SEND_SERVER_ID data;
        // his peer id
        data.peer_id = message.deserializePeerId();
        //the cluster id
        data.cluster_id = message.getULong();
        //his listen port
        data.port = (uint16_t(message.getInt()));
        return data;
    }


} // namespace supercloud
