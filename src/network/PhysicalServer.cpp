#include "PhysicalServer.hpp"

#include <chrono>
#include <limits>
#include <thread>

#include "IdentityManager.hpp"
#include "../utils/Utils.hpp"
#include "ConnectionMessageManager.hpp"
#include "ClusterAdminMessageManager.hpp"
#include "networkAdapter.hpp"


namespace supercloud {

    PhysicalServer::PhysicalServer() : has_peer_id(false) {
        this->m_listen_socket = ServerSocket::factory->create();
    }

    std::shared_ptr<PhysicalServer> PhysicalServer::createAndInit(const std::filesystem::path& folderPath) {
        std::shared_ptr<PhysicalServer> ptr = std::shared_ptr<PhysicalServer>{ new PhysicalServer{} };
        ptr->m_cluster_id_mananger = std::make_shared<IdentityManager>(*ptr, folderPath / "clusterIds.properties");
        ptr->m_cluster_id_mananger->load();
        ptr->m_connection_manager = ConnectionMessageManager::create(ptr, ptr->m_state);
        ptr->m_cluster_admin_manager = ClusterAdminMessageManager::create(*ptr);

        //init a random peer id if i won't reuse the previous one.
        uint64_t my_peer_id = ptr->m_cluster_id_mananger->getSelfPeer()->getPeerId();
        if (my_peer_id == 0 || my_peer_id == NO_PEER_ID) {
            ptr->m_cluster_id_mananger->getSelfPeer()->setPeerId(1 + rand_u63());
        }

        return ptr;
    }


    uint64_t PhysicalServer::getPeerId() const {
        return m_cluster_id_mananger->getSelfPeer()->getPeerId();
    }
    uint16_t PhysicalServer::getComputerId() const {
        return m_cluster_id_mananger->getComputerId();
    }

    void PhysicalServer::launchUpdater() {
        if (!this->hasUpdaterThread) {
            this->hasUpdaterThread = true;
            std::shared_ptr<PhysicalServer> me = ptr();
            std::thread updaterThread([me](){
                std::cout << "launchUpdater launched\n";
                while (true) {
                    me->update();
                }
            });
            updaterThread.detach();
        }
    }

    int64_t update_check_NO_COMPUTER_ID = 0;
    void PhysicalServer::update() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::this_thread::sleep_for(std::chrono::milliseconds(5000)); //FIXME only for testing purpose to reduce clutter, delete
        //bool quickUpdate = true;
        //can't update until you're connected
        if (getComputerId() == NO_COMPUTER_ID) {
            //if (quickUpdate) {
            //} else {
            //    std::this_thread::sleep_for(std::chrono::seconds(60));
            //}
            //quickUpdate = false;

            // in case of something went wrong, recheck.
            /*if (getIdentityManager().getClusterId() != NO_CLUSTER_ID) {
                chooseComputerId();
            }*/
            if (update_check_NO_COMPUTER_ID == 0 && m_last_minute_update != 0) {
                update_check_NO_COMPUTER_ID = m_last_minute_update;
            }
            {std::lock_guard lock(this->peers_mutex);
                int nbAlive = 0;
                if ( (peers.empty() || !peers.front()->isAlive()) && update_check_NO_COMPUTER_ID != m_last_minute_update){
                    error("Error, There is no connection still alive and i still don't have a computer id. Please connect to a valid peer or create a new cluster before calling update.");
                }
            }
            return;
        }

        //std::this_thread::sleep_for(std::chrono::seconds(5));
        int64_t now = get_current_time_milis(); // just like java (new Date()).getTime();
        ByteBuff now_msg = ByteBuff{}.putLong(now).flip();
       
