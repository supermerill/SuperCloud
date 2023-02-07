#include "ConnectionMessageManager.hpp"
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
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_ID, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_LIST, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_VERIFY_IDENTITY, this->ptr());
        clusterManager->registerListener(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, this->ptr());
    }

    void ConnectionMessageManager::requestCurrentStep(PeerPtr sender, bool enforce) {
        if (!sender->isAlive()) { return; }
        ConnectionStep currentStep = status[sender].recv;
        if (currentStep == ConnectionStep::BORN && (enforce || status[sender].last_request != ConnectionStep::ID)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_ID);
            {std::lock_guard lock{ status_mutex };
            status[sender].last_request = ConnectionStep::ID;
            }
        }
        else if (currentStep == ConnectionStep::ID && (enforce || status[sender].last_request != ConnectionStep::SERVER_LIST)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_LIST);
            {std::lock_guard lock{ status_mutex };
            status[sender].last_request = ConnectionStep::SERVER_LIST;
            }
        }
        else if (currentStep == ConnectionStep::SERVER_LIST && (enforce || status[sender].last_request != ConnectionStep::RSA)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
            {std::lock_guard lock{ status_mutex };
            status[sender].last_request = ConnectionStep::RSA;
            }
        }
        else if (currentStep == ConnectionStep::RSA && (enforce || status[sender].last_request != ConnectionStep::IDENTITY_VERIFIED)) {
            IdentityManager::Identityresult result = clusterManager->getIdentityManager().sendIdentity(*sender, clusterManager->getIdentityManager().createMessageForIdentityCheck(*sender, false), true);
            if (result == IdentityManager::Identityresult::NO_PUB) {
                sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                {std::lock_guard lock{ status_mutex };
                status[sender].last_request = ConnectionStep::RSA;
                }
            } else if (result == IdentityManager::Identityresult::BAD) {
                sender->close();
            } else if (result == IdentityManager::Identityresult::OK) {
                {std::lock_guard lock{ status_mutex };
                status[sender].last_request = ConnectionStep::IDENTITY_VERIFIED;
                }
            }
        }
        else if (currentStep == ConnectionStep::IDENTITY_VERIFIED && (enforce || status[sender].last_request != ConnectionStep::AES)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_AES_KEY);
            {std::lock_guard lock{ status_mutex };
            status[sender].last_request = ConnectionStep::AES;
            }
        }
    }

    void ConnectionMessageManager::setStatus(const PeerPtr& peer, const ConnectionStep new_status) {
        std::lock_guard lock{ status_mutex };
        if (status[peer].recv == ConnectionStep::BORN && new_status > ConnectionStep::BORN) {
            this->m_connection_state.beganConnection();
        }
        status[peer].recv = std::max(status[peer].recv, new_status);
    }

    void ConnectionMessageManager::receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) {
        // answer to deletion
        if (!sender->isAlive() || messageId == *UnnencryptedMessageType::CONNECTION_CLOSED) {
            {std::lock_guard lock{ status_mutex };
                if (auto status_it = status.find(sender); status_it != status.end()) {
                    bool was_connected = false;
                    if (status_it->second.recv == ConnectionStep::AES) {
                        this->m_connection_state.removeConnection(sender->getComputerId());
                        was_connected = true;
                    } else if(status_it->second.recv > ConnectionStep::BORN) {
                        this->m_connection_state.abordConnection();
                    }
                    status.erase(sender);
                } 
            }
            return;
        }
        log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive message " + messageId+" : " + messageId_to_string(messageId) + "(ConnectionMessageManager)");
        if (messageId == *UnnencryptedMessageType::GET_SERVER_ID) {
            // special case, give the peer object directly.
            log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + "read GET_SERVER_ID : now sending my peerId");
            sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_ID, create_SEND_SERVER_ID_msg(
                Data_SEND_SERVER_ID{ clusterManager->getPeerId() , clusterManager->getIdentityManager().getClusterId() , clusterManager->getListenPort() }));
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_ID) {
            // add it to the map, if not already here
            if (status.find(sender) == status.end()) { status[sender].recv = ConnectionStep::BORN; }
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
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " read SEND_SERVER_ID : get their id: " + sender->getPeerId());

                setStatus(sender, ConnectionStep::ID);
                //ask for next step
                requestCurrentStep(sender, false);
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_LIST) {
            if (status[sender].recv < ConnectionStep::ID) { // still don't have your id, please give them to me beforehand
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + "he want my server list");
                sender->writeMessage(*UnnencryptedMessageType::SEND_SERVER_LIST, 
                    this->create_SEND_SERVER_LIST_msg(this->create_Data_SEND_SERVER_LIST()));
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_LIST) {
            if (status[sender].recv < ConnectionStep::ID) { // still don't have your id, please give them to me beforehand
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_SERVER_LIST");
                Data_SEND_SERVER_LIST data_msg = get_SEND_SERVER_LIST_msg(message);
                //now choose computer & peer id
                chooseComputerId(data_msg.registered_computer_id, data_msg.connected_computer_id);
                choosePeerId(data_msg.registered_peer_id, data_msg.connected_peer_id);
                if (sender->isAlive()) { 
                    setStatus(sender, ConnectionStep::SERVER_LIST);
                    //ask for next step
                    requestCurrentStep(sender, false);
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY) {
            if (status[sender].recv < ConnectionStep::SERVER_LIST) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive GET_SERVER_PUBLIC_KEY");
                clusterManager->getIdentityManager().sendPublicKey(*sender);
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY) {
            if (status[sender].recv < ConnectionStep::SERVER_LIST) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_SERVER_PUBLIC_KEY");
                clusterManager->getIdentityManager().receivePublicKey(*sender, message);
                if (sender->isAlive()) { setStatus(sender, ConnectionStep::RSA); }
                //ask for next step
                requestCurrentStep(sender, false);
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_VERIFY_IDENTITY) {
            if (status[sender].recv < ConnectionStep::RSA) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive GET_VERIFY_IDENTITY");
                IdentityManager::Identityresult result = clusterManager->getIdentityManager().answerIdentity(*sender, message);
                if (result == IdentityManager::Identityresult::NO_PUB) {
                    sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                    status[sender].last_request = ConnectionStep::RSA;
                } else if (result == IdentityManager::Identityresult::BAD) {
                    sender->close();
                } else if (result == IdentityManager::Identityresult::OK) {
                    if (sender->isAlive()) { setStatus(sender, ConnectionStep::IDENTITY_VERIFIED); }
                    status[sender].last_request = ConnectionStep::IDENTITY_VERIFIED;
                    // answer is sent inside the  'answerIdentity' method, as it has to reuse the decoded message content.
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_VERIFY_IDENTITY) {
            if (status[sender].recv < ConnectionStep::RSA) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_VERIFY_IDENTITY");
                IdentityManager::Identityresult result = clusterManager->getIdentityManager().receiveIdentity(sender, message);
                if (result == IdentityManager::Identityresult::NO_PUB) {
                    sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                    status[sender].last_request = ConnectionStep::RSA;
                } else if (result == IdentityManager::Identityresult::BAD) {
                    sender->close();
                } else if (result == IdentityManager::Identityresult::OK) {
                    if (sender->isAlive()) { setStatus(sender, ConnectionStep::IDENTITY_VERIFIED); }
                    //ask for next step
                    requestCurrentStep(sender, false);
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_AES_KEY) {
            if (status[sender].recv < ConnectionStep::IDENTITY_VERIFIED) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive GET_SERVER_AES_KEY");
                clusterManager->getIdentityManager().sendAesKey(*sender, IdentityManager::AES_PROPOSAL);
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_AES_KEY) {
            if (status[sender].recv < ConnectionStep::IDENTITY_VERIFIED) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager->getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_SERVER_AES_KEY");
                bool valid = clusterManager->getIdentityManager().receiveAesKey(*sender, message);
                if (valid && sender->isAlive()) { 
                    //update this manager status for this peer (track the connection progress)
                    this->setStatus(sender, ConnectionStep::AES);
                    //update the peer connection status (track the connectivity of a single peer)
                    // now other manager can emit message to the peer, and be notified by timers.
                    {
                        std::lock_guard lock{ sender->synchronize() };
                        sender->setState((sender->getState() & ~Peer::ConnectionState::CONNECTING) | Peer::ConnectionState::CONNECTED);
                    }
                    //update the clusterManager connection status (track the connection of our computer to the network)
                    // clusterManager.state.finishConnection(sender->getComputerId())
                    this->m_connection_state.finishConnection(sender->getComputerId());

                    //notify peers that a connection is established.
                    clusterManager->propagateMessage(sender, *UnnencryptedMessageType::NEW_CONNECTION, ByteBuff{});
                }
            }
        }
    }

    void ConnectionMessageManager::chooseComputerId(const std::unordered_set<uint16_t>& registered_computer_id, const std::unordered_set<uint16_t>& connected_computer_id) {
        // do not run this method in multi-thread (i use getIdentityManager() because i'm the only one to use it, to avoid possible dead sync issues)
        //std::lock_guard choosecomplock(getIdentityManager().synchronize());
         std::lock_guard choosecomplock(clusterManager->getIdentityManager().synchronize());

        // check if we have a computerId
        if (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::NOT_CHOOSEN
            || (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::TEMPORARY 
                && std::find(registered_computer_id.begin(), registered_computer_id.end(), clusterManager->getIdentityManager().getComputerId()) != registered_computer_id.end())) {
            //choose a random one and set our to "TEMPORARY"

            uint16_t choosenId = rand_u16();
            // while it's already taken
            const size_t MAX_ITERATION = 1000;
            size_t iteration = 0;
            //first, try to get a random one
            while ( (choosenId == 0 || choosenId == NO_COMPUTER_ID || std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end())
                && iteration < MAX_ITERATION) {
                log(std::to_string(clusterManager->getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = rand_u16();
                iteration++;
            }
            //if still not good
            if (std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end()) {
                //choose the first one
                for (choosenId = 0;
                    choosenId < std::numeric_limits<uint16_t>::max() - 2
                    && (choosenId == 0 || choosenId == NO_COMPUTER_ID || std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end());
                    ++choosenId) {
                }
            }
            if (choosenId >= std::numeric_limits<uint16_t>::max() - 2) {
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
        } else {
            // yes
            // there are a conflict?

            if (std::find(connected_computer_id.begin(), connected_computer_id.end(), clusterManager->getComputerId()) != connected_computer_id.end()) {
                log(std::string("my computerid is already connected, there : ") + clusterManager->getComputerId() + "\n");
                // yes
                // i choose my id recently?
                if (clusterManager->getIdentityManager().getComputerIdState() == IdentityManager::ComputerIdState::TEMPORARY) {
                    // change?
                    // TODO: choose a computer id, save it and close all connections before reconnecting.
                    // getIdentityManager().timeChooseId
                    error(std::to_string(clusterManager->getPeerId() % 100) + " ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.\n");
                    throw std::runtime_error("ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.");
                } else {
                    error(std::to_string(clusterManager->getPeerId() % 100) + " Error, an other server/instance has picked the same ComputerId as me. Both of us should be destroyed and re-created, at a time when we can communicate with each other.\n");
                }
            } else {
                // no
                // nothing todo do ! everything is ok!!
                log("ComputerId : everything ok, nothing to do\n");
            }

        }
        log(std::to_string(clusterManager->getPeerId() % 100) + " Choose my new ComputerId=" + clusterManager->getComputerId() + "\n");
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
                    std::thread reconnectThread([ptr, address, port]() {
                        //wait a bit for the disconnect to be in effect
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        //reconnect
                        ptr->connectTo(address, port, 0);
                    });
                    reconnectThread.detach();
                }
            }
        }
    }

    void ConnectionMessageManager::choosePeerId(const std::unordered_set<uint64_t>& registered_peer_id, const std::unordered_set<uint64_t>& connected_peer_id) {
        //do not run in multi-thread
        std::lock_guard choosecomplock(clusterManager->getIdentityManager().synchronize());

        bool need_new_id = !clusterManager->hasPeerId();
        bool need_to_reconnect = false;
        if (!need_new_id && std::find(connected_peer_id.begin(), connected_peer_id.end(), clusterManager->getPeerId()) != connected_peer_id.end()) {
            need_new_id = true;
            need_to_reconnect = true;
        }
        if (need_new_id) {

            uint64_t choosenId = clusterManager->getPeerId();
            // while it's already taken
            const size_t MAX_ITERATION = 1000;
            size_t iteration = 0;
            //first, try to get a random one
            while ( (choosenId == 0 || choosenId== NO_PEER_ID || std::find(registered_peer_id.begin(), registered_peer_id.end(), choosenId) != registered_peer_id.end())
                && iteration < MAX_ITERATION) {
                log(std::to_string(clusterManager->getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = rand_u63();
                iteration++;
            }
            //then, get a random one (hopefully, the random generator can output all values)
            while (choosenId == 0 || choosenId == NO_PEER_ID || std::find(connected_peer_id.begin(), connected_peer_id.end(), choosenId) != connected_peer_id.end()) {
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
        // == the connected peerIds  ==
        size_t nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            data.connected_peer_id.insert(message.getULong());
        }
        // == the connected computerIds  ==
        nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            data.connected_computer_id.insert(message.getUShort());
        }
        // == then, the registered peerIds ==
        nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            data.registered_peer_id.insert(message.getULong());
        }
        // == then, the registered computerIds ==
        nb = message.getSize();
        for (size_t i = 0; i < nb; i++) {
            data.registered_computer_id.insert(message.getUShort());
        }
        return data;
    }

    ConnectionMessageManager::Data_SEND_SERVER_LIST ConnectionMessageManager::create_Data_SEND_SERVER_LIST() {
        const std::vector<PeerPtr>& registered = this->clusterManager->getIdentityManager().getLoadedPeers();
        const PeerList& connected = this->clusterManager->getPeersCopy();
        Data_SEND_SERVER_LIST data;
        // == put our id if any ==
        //TODO: use getComputerIdState instead of NO_COMPUTER_ID
        bool has_id = clusterManager->getIdentityManager().getComputerIdState() != IdentityManager::ComputerIdState::NOT_CHOOSEN || clusterManager->getComputerId() != NO_COMPUTER_ID;
        // == first, the registered computer (and peer) ids (also with me if i was/is connected) ==
        if (has_id) {
            data.registered_computer_id.insert(clusterManager->getComputerId());
            data.registered_peer_id.insert(clusterManager->getPeerId());
        }
        for (const PeerPtr& peer : registered) {
            data.registered_computer_id.insert(peer->getComputerId());
            data.registered_peer_id.insert(peer->getPeerId());
        }
        // == then, the connected peerIds ==
        if (has_id && clusterManager->getState().isConnected()) {
            data.registered_computer_id.insert(clusterManager->getComputerId());
            data.registered_peer_id.insert(clusterManager->getPeerId());
        }
        for (const PeerPtr& peer : connected) {
            if (peer->isConnected()) {
                data.connected_computer_id.insert(peer->getComputerId());
                data.connected_peer_id.insert(peer->getPeerId());
            }
        }
        return data;
    }

    ByteBuff ConnectionMessageManager::create_SEND_SERVER_LIST_msg(Data_SEND_SERVER_LIST& data) {
        ByteBuff buff;
        // == the connected peerIds  ==
        buff.putSize(data.connected_peer_id.size());
        for (uint64_t peerid : data.connected_peer_id) {
            buff.putULong(peerid);
        }
        // == the connected computerIds  ==
        buff.putSize(data.connected_computer_id.size());
        for (uint16_t computerid : data.connected_computer_id) {
            buff.putUShort(computerid);
        }
        // == then, the registered peerIds ==
        buff.putSize(data.registered_peer_id.size());
        for (uint64_t peerid : data.registered_peer_id) {
            buff.putULong(peerid);
        }
        // == then, the registered computerIds ==
        buff.putSize(data.registered_computer_id.size());
        for (uint16_t computerid : data.registered_computer_id) {
            buff.putUShort(computerid);
        }
        buff.flip();
        return buff;
    }


    //void ConnectionMessageManager::sendServerId(Peer& peer) {
    ByteBuff ConnectionMessageManager::create_SEND_SERVER_ID_msg(ConnectionMessageManager::Data_SEND_SERVER_ID& data) {
        ByteBuff buff;
        buff.putULong(data.peer_id);
        buff.putULong(data.cluster_id);
        buff.putInt(data.port);
        return buff.flip();
    }
    ConnectionMessageManager::Data_SEND_SERVER_ID ConnectionMessageManager::get_SEND_SERVER_ID_msg(ByteBuff& message) {
        Data_SEND_SERVER_ID data;
        // special case, give the peer object directly.
        data.peer_id = message.getULong();
        //check if the cluster is ok
        data.cluster_id = message.getULong();
        //get listen port
        data.port = (uint16_t(message.getInt()));
        return data;
    }


} // namespace supercloud
