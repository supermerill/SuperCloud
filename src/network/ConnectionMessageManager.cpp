#include "ConnectionMessageManager.hpp"
#include "ServerIdDb.hpp"

#include <chrono>

namespace supercloud{

    void ConnectionMessageManager::register_listener() {
        clusterManager.registerListener(*UnnencryptedMessageType::CONNECTION_CLOSED, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::GET_SERVER_ID, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::GET_SERVER_LIST, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::GET_VERIFY_IDENTITY, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::GET_SERVER_AES_KEY, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::SEND_SERVER_ID, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::SEND_SERVER_LIST, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::SEND_VERIFY_IDENTITY, this->ptr());
        clusterManager.registerListener(*UnnencryptedMessageType::SEND_SERVER_AES_KEY, this->ptr());
    }

    void ConnectionMessageManager::requestCurrentStep(PeerPtr sender, bool enforce) {
        if (!sender->isAlive()) { return; }
        ConnectionStep currentStep = status[sender].recv;
        if (currentStep == ConnectionStep::BORN && (enforce || status[sender].last_request != ConnectionStep::ID)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_ID);
            status[sender].last_request = ConnectionStep::ID;
        }
        else if (currentStep == ConnectionStep::ID && (enforce || status[sender].last_request != ConnectionStep::SERVER_LIST)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_LIST);
            status[sender].last_request = ConnectionStep::SERVER_LIST;
        }
        else if (currentStep == ConnectionStep::SERVER_LIST && (enforce || status[sender].last_request != ConnectionStep::RSA)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
            status[sender].last_request = ConnectionStep::RSA;
        }
        else if (currentStep == ConnectionStep::RSA && (enforce || status[sender].last_request != ConnectionStep::IDENTITY_VERIFIED)) {
            ServerIdDb::Identityresult result = clusterManager.getServerIdDb().sendIdentity(*sender, clusterManager.getServerIdDb().createMessageForIdentityCheck(*sender, false), true);
            if (result == ServerIdDb::Identityresult::NO_PUB) {
                sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                status[sender].last_request = ConnectionStep::RSA;
            } else if (result == ServerIdDb::Identityresult::BAD) {
                sender->close();
            } else if (result == ServerIdDb::Identityresult::OK) {
                status[sender].last_request = ConnectionStep::IDENTITY_VERIFIED;
            }
        }
        else if (currentStep == ConnectionStep::IDENTITY_VERIFIED && (enforce || status[sender].last_request != ConnectionStep::AES)) {
            sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_AES_KEY);
            status[sender].last_request = ConnectionStep::AES;
        }
    }

    void ConnectionMessageManager::receiveMessage(PeerPtr sender, uint8_t messageId, ByteBuff message) {
        // answer to deletion
        if (!sender->isAlive()) {
            if (status.find(sender) != status.end()) {
                status.erase(sender);
            }
            return;
        }
        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive message " + messageId+" : " + messageId_to_string(messageId) + "(ConnectionMessageManager)");
        if (messageId == *UnnencryptedMessageType::GET_SERVER_ID) {
            // special case, give the peer object directly.
            log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + "read GET_SERVER_ID : now sending my peerId");
            sendServerId(*sender);
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_ID) {
            // add it to the map, if not already here
            if (status.find(sender) == status.end()) { status[sender].recv = ConnectionStep::BORN; }
            // special case, give the peer object directly.
            sender->setPeerId(message.getULong());
            //check if the cluster is ok
            uint64_t clusterId = message.getULong();
            //get listen port
            sender->setPort(uint16_t(message.getInt()));
            //Verify that the cluster Id is good
            if (clusterId > 0 && clusterManager.getServerIdDb().getClusterId() < 0) {
                //set our cluster id
//						myServer.getServerIdDb().setClusterId(clusterId);
                throw std::runtime_error("Error, we haven't a clusterid !! Can we pick one from an existing network? : not anymore!");
                //						changeState(PeerConnectionState::HAS_ID, true);
            } else if (clusterId > 0 && clusterManager.getServerIdDb().getClusterId() != clusterId) {
                //error, not my cluster!
                std::cerr << std::to_string(clusterManager.getPeerId() % 100) << " Error, trying to connect with " << (sender->getPeerId() % 100) << " but his cluster is " << clusterId << " and mine is "
                    << clusterManager.getServerIdDb().getClusterId() << " => closing connection\n";
                sender->close();
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " read SEND_SERVER_ID : get their id: " + sender->getPeerId());
                status[sender].recv = std::max(status[sender].recv, ConnectionStep::ID);
                //ask for next step
                requestCurrentStep(sender, false);
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_LIST) {
            if (status[sender].recv < ConnectionStep::ID) { // still don't have your id, please give them to me beforehand
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + "he want my server list");
                {std::lock_guard lock(this->clusterManager.getServerIdDb().synchronize());
                    sendServerList(*sender, this->clusterManager.getServerIdDb().getRegisteredPeers(), this->clusterManager.getPeersCopy());
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_LIST) {
            if (status[sender].recv < ConnectionStep::ID) { // still don't have your id, please give them to me beforehand
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_SERVER_LIST");
                useServerList(sender, message);
                if (sender->isAlive()) { 
                    status[sender].recv = std::max(status[sender].recv, ConnectionStep::SERVER_LIST);
                    //ask for next step
                    requestCurrentStep(sender, false);
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY) {
            if (status[sender].recv < ConnectionStep::SERVER_LIST) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive GET_SERVER_PUBLIC_KEY");
                clusterManager.getServerIdDb().sendPublicKey(*sender);
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY) {
            if (status[sender].recv < ConnectionStep::SERVER_LIST) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_SERVER_PUBLIC_KEY");
                clusterManager.getServerIdDb().receivePublicKey(*sender, message);
                if (sender->isAlive()) { status[sender].recv = std::max(status[sender].recv, ConnectionStep::RSA); }
                //ask for next step
                requestCurrentStep(sender, false);
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_VERIFY_IDENTITY) {
            if (status[sender].recv < ConnectionStep::RSA) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive GET_VERIFY_IDENTITY");
                ServerIdDb::Identityresult result = clusterManager.getServerIdDb().answerIdentity(*sender, message);
                if (result == ServerIdDb::Identityresult::NO_PUB) {
                    sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                    status[sender].last_request = ConnectionStep::RSA;
                } else if (result == ServerIdDb::Identityresult::BAD) {
                    sender->close();
                } else if (result == ServerIdDb::Identityresult::OK) {
                    if (sender->isAlive()) { status[sender].recv = std::max(status[sender].recv, ConnectionStep::IDENTITY_VERIFIED); }
                    status[sender].last_request = ConnectionStep::IDENTITY_VERIFIED;
                    // answer is sent inside the  'answerIdentity' method, as it has to reuse the decoded message content.
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_VERIFY_IDENTITY) {
            if (status[sender].recv < ConnectionStep::RSA) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_VERIFY_IDENTITY");
                ServerIdDb::Identityresult result = clusterManager.getServerIdDb().receiveIdentity(sender, message);
                if (result == ServerIdDb::Identityresult::NO_PUB) {
                    sender->writeMessage(*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY);
                    status[sender].last_request = ConnectionStep::RSA;
                } else if (result == ServerIdDb::Identityresult::BAD) {
                    sender->close();
                } else if (result == ServerIdDb::Identityresult::OK) {
                    if (sender->isAlive()) { status[sender].recv = std::max(status[sender].recv, ConnectionStep::IDENTITY_VERIFIED); }
                    //ask for next step
                    requestCurrentStep(sender, false);
                }
            }
        }
        if (messageId == *UnnencryptedMessageType::GET_SERVER_AES_KEY) {
            if (status[sender].recv < ConnectionStep::IDENTITY_VERIFIED) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive GET_SERVER_AES_KEY");
                clusterManager.getServerIdDb().sendAesKey(*sender, ServerIdDb::AES_PROPOSAL);
            }
        }
        if (messageId == *UnnencryptedMessageType::SEND_SERVER_AES_KEY) {
            if (status[sender].recv < ConnectionStep::IDENTITY_VERIFIED) {
                requestCurrentStep(sender);
            } else {
                log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive SEND_SERVER_AES_KEY");
                clusterManager.getServerIdDb().receiveAesKey(*sender, message);
                if (sender->isAlive()) { status[sender].recv = std::max(status[sender].recv, ConnectionStep::AES); }
            }
        }
        //old one, to be moved somewhere else, after the connection is established
        //if (messageId == 666/*SEND_SERVER_LIST*/) {
        //    log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " received SEND_SERVER_LIST");

        //    //			System.out.println(p.getMyServer().getId()%100+" read "+myId+" for "+p.getKey().getOtherServerId()%100);
        //    //add this id in our list, to be sure we didn't use it and we can transmit it.
        //    {
        //        std::lock_guard lock(this->clusterManager.getServerIdDb().synchronize());
        //        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " receive peer computerId:  " + senderComputerId );
        //        this->clusterManager.getServerIdDb().addPeer(senderComputerId);
        //    }
        //    size_t nb = message.getTrailInt();
        //    for (int i = 0; i < nb; i++) {
        //        long id = message.getLong();
        //        std::string ip = message.getUTF8();
        //        int port = message.getTrailInt();
        //        short computerId = message.getShort();

        //        //add this id in our list, to be sure we didn't use it and we can transmit it.
        //        {
        //            std::lock_guard lock(this->clusterManager.getServerIdDb().synchronize());
        //            log(std::to_string(clusterManager.getPeerId() % 100) + " receive a distant computerId:  " + computerId + " of " + id % 100 );
        //            this->clusterManager.getServerIdDb().addPeer(computerId);
        //        }
        //        //				System.out.println(p.getMyServer().getId()%100+" i have found "+ip+":"+ port);


        //        //					InetSocketAddress addr = new InetSocketAddress(ip,port);
        //        if (computerId > 0 && computerId != clusterManager.getComputerId() && computerId != senderComputerId
        //            && id != clusterManager.getPeerId() && id != sender->getPeerId()) {
        //            //note: connectTO can throw exception if it can't be joinable.
        //            //new Thread(()->clusterManager.connectTo(ip, port)).start();
        //            //std::thread t([clusterManager&, ip&, port]() { clusterManager.connectTo(ip, port); });
        //            std::thread t([this, ip, port]() { this->clusterManager.connect(ip, port); });
        //            t.detach();
        //        }
        //    }
        //    clusterManager.getServerIdDb().addToReceivedServerList(sender);
        //    clusterManager.chooseComputerId();
        //}

    }

    void ConnectionMessageManager::chooseComputerId(const std::unordered_set<uint16_t>& registered_computer_id, const std::unordered_set<uint16_t>& connected_computer_id) {
        // do not run this method in multi-thread (i use getServerIdDb() because i'm the only one to use it, to avoid possible dead sync issues)
        //std::lock_guard choosecomplock(getServerIdDb().synchronize());
         std::lock_guard choosecomplock(clusterManager.getServerIdDb().synchronize());

        // check if we have a clusterId
        if (clusterManager.getServerIdDb().getComputerIdState() == ServerIdDb::ComputerIdState::NOT_CHOOSEN
            || (clusterManager.getServerIdDb().getComputerIdState() == ServerIdDb::ComputerIdState::TEMPORARY 
                && std::find(registered_computer_id.begin(), registered_computer_id.end(), clusterManager.getServerIdDb().getComputerId()) != registered_computer_id.end())) {
            //choose a random one and set our to "TEMPORARY"

            uint16_t choosenId = rand_u16();
            // while it's already taken
            const size_t MAX_ITERATION = 1000;
            size_t iteration = 0;
            //first, try to get a random one
            while ( (choosenId == 0 || choosenId == NO_COMPUTER_ID || std::find(registered_computer_id.begin(), registered_computer_id.end(), choosenId) != registered_computer_id.end())
                && iteration < MAX_ITERATION) {
                log(std::to_string(clusterManager.getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
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
                if (clusterManager.getServerIdDb().getComputerIdState() == ServerIdDb::ComputerIdState::TEMPORARY) {
                    error(std::to_string(clusterManager.getPeerId() % 100) + " ERROR! my ComputerId is already taken, and I can't change as they are all taken.\n");
                    throw std::runtime_error("ERROR! my ComputerId is already taken, and I can't change as they are all taken.. Please destroy this instance/peer/server and reuse another one.");
                } else {
                    error(std::to_string(clusterManager.getPeerId() % 100) + " ERROR! no computerId left to be taken.\n");
                    throw std::runtime_error("ERROR! no computerId left to be taken. Please reuse one instead.");
                }
            }
            clusterManager.getServerIdDb().setComputerId(choosenId, ServerIdDb::ComputerIdState::TEMPORARY);
        } else {
            // yes
            // there are a conflict?

            if (std::find(connected_computer_id.begin(), connected_computer_id.end(), clusterManager.getComputerId()) != connected_computer_id.end()) {
                log(std::string("my computerid is already connected, there : ") + clusterManager.getComputerId() + "\n");
                // yes
                // i choose my id recently?
                if (clusterManager.getServerIdDb().getComputerIdState() == ServerIdDb::ComputerIdState::TEMPORARY) {
                    // change?
                    // TODO: choose a computer id, save it and close all connections before reconnecting.
                    // getServerIdDb().timeChooseId
                    error(std::to_string(clusterManager.getPeerId() % 100) + " ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.\n");
                    throw std::runtime_error("ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.");
                } else {
                    error(std::to_string(clusterManager.getPeerId() % 100) + " Error, an other server/instance has picked the same ComputerId as me. Both of us should be destroyed and re-created, at a time when we can communicate with each other.\n");
                }
            } else {
                // no
                // nothing todo do ! everything is ok!!
                log("ComputerId : everything ok, nothing to do\n");
            }

        }
        log(std::to_string(clusterManager.getPeerId() % 100) + " Choose my new ComputerId=" + clusterManager.getComputerId() + "\n");
    }
    void ConnectionMessageManager::choosePeerId(const std::unordered_set<uint64_t>& registered_peer_id, const std::unordered_set<uint64_t>& connected_peer_id) {
        //do not run in multi-thread
        std::lock_guard choosecomplock(clusterManager.getServerIdDb().synchronize());

        bool need_new_id = !clusterManager.hasPeerId();
        bool need_to_reconnect = false;
        if (!need_new_id && std::find(connected_peer_id.begin(), connected_peer_id.end(), clusterManager.getPeerId()) != connected_peer_id.end()) {
            need_new_id = true;
            need_to_reconnect = true;
        }
        if (need_new_id) {

            uint64_t choosenId = clusterManager.getPeerId();
            // while it's already taken
            const size_t MAX_ITERATION = 1000;
            size_t iteration = 0;
            //first, try to get a random one
            while ( (choosenId == 0 || choosenId== NO_PEER_ID || std::find(registered_peer_id.begin(), registered_peer_id.end(), choosenId) != registered_peer_id.end())
                && iteration < MAX_ITERATION) {
                log(std::to_string(clusterManager.getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = rand_u63();
                iteration++;
            }
            //then, get a random one (hopefully, the random generator can output all values)
            while (choosenId == 0 || choosenId == NO_PEER_ID || std::find(connected_peer_id.begin(), connected_peer_id.end(), choosenId) != connected_peer_id.end()) {
                log(std::to_string(clusterManager.getPeerId() % 100) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                choosenId = rand_u63();
            }

            clusterManager.setPeerId(choosenId);
        }
        if (need_to_reconnect) {
            clusterManager.reconnect();
        }
    }
    void ConnectionMessageManager::useServerList(PeerPtr sender, ByteBuff& message) {

        log(std::to_string(clusterManager.getPeerId() % 100) + "<-" + (sender->getPeerId() % 100) + " received SEND_SERVER_LIST");
        std::unordered_set<uint16_t> registered_computer_id;
        std::unordered_set<uint16_t> connected_computer_id;
        std::unordered_set<uint64_t> registered_peer_id;
        std::unordered_set<uint64_t> connected_peer_id;
        //get registered
        size_t nb_registered = message.getTrailInt();
        for (int i = 0; i < nb_registered; i++) {
            uint64_t peer_id = message.getULong();
            uint16_t computer_id = uint16_t(message.getShort());
            // but not me
            if (peer_id != clusterManager.getPeerId() || computer_id != clusterManager.getComputerId()) {
                registered_peer_id.insert(peer_id);
                registered_computer_id.insert(computer_id);
            }
        }
        //get connected
        for (int i = 0; i < nb_registered; i++) {
            uint64_t peer_id = message.getULong();
            uint16_t computer_id = uint16_t(message.getShort());
            // but not me
            if (peer_id != clusterManager.getPeerId() || computer_id != clusterManager.getComputerId()) {
                registered_peer_id.insert(peer_id);
                connected_peer_id.insert(peer_id);
                registered_computer_id.insert(computer_id);
                connected_computer_id.insert(computer_id);
            }
        }
        //now choose computer & peer id
        chooseComputerId(registered_computer_id, connected_computer_id);
        choosePeerId(registered_peer_id, connected_peer_id);
    }

    void ConnectionMessageManager::sendServerList(Peer& sendTo, const std::vector<PeerPtr>& registered, const PeerList& connected) {
        ByteBuff buff;
        // == put our id if any ==
        //TODO: use getComputerIdState instead of NO_COMPUTER_ID
        bool has_id = clusterManager.getServerIdDb().getComputerIdState() != ServerIdDb::ComputerIdState::NOT_CHOOSEN || clusterManager.getComputerId() != NO_COMPUTER_ID;
        // == first, the registered computer (and peer) ids (also with me if i was/is connected) ==
        buff.putTrailInt(registered.size() + has_id ? 1 : 0);
        if (has_id) {
            buff.putULong(clusterManager.getPeerId());
            buff.putShort(clusterManager.getComputerId());
        }
        for (const PeerPtr& peer : registered) {
            buff.putULong(peer->getPeerId());
            buff.putShort(peer->getComputerId());
            //				log(/*serv.getListenPort()+*/" SEND SERVER "+peer.getPort()+ " to "+p.getKey().getPort());
        }
        // == then, the connected peerIds ==
        buff.putTrailInt(int32_t(registered.size()));
        for (const PeerPtr& peer : registered) {
            buff.putULong(peer->getPeerId());
            buff.putShort(clusterManager.getComputerId());
            //				log(/*serv.getListenPort()+*/" SEND SERVER "+peer.getPort()+ " to "+p.getKey().getPort());
        }
        buff.flip();
        sendTo.writeMessage(*UnnencryptedMessageType::SEND_SERVER_LIST, buff);
    }


    void ConnectionMessageManager::sendServerId(Peer& peer) {
        ByteBuff buff;
        buff.putULong(clusterManager.getPeerId());
        buff.putULong(clusterManager.getServerIdDb().getClusterId());
        buff.putInt(clusterManager.getListenPort());
        //clusterManager.writeMessage(peer, SEND_SERVER_ID, buff.flip());
        peer.writeMessage(*UnnencryptedMessageType::SEND_SERVER_ID, buff.flip());
    }


} // namespace supercloud