        //clean peer list
        {std::lock_guard lock(this->peers_mutex);
            log(std::to_string(getPeerId() % 100) + " ======== peers =======");
            for (PeerPtr& p : this->peers) {
                log(std::to_string(getPeerId() % 100) + " " + p->getIP() + ":" + p->getPort() + " cid=" + p->getComputerId() + " pid=" + (p->getPeerId() % 100) + p->isAlive());
            }
            log(std::to_string(getPeerId() % 100) + " ======================");

            //remove bad peers
            for (auto itPeers = custom::it{ peers }; itPeers.has_next();) {
                PeerPtr& nextP = itPeers.next();
                if (this->getComputerId() != NO_COMPUTER_ID && nextP->getComputerId() == this->getComputerId()) {
                    error(std::string("Error: a peer (")+ (nextP->getPeerId()%100)+") is me (" + (getPeerId() % 100) + ") (same computerid) " + this->getComputerId());
                    getIdentityManager().removeBadPeer(nextP.get(), this->getComputerId());
                    nextP->close();
                    itPeers.erase();
                } else if (this->hasPeerId() && nextP->getPeerId() == this->getPeerId()) {
                    error(std::string(" Error: a peer (") + (nextP->getPeerId() % 100) +") is me (" + (getPeerId() % 100) + ") (same peerId) ");
                    getIdentityManager().removeBadPeer(nextP.get(), this->getComputerId());
                    nextP->close();
                    itPeers.erase();
                } else if (!nextP->isAlive()){
                    error(std::to_string(getPeerId() % 100) + std::string(" Error: a peer (") + (nextP->getPeerId() % 100) +") is dead: clean it" );
                    getIdentityManager().removeBadPeer(nextP.get(), this->getComputerId());
                    nextP->close();
                    itPeers.erase();
                }else if ((nextP->getComputerId() == 0 || nextP->getComputerId() == NO_COMPUTER_ID) && nextP->createdAt < now - 2 * 60 * 1000) {
                    error(std::to_string(getPeerId() % 100) + std::string("Error: a peer ")+ (nextP->getPeerId()%100)+") has no id and is old(>2min) : " );
                    nextP->close();
                    itPeers.erase();
                }
            }

            // remove duplicate peers
            std::unordered_map<int16_t, uint16_t> count;
            for (const PeerPtr& p : peers) {
                count[p->getComputerId()]++;
            }

            foreach(itPeer, peers) {
                 if (count[(*itPeer)->getComputerId()] > 1) {
                    std::cerr<<"Error: multiple peer for " << this->getPeerId() << " : deleting one of multiple instances \n";
                    count[(*itPeer)->getComputerId()]--;
                    (*itPeer)->close();
                    itPeer.erase();
                }
            }
        }

        PeerList copy = getPeersCopy();

        log(std::to_string(getPeerId() % 100) + " update ");
        for (const PeerPtr& peer : copy) {
            log(std::to_string(getPeerId() % 100 ) + " update peer " + peer->getPeerId() % 100+"\n");
            // If the peer is fully connected, emit timer message.
            if (peer->isConnected()) {
                this->propagateMessage(peer, *UnnencryptedMessageType::TIMER_SECOND, now_msg);
                if (now - m_last_minute_update > 1000 * 60) {
                    this->propagateMessage(peer, *UnnencryptedMessageType::TIMER_MINUTE, now_msg);
                }
            }
        }

