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

    PhysicalServer::PhysicalServer() {
    }

    std::shared_ptr<PhysicalServer> PhysicalServer::createAndInit(std::unique_ptr<Parameters>&& identity_parameters, std::shared_ptr<Parameters> install_parameters) {
        std::shared_ptr<PhysicalServer> ptr = std::shared_ptr<PhysicalServer>{ new PhysicalServer{} };
        ptr->m_cluster_id_mananger = std::make_shared<IdentityManager>(*ptr, std::move(identity_parameters));
        ptr->m_cluster_id_mananger->setInstallParameters(install_parameters);
        ptr->m_cluster_id_mananger->load();
        ptr->m_connection_manager = ConnectionMessageManager::create(ptr, ptr->m_state);
        ptr->m_cluster_admin_manager = ClusterAdminMessageManager::create(*ptr);

        //init a random peer id if i won't reuse the previous one.
        PeerId my_peer_id = ptr->m_cluster_id_mananger->getSelfPeer()->getPeerId();
        if (my_peer_id == 0 || my_peer_id == NO_PEER_ID) {
            ptr->m_cluster_id_mananger->getSelfPeer()->setPeerId(1 + rand_u63());
        }

        //listen? check if it's not better to create it just before we need it.
        ptr->m_listen_socket = ServerSocket::factory->create();

        return ptr;
    }


    PeerId PhysicalServer::getPeerId() const {
        return m_cluster_id_mananger->getSelfPeer()->getPeerId();
    }
    ComputerId PhysicalServer::getComputerId() const {
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
        for (size_t i = 0; i < 100; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (this->m_state.isClosed()) return;
        }
        for (size_t i = 0; i < 500; i++) //FIXME only for testing purpose to reduce clutter, delete
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); //FIXME only for testing purpose to reduce clutter, delete
            if (this->m_state.isClosed()) return;
        }
        log(std::to_string(getPeerId() % 100) + " update, is closed? "+ this->m_state.isClosed());
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
            {std::lock_guard lock(this->m_peers_mutex);
                int nbAlive = 0;
                if ( (m_peers.empty() || !m_peers.front()->isAlive()) && update_check_NO_COMPUTER_ID != m_last_minute_update){
                    error("Error, There is no connection still alive and i still don't have a computer id. Please connect to a valid peer or create a new cluster before calling update.");
                }
            }
            return;
        }

        //std::this_thread::sleep_for(std::chrono::seconds(5));
        int64_t now = get_current_time_milis(); // just like java (new Date()).getTime();
        ByteBuff now_msg = ByteBuff{}.putLong(now).flip();
       
        //clean peer list
        log_peers();
        {std::lock_guard lock(this->m_peers_mutex);
            //remove bad peers
            for (auto itPeers = custom::it{ m_peers }; itPeers.has_next();) {
                PeerPtr& nextP = itPeers.next();
                if (this->getComputerId() != NO_COMPUTER_ID && nextP->getComputerId() == this->getComputerId()) {
                    error(std::string("Error: a peer (")+ (nextP->getPeerId()%100)+") is me (" + (getPeerId() % 100) + ") (same computerid) " + this->getComputerId());
                    getIdentityManager().removeBadPeer(nextP);
                    nextP->close();
                    itPeers.erase();
                } else if (this->hasPeerId() && nextP->getPeerId() == this->getPeerId()) {
                    error(std::string(" Error: a peer (") + (nextP->getPeerId() % 100) +") is me (" + (getPeerId() % 100) + ") (same peerId) ");
                    getIdentityManager().removeBadPeer(nextP);
                    nextP->close();
                    itPeers.erase();
                } else if (!nextP->isAlive()){
                    error(std::to_string(getPeerId() % 100) + std::string(" Error: a peer (") + (nextP->getPeerId() % 100) +") is dead: clean it" );
                    getIdentityManager().removeBadPeer(nextP);
                    nextP->close();
                    itPeers.erase();
                }else if ((nextP->getComputerId() == 0 || nextP->getComputerId() == NO_COMPUTER_ID) && nextP->createdAt < now - 2 * 60 * 1000) {
                    error(std::to_string(getPeerId() % 100) + std::string("Error: a peer ")+ (nextP->getPeerId()%100)+") has no id and is old(>2min) : " );
                    nextP->close();
                    itPeers.erase();
                }
            }

            // remove duplicate peers
            std::unordered_map<ComputerId, size_t> count;
            for (const PeerPtr& p : m_peers) {
                count[p->getComputerId()]++;
            }

            foreach(itPeer, m_peers) {
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
            } else if(peer->isAlive()){
                //propagate only to connectionmessage
                this->m_connection_manager->receiveMessage(peer, *UnnencryptedMessageType::TIMER_SECOND, now_msg.rewind());
            }
        }

        if (now - m_last_minute_update > 1000 * 60) {
            m_last_minute_update = now;
        }
    }

    void PhysicalServer::log_peers(){
        PeerList peers;
        { std::lock_guard lock{ this->m_peers_mutex };
            peers = getPeersCopy();
        }
        PeerPtr me = getPeer();
        { std::lock_guard logl{ *loglock() };
            log(std::to_string(getPeerId() % 100) + " ======== peers =======");
            log(std::to_string(getPeerId() % 100) + " -ME- ip=" + me->getIP() + ":" +getListenPort() + " cid=" + me->getComputerId() + " alive:" + me->isAlive() + " state: " + Peer::connectionStateToString(me->getState()));
            for (PeerPtr& p : peers) {
                log(std::to_string(getPeerId() % 100) + " pid=" + (p->getPeerId() % 100) + " ip=" + p->getIP() + ":" + p->getPort() + " cid=" + p->getComputerId() + " alive:" + p->isAlive() + " state: " + Peer::connectionStateToString(p->getState()));
            }
            log(std::to_string(getPeerId() % 100) + " ======================");
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
                        if (!me->m_listen_socket) {
                            me->m_listen_socket = ServerSocket::factory->create();
                        }
                        me->getIdentityManager().getSelfPeer()->setPort(port);
                        me->m_listen_socket->init(port);
                        log(std::to_string(me->getPeerId() % 100) + " Listen to " + me->m_listen_socket->endpoint().address() + " : " + me->m_listen_socket->endpoint().port() + "\n");

                        while (true) {
                            log(std::to_string(me->getPeerId() % 100) + " wait a connect...\n");
                            //create this connection socket
                            std::shared_ptr<Socket> new_socket = me->m_listen_socket->listen();
                            log(std::to_string(me->getPeerId() % 100) + " connected to a socketserver\n");
                            PeerPtr peer = Peer::create(*me, new_socket->local_endpoint().address(), new_socket->local_endpoint().port(), Peer::ConnectionState::TEMPORARY | Peer::ConnectionState::CONNECTING);
                            std::thread clientSocketThread([me, peer, new_socket]() {
                                try {
                                    //new socket -> update private interface
                                    me->checkSelfPrivateInterface(*new_socket);
                                    //init the connection
                                    if (me->initConnection(peer, new_socket, false)) {
                                        //run the reading thread.
                                        //have to listen to the connection from this thread or the socket is closed
                                        peer->run();
                                    }
                                    //exception in reading, close.
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
                {std::lock_guard lock(this->m_peers_mutex);
                    // check if we already have this peer id.
                    PeerPtr otherPeer;
                    for (const PeerPtr& p : m_peers) {
                        if (p->getPeerId() == peer->getPeerId()) {
                            otherPeer = p;
                            break;
                        }
                    }
                    if (!otherPeer) {
                        log(std::to_string(getPeerId() % 100 ) + " 'new' accept connection to " + peer->getPeerId() % 100 + "\n");
                        // m_peers.put(peer->getKey(), peer);
                        if (!contains(m_peers, peer)) {
                            //log(std::to_string(getPeerId() % 100 ) + " PROPAGATE " + peer->getPeerId() % 100 + "!\n");
                            m_peers.push_back(peer);

                            // new peer: propagate! send this new information to my connected peers => now done in ClusterAdminManager
                            //for (PeerPtr& oldPeer : peers) {
                            //    if (oldPeer->isAlive() && oldPeer->getPeerId() != peer->getPeerId()) {
                            //       log(std::to_string(getPeerId() % 100 ) + " PROPAGATE new peer '"+(peer->getPeerId() % 100)+"' to " + oldPeer->getPeerId() % 100+"\n");
                            //        // MyServerList.get().write(peers, oldPeer);
                            //        messageManager->sendServerList(*oldPeer, getIdentityManager().getRegisteredPeers(), peers);
                            //    }
                            //}
                        } else {
                            m_peers.push_back(peer);
                        }
                        return true;
                    } else {
                        if (otherPeer->isAlive() && !peer->isAlive()) {
                            error(std::to_string( getPeerId() % 100) + " 'warn' , close a new dead connection to "
                                    + std::to_string(peer->getPeerId() % 100) + " is already here.....");
                            peer->close();
                        } else if (!otherPeer->isAlive() && peer->isAlive()) {
                            error(std::to_string( getPeerId() % 100) + " warn,  close an old dead  a connection to "
                                    + std::to_string(peer->getPeerId() % 100) + " is already here.....");
                            otherPeer->close();
                            // m_peers.put(peer->getKey(), peer);
                            m_peers.push_back(peer);
                            return true;
                        } else if (otherPeer->getPeerId() != 0) {
                            if (otherPeer->getPeerId() < getPeerId()) {
                                error(std::to_string( getPeerId() % 100) + " warn, (I AM LEADER) a connection to "
                                        + std::to_string(peer->getPeerId() % 100) + " is already here..... => close it");
                                peer->close();
                            } else {
                                error(std::to_string( getPeerId() % 100) + " warn, (I am not leader) a connection to "
                                        + std::to_string(peer->getPeerId() % 100) + " is already here..... => wait\n");
                                m_peers.push_back(peer);
                                return true;
                            }
                        } else if (peer->getPeerId() != 0) {
                            if (peer->getPeerId() < getPeerId()) {
                                error(std::to_string( getPeerId() % 100) + " warn, (I AM LEADER) a connection to "
                                        + std::to_string(peer->getPeerId() % 100) + " is already here..... => closeit\n");
                                otherPeer->close();
                                // m_peers.put(peer->getKey(), peer);
                                m_peers.push_back(peer);
                                return true;
                            } else {
                                error(std::to_string( getPeerId() % 100) + " warn, (I am not leader) a connection to "
                                        + std::to_string(peer->getPeerId() % 100) + " is already here..... =>donothing\n");
                                m_peers.push_back(peer);
                                return true;
                            }
                        } else {
                            error(std::to_string( getPeerId() % 100) + " warn, an unknown connection to "
                                    + std::to_string(peer->getPeerId() % 100) + " is already here..... => close\n");
                            peer->close();
                        }
                        log(std::to_string(getPeerId() % 100) + " 'old' accept connection to "
                                + std::to_string(peer->getPeerId() % 100));
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
    bool PhysicalServer::connectTo(PeerPtr peer, const std::string& ip, uint16_t port, int64_t timeout_milis, std::shared_ptr<std::promise<bool>> notify_socket_connection) {
        bool has_emmitted_promise = false;
        Peer::ConnectionState state = peer->getState();
        // check if it's not me
        if (this->m_listen_socket && this->m_listen_socket->endpoint().port() == port
            && (m_listen_socket->endpoint().address() == (ip) || ip == ("127.0.0.1"))) {
            log("DON't connect TO ME MYSELF\n");
            goto return_false;
        } else if (peer->isConnected() || (state & Peer::CONNECTING)) {
            error(std::to_string(getPeerId() % 100) + " Error, trying to connect to a peer " + (peer->getPeerId() % 100) + " that is already connected/connecting");
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
            }else{
                //wait
                connect_end.wait();
            }
            if (!new_socket->is_open()) {
                new_socket->cancel();
                goto return_false;
            }
            //new socket -> update private interface
            this->checkSelfPrivateInterface(*new_socket);
            // socket established -> notify the caller
            if (notify_socket_connection) {
                has_emmitted_promise = true;
                notify_socket_connection->set_value(true);
            }
            //create a new peer.
            //note: this may be well a duplicate of an existing one but:
            // 1- it's one that has been closed, and it's better to let it disapear and create a new one.
            // 2- it's one loaded from the database, but we don't know for sure its computerid and identity for now, so the "fusion" will be done later.
            //PeerPtr peer = Peer::create(*this, ip, port, Peer::ConnectionState::TEMPORARY | Peer::ConnectionState::CONNECTING);
            if (!peer) {
                peer = Peer::create(*this, ip, port, Peer::ConnectionState::TEMPORARY | Peer::ConnectionState::CONNECTING);
            } else {
                {
                    std::lock_guard lock{ peer->synchronize() };
                    Peer::ConnectionState state = peer->getState();
                    assert(!(state & Peer::ConnectionState::US));
                    assert(!(state & Peer::ConnectionState::CONNECTING));
                    assert(!(state & Peer::ConnectionState::CONNECTED));
                    // connecting 
                    state = state | Peer::ConnectionState::CONNECTING;
                    // (keep disconnected flag for now)
                    peer->setState(state);
                }
            }
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
        const std::lock_guard lock(this->m_peers_mutex);
        // return copy (made inside the lock)
        PeerList copy = m_peers;
        return copy;
    }

    void PhysicalServer::removeExactPeer(Peer* peer) {
        { std::lock_guard lock(m_peers_mutex);
            foreach(peerIt, m_peers) {
                if (peerIt->get() == peer) {
                    peerIt.erase();
                }
            }
        }
    }
    void PhysicalServer::removeExactPeer(PeerPtr& peer) {
        removeExactPeer(peer.get());
    }

    void PhysicalServer::setPeerId(PeerId new_peer_id) {
        msg(std::to_string(getPeerId()%100) + " choose my definitive peer id : " + new_peer_id +" ("+(new_peer_id%100)+")");
        getIdentityManager().getSelfPeer()->setPeerId(new_peer_id);
    }

    bool PhysicalServer::reconnect() {
        // reconnect all connections
        PeerList oldConnection;
        { std::lock_guard lock(m_peers_mutex);
            oldConnection = m_peers;
            m_peers.clear();
        }
        for (PeerPtr& oldp : oldConnection) {
            if (oldp->isAlive()) {
                oldp->close();
                std::cout << "RECONNECT " << oldp->getIP() + " : " << oldp->getPort() << "\n";
                const std::string& path = oldp->getIP();
                uint16_t port = oldp->getPort();
                std::shared_ptr<PhysicalServer> me = ptr();
                std::thread updaterThread([me, oldp, path, port]() {
                    me->connectTo(oldp, path, port, 2000, {});
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

    PeerPtr PhysicalServer::getPeerPtr(PeerId senderId) const {
        {std::lock_guard lock(this->m_peers_mutex);
            for (const PeerPtr& peer : m_peers) {
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

    void PhysicalServer::propagateMessage(PeerPtr sender, uint8_t messageId, const ByteBuff& message) {
        // propagate message
        //copy the list, to allow for registering/unregistering inside the calls
        std::vector<std::shared_ptr<AbstractMessageManager>> lst;
        {
            std::lock_guard lock{ listeners_mutex };
            lst = listeners[messageId];
        }
        for (std::shared_ptr<AbstractMessageManager>& listener : lst) {
            if (!sender->isAlive() && messageId != *UnnencryptedMessageType::CONNECTION_CLOSED) { return; } // can be dead & connected, when closing (and emmiting CLOSE event)
            // rewind as a ByteBuff's posisition can be changed even if const
            listener->receiveMessage(sender, messageId, message.rewind());
        }
    }

    void PhysicalServer::init(uint16_t listenPort) {
        this->listen(listenPort);
    }

    std::future<bool> PhysicalServer::connect(PeerPtr peer, const std::string& path, uint16_t port, int64_t timeout_milis) {
        std::shared_ptr<PhysicalServer> me = ptr();
        std::shared_ptr<std::promise<bool>> notify_connection_is_successful{ new std::promise<bool> {} };
        std::future<bool> future = notify_connection_is_successful->get_future();
        std::thread updaterThread([me, peer, path, port, timeout_milis, notify_connection_is_successful]() {
            me->connectTo(peer, path, port, timeout_milis, notify_connection_is_successful);
        });
        updaterThread.detach();
        return future;
    }

    IdentityManager& PhysicalServer::getIdentityManager() {
        return *m_cluster_id_mananger;
    }

    void PhysicalServer::checkSelfPrivateInterface(const Socket& socket) {
        //if we are listening to something
        if (this->getIdentityManager().getSelfPeer()->getPort() > 0)
        {
            IdentityManager::PeerData self_data = this->getIdentityManager().getSelfPeerData();
            //if our private inerface hasn't been set.
            if (!self_data.private_interface.has_value()) {
                IdentityManager::PeerData self_data_updated = self_data;
                //use the local ip and our listening port
                self_data_updated.private_interface = { socket.local_endpoint().address(), this->getIdentityManager().getSelfPeer()->getPort() };
                this->getIdentityManager().setPeerData(self_data, self_data_updated);
            }
        }
    }

    void PhysicalServer::initializeNewCluster() {

        log(std::to_string(getPeerId() % 100 ) + " CHOOSE A NEW COMPUTER ID to 1 "+"\n");
        getIdentityManager().setComputerId(1, IdentityManager::ComputerIdState::DEFINITIVE);
        if (getIdentityManager().getClusterId() == -1)
            getIdentityManager().newClusterId();
        log(std::to_string(getPeerId() % 100 ) + " creation of cluster " + getIdentityManager().getClusterId()+"\n");
        getIdentityManager().requestSave();
    }

    ComputerId PhysicalServer::getComputerId(PeerId senderId) const {
        if (senderId == this->getPeerId()) return getComputerId();
        PeerPtr p = getPeerPtr(senderId);
        if (p && p->isConnected()) {
            return p->getComputerId();
        }
        return NO_COMPUTER_ID;
    }
    PeerPtr PhysicalServer::getPeer() {
        return getIdentityManager().getSelfPeer();
    }
    std::string PhysicalServer::getLocalIPNetwork() const {
        std::lock_guard lock(this->m_peers_mutex);
        //try to get a connection
        for (const PeerPtr& peer : m_peers) {
            if (std::string net = peer->getLocalIPNetwork(); net != "") {
                return net;
            }
        }
        return "";
    }

    PeerId PhysicalServer::getPeerIdFromCompId(ComputerId compId) const {
        if (this->getComputerId() == compId) return this->getPeerId();
        { std::lock_guard lock(this->m_peers_mutex);
            for (const PeerPtr& p : m_peers) {
                if (p->isAlive() && p->getComputerId() == compId) {
                    return p->getPeerId();
                }
            }
        }
        return -1;
    }

    void PhysicalServer::connect() {
        //TODO: call a ClusterAdminMessageManager method instead, so we try to connect the best first
        for (PeerPtr& peer : getIdentityManager().getLoadedPeers()) {
            std::future<bool> wait = this->connect(peer, peer->getIP(), peer->getPort(), 1000);
            //discard the future
        }
    }

    size_t PhysicalServer::getNbPeers() const {
        size_t nb = 0;
        { std::lock_guard lock(this->m_peers_mutex);
            for (const PeerPtr& p : m_peers) {
                if (p->isAlive() && p->isConnected()) nb++;
            }
        }
        return nb;
    }

    void PhysicalServer::close() {
        log(std::string("Closing server ") + (this->getPeerId() % 100));
        { std::lock_guard lock(this->m_peers_mutex);
            for (PeerPtr& peer : m_peers) {
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