        if (now - m_last_minute_update > 1000 * 60) {
            m_last_minute_update = now;
        }
    }

    /**
    * Create a new thread to listen to connection.
    * It will create a new one for each new connection.
    *
    */
    void PhysicalServer::listen(uint16_t port) {
        if (!hasSocketListener) {
            try {
                hasSocketListener = true;
                std::shared_ptr<PhysicalServer> me = ptr();
                std::thread socketListenerThread([me, port]() {
                    try {
                        me->m_listen_socket->init(port);
                        log(std::string("Listen to ") + me->m_listen_socket->endpoint().address() + " : " + me->m_listen_socket->endpoint().port() + "\n");

                        while (true) {
                            log("wait a connect...\n");
                            //create this connection socket
                            std::shared_ptr<Socket> new_socket = me->m_listen_socket->listen();
                            log("connected to a socketserver\n");
                            PeerPtr peer = Peer::create(*me, new_socket->local_endpoint().address(), new_socket->local_endpoint().port(), Peer::ConnectionState::CONNECTING);
                            std::thread clientSocketThread([me, peer, new_socket]() {
                                try {
                                    if (me->initConnection(peer, new_socket, false)) {
                                        //FIXME have to listen to the connection from this thread or the socket is closed
                                        peer->run();
                                    }
                                    peer->close();
                                }
                                catch (std::exception e) {
                                    error(std::string("error: end of the socket for peer: ")+(peer->getPeerId()%100)+" : " + e.what());
                                }
                            });
                            clientSocketThread.detach();
                        }
                    }
                    catch (std::exception e) {
                        me->hasSocketListener = false;
                        error(std::string("error: ") +(me->getPeerId()%100) + (" the listener thread has stopped working : ") + e.what());
                    }
                    me->hasSocketListener = false;
                });
                socketListenerThread.detach();
            }
            catch (std::exception e) {
                this->hasSocketListener = false;
                error(std::string("error: ") + (this->getPeerId() % 100) + (" can't start the listener thread : ") + e.what());
            }
        }

    }

    /**
     * Init a connection with an other (hypothetical) peer.
     * note: You HAVE TO call run() if the return value is true so this thread continue to answer message from the socket.
     * (as opening a new thread and letting this one close may close the socket via an unknown mecanism)
     * @param peer peer to try to connect
     * @param sock our socket to know what to tell him if he want to connect to us.
     * @return true if i am listening to him at the end, but do not trust this too much.
     * @throws InterruptedException
     * @throws IOException
     */
    bool PhysicalServer::initConnection(PeerPtr peer, std::shared_ptr<Socket> sock, bool initiated_by_me) {

        if (peer->connect(sock, initiated_by_me)) {

            //try {
                {std::lock_guard lock(this->peers_mutex);
                    // check if we already have this peer id.
                    PeerPtr otherPeer;
                    for (const PeerPtr& p : peers) {
                        if (p->getPeerId() == peer->getPeerId()) {
                            otherPeer = p;
                            break;
                        }
                    }
                    if (!otherPeer) {
                        log(std::to_string(getPeerId() % 100 ) + " 'new' accept connection to " + peer->getPeerId() % 100 + "\n");
                        // peers.put(peer->getKey(), peer);
                        if (!peers.contains(peer)) {
                            //log(std::to_string(getPeerId() % 100 ) + " PROPAGATE " + peer->getPeerId() % 100 + "!\n");
                            peers.push_back(peer);

                            // new peer: propagate! send this new information to my connected peers => now done in ClusterAdminManager
                            //for (PeerPtr& oldPeer : peers) {
                            //    if (oldPeer->isAlive() && oldPeer->getPeerId() != peer->getPeerId()) {
                            //       log(std::to_string(getPeerId() % 100 ) + " PROPAGATE new peer '"+(peer->getPeerId() % 100)+"' to " + oldPeer->getPeerId() % 100+"\n");
                            //        // MyServerList.get().write(peers, oldPeer);
                            //        messageManager->sendServerList(*oldPeer, getIdentityManager().getRegisteredPeers(), peers);
                            //    }
                            //}
                        } else {
                            peers.push_back(peer);
                        }
                        return true;
                    } else {
                        if (otherPeer->isAlive() && !peer->isAlive()) {
                            std::cerr << getPeerId() % 100 << " 'warn' , close a new dead connection to "
                                    << peer->getPeerId() % 100 << " is already here....."<<"\n";
                            peer->close();
                        } else if (!otherPeer->isAlive() && peer->isAlive()) {
                            std::cerr << getPeerId() % 100 << " warn,  close an old dead  a connection to "
                                    << peer->getPeerId() % 100 << " is already here....."<<"\n";
                            otherPeer->close();
                            // peers.put(peer->getKey(), peer);
                            peers.push_back(peer);
                            return true;
                        } else if (otherPeer->getPeerId() != 0) {
                            if (otherPeer->getPeerId() < getPeerId()) {
                                std::cerr << getPeerId() % 100 << " warn, (I AM LEADER) a connection to "
                                        << peer->getPeerId() % 100 << " is already here....."<<"\n";
                                peer->close();
                            } else {
                                std::cerr << getPeerId() % 100 << " warn, (I am not leader) a connection to "
                                        << peer->getPeerId() % 100 << " is already here.....\n";
                                peers.push_back(peer);
                                return true;
                            }
                        } else if (peer->getPeerId() != 0) {
                            if (peer->getPeerId() < getPeerId()) {
                                std::cerr << getPeerId() % 100 << " warn, (I AM LEADER) a connection to "
                                        << peer->getPeerId() % 100 << " is already here.....\n";
                                otherPeer->close();
                                // peers.put(peer->getKey(), peer);
                                peers.push_back(peer);
                                return true;
                            } else {
                                std::cerr << getPeerId() % 100 << " warn, (I am not leader) a connection to "
                                        << peer->getPeerId() % 100 << " is already here.....\n";
                                peers.push_back(peer);
                                return true;
                            }
                        } else {
                            std::cerr << getPeerId() % 100 << " warn, an unknown connection to "
                                    << peer->getPeerId() % 100 << " is already here.....\n";
                            peer->close();
                        }
                        std::cout << getPeerId() % 100 << " 'old' accept connection to "
                                << peer->getPeerId() % 100<<"\n";
                    }
                }

                // test if id is ok
                //TODO check if still needed
                if (peer->getPeerId() == getPeerId() && hasPeerId()) {
                    error(std::to_string(getPeerId() % 100) + " my peerid is equal to  " + peer->getPeerId() % 100 + "\n");
                    //return rechooseId();
                    return false;
                }
                // TODO: test if id is inside
            //}
            // catch (Exception e) {
            //  throw new RuntimeException(e);
            //}
        } else {
            peer->close();
        }
        return false;
    }

    /**
    * Try to connect to a peer at this address/port.
    * It will continue to use the calling thread for the connection.
    * @param ip address
    * @param port port
    * @return false if it fails to connect, and true if it has a connection at some point. 
    */
    bool PhysicalServer::connectTo(const std::string& ip, uint16_t port, int64_t timeout_milis, std::shared_ptr<std::promise<bool>> notify_socket_connection) {
        bool has_emmitted_promise = false;
        // check if it's not me
        if (this->m_listen_socket && this->m_listen_socket->endpoint().port() == port
            && (m_listen_socket->endpoint().address() == (ip) || ip == ("127.0.0.1"))) {
            log("DON't connect TO ME MYSELF\n");
            goto return_false;
        } else {
            // std::cout<<mySocket.getLocalPort()<<" =?= "<<port<<"\n";
            // std::cout<<mySocket.getInetAddress().getHostAddress()<<" =?= "<<ip<<"\n";
            // std::cout<<mySocket.getInetAddress().getHostName()<<" =?= "<<ip<<"\n";
        }
        log(std::to_string(getPeerId() % 100 ) + " I am " + m_listen_socket->endpoint().address() + ":" + m_listen_socket->endpoint().port()+"\n");
        log(std::to_string(getPeerId() % 100 ) + " I want to CONNECT with " + ip + ":" + port+"\n");
        try {
            std::shared_ptr<Socket> new_socket = this->m_listen_socket->client_socket(EndPoint{ ip , port });
            std::future<void> connect_end = new_socket->connect();
            if (timeout_milis > 0) {
                //wait but only for timeout_milis;
                std::future_status result = connect_end.wait_for(std::chrono::milliseconds(timeout_milis));
                if (!new_socket->is_open()) {
                    new_socket->cancel();
                    goto return_false;
                }
            }
            if (notify_socket_connection) {
                has_emmitted_promise = true;
                notify_socket_connection->set_value(true);
            }
            //create a new peer.
            //note: this may be well a duplicate of an existing one but:
            // 1- it's one that has been closed, and it's better to let it disapear and create a new one.
            // 2- it's one loaded from the database, but we don't know for sure its computerid and identity for now, so the "fusion" will be done later.
            PeerPtr peer = Peer::create(*this, ip, port, Peer::ConnectionState::CONNECTING);
            //try {
                if (initConnection(peer, new_socket, true)) {
                    //FIXME have to listen to the connection from this thread or the socket is closed
                    peer->run();
                    goto return_true;
                } else {
                    goto return_false;
                }
            //}
            //catch (InterruptedException | IOException e) {
            //    // e.printStackTrace();
            //    System.err.println(getPeerId() % 100 << " error in initialization : connection close with "
            //        << peer.getPeerId() % 100 << " (" << peer.getPort() << ")");
            //    peer.close();
            //}
        }
        catch (std::exception e) {
            error(std::string("Error in Connect to : " ) + e.what());
        }
        goto return_false;
        //this is a way to ensure that 
    return_false:
        if (!has_emmitted_promise && notify_socket_connection) {
            notify_socket_connection->set_value(false);
        }
        return false;
    return_true:
        if (!has_emmitted_promise && notify_socket_connection) {
            notify_socket_connection->set_value(true);
        }
        return true;
    }

    PeerList PhysicalServer::getPeersCopy() const {
        const std::lock_guard lock(this->peers_mutex);
        // return copy (made inside the lock)
        PeerList copy = peers;
        return copy;
    }

    void PhysicalServer::removeExactPeer(Peer* peer) {
        { std::lock_guard lock(peers_mutex);
            foreach(peerIt, peers) {
                if (peerIt->get() == peer) {
                    peerIt.erase();
                }
            }
        }
    }
    void PhysicalServer::removeExactPeer(PeerPtr& peer) {
        removeExactPeer(peer.get());
    }

    void PhysicalServer::setPeerId(uint64_t new_peer_id) {
        msg(std::to_string(getPeerId()%100) + " choose my definitive peer id : " + new_peer_id +" ("+(new_peer_id%100)+")");
        getIdentityManager().getSelfPeer()->setPeerId(new_peer_id);
        has_peer_id = true;
    }

    bool PhysicalServer::reconnect() {
        // reconnect all connections
        PeerList oldConnection;
        { std::lock_guard lock(peers_mutex);
            oldConnection = peers;
            peers.clear();
        }
        for (PeerPtr& oldp : oldConnection) {
            if (oldp->isAlive()) {
                oldp->close();
                std::cout << "RECONNECT " << oldp->getIP() + " : " << oldp->getPort() << "\n";
                const std::string& path = oldp->getIP();
                uint16_t port = oldp->getPort();
                std::shared_ptr<PhysicalServer> me = ptr();
                std::thread updaterThread([me, path, port]() {
                    me->connectTo(path, port, 0, {});
                    });
                updaterThread.detach();
            }
        }
        return false;
    }
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //@Override
    //    public int writeBroadcastMessage(uint8_t messageId, uint8_tBuff message) {
    //    int nbEmit = 0;
    //    synchronized(this->clusterIdMananger.getRegisteredPeers()) {
    //        for (Peer peer : this->clusterIdMananger.getRegisteredPeers()) {
    //            if (peer != null && peer.isAlive()) {
    //                std::cout<<"write msg " + messageId + " to " + peer->getPeerId() % 100<<"\n";
    //                writeMessage(peer, messageId, message);
    //                nbEmit++;
    //            } else {
    //                std::cout<<"peer " + peer->getPeerId() % 100 + " is not alive, can't send msg"<<"\n";
    //            }
    //        }
    //    }
    //    return nbEmit;
    //}

    PeerPtr PhysicalServer::getPeerPtr(uint64_t senderId) const {
        {std::lock_guard lock(this->peers_mutex);
            for (const PeerPtr& peer : peers) {
                if (peer->getPeerId() == senderId) {
                    return peer;
                }
            }
        }
        return PeerPtr{};
    }


    void PhysicalServer::registerListener(uint8_t messageId, std::shared_ptr<AbstractMessageManager> listener) {
        std::lock_guard lock{ listeners_mutex };
        std::vector<std::shared_ptr<AbstractMessageManager>>& list = this->listeners[messageId];
        list.push_back(listener);
    }
    void PhysicalServer::unregisterListener(uint8_t messageId, std::shared_ptr<AbstractMessageManager> listener) {
        std::lock_guard lock{ listeners_mutex };
        std::vector<std::shared_ptr<AbstractMessageManager>>& list = this->listeners[messageId];
        foreach(it, list) {
            if (it->get() == listener.get()) {
                it.erase();
            }
        }
    }

    void PhysicalServer::propagateMessage(PeerPtr sender, uint8_t messageId, ByteBuff& message) {
        // propagate message
        std::vector<std::shared_ptr<AbstractMessageManager>>& lst = listeners[messageId];
        for (std::shared_ptr<AbstractMessageManager>& listener : lst) {
            if (!sender->isAlive() && messageId != *UnnencryptedMessageType::CONNECTION_CLOSED) { return; } // can be dead & connected, when closing (and emmiting CLOSE event)
            // receiveMessage get message by value -> it gets a "copy", so it's okay to send the same to evryone.
            listener->receiveMessage(sender, messageId, message);
        }
    }

    void PhysicalServer::init(uint16_t listenPort) {
        this->listen(listenPort);
    }

    std::future<bool> PhysicalServer::connect(const std::string& path, uint16_t port, int64_t timeout_milis) {
        std::shared_ptr<PhysicalServer> me = ptr();
        std::shared_ptr<std::promise<bool>> notify_connection_is_successful{ new std::promise<bool> {} };
        std::future<bool> future = notify_connection_is_successful->get_future();
        std::thread updaterThread([me, path, port, timeout_milis, notify_connection_is_successful]() {
            me->connectTo(path, port, timeout_milis, notify_connection_is_successful);
        });
        updaterThread.detach();
        return future;
    }

    IdentityManager& PhysicalServer::getIdentityManager() {
        return *m_cluster_id_mananger;
    }


    void PhysicalServer::initializeNewCluster() {

        log(std::to_string(getPeerId() % 100 ) + " CHOOSE A NEW COMPUTER ID to 1 "+"\n");
        getIdentityManager().setComputerId(1, IdentityManager::ComputerIdState::DEFINITIVE);
        if (getIdentityManager().getClusterId() == -1)
            getIdentityManager().newClusterId();
        log(std::to_string(getPeerId() % 100 ) + " creation of cluster " + getIdentityManager().getClusterId()+"\n");
        getIdentityManager().requestSave();
    }

    uint16_t PhysicalServer::getComputerId(uint64_t senderId) const {
        if (senderId == this->getPeerId()) return getComputerId();
        PeerPtr p = getPeerPtr(senderId);
        if (p && p->isConnected()) {
            return p->getComputerId();
        }
        return NO_COMPUTER_ID;
    }

    uint64_t PhysicalServer::getPeerIdFromCompId(uint16_t compId) const {
        if (this->getComputerId() == compId) return this->getPeerId();
        { std::lock_guard lock(this->peers_mutex);
            for (const PeerPtr& p : peers) {
                if (p->isAlive() && p->getComputerId() == compId) {
                    return p->getPeerId();
                }
            }
        }
        return -1;
    }

    void PhysicalServer::connect() {
        //TODO: call a ClusterAdminMessageManager method instead, so we try to connect the best first
        for (PeerPtr& falsePeer : getIdentityManager().getLoadedPeers()) {
            std::future<bool> wait = this->connect(falsePeer->getIP(), falsePeer->getPort(), 1000);
        }
    }

    size_t PhysicalServer::getNbPeers() const {
        size_t nb = 0;
        { std::lock_guard lock(this->peers_mutex);
            for (const PeerPtr& p : peers) {
                if (p->isAlive() && p->isConnected()) nb++;
            }
        }
        return nb;
    }

    void PhysicalServer::close() {
        log(std::string("Closing server ") + (this->getPeerId() % 100));
        { std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& peer : peers) {
                if(peer)
                    peer->close();
            }
            if (m_listen_socket) {
                this->m_listen_socket->close();
            }
            this->m_state.disconnect();
        }
    }


} // namespace supercloud
