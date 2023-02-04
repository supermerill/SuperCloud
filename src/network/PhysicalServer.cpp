#include "PhysicalServer.hpp"

#include <chrono>
#include <limits>
#include <thread>

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "IdentityManager.hpp"
#include "../utils/Utils.hpp"
#include "fs/stubFileSystemManager.hpp"
#include "ConnectionMessageManager.hpp"
#include "ClusterAdminMessageManager.hpp"


namespace supercloud {


    PhysicalServer::PhysicalServer(std::shared_ptr<FileSystemManager> fs, const std::filesystem::path& folderPath) :
        myFs(fs), myPeerId(rand_u63()), has_peer_id(false){
        clusterIdMananger = std::make_shared<IdentityManager>(*this, folderPath / "clusterIds.properties");
        clusterIdMananger->load();
        connectionManager = ConnectionMessageManager::create(*this);
        clusterAdminManager = ClusterAdminMessageManager::create(*this);
    }


    uint16_t PhysicalServer::getComputerId() const {
        return clusterIdMananger->getComputerId();
    }

    void PhysicalServer::launchUpdater() {
        if (!this->hasUpdaterThread) {
            this->hasUpdaterThread = true;
            std::thread updaterThread([this](){
                std::cout << "launchUpdater launched\n";
                while (true) {
                    this->update();
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
            if (update_check_NO_COMPUTER_ID == 0 && last_minute_update != 0) {
                update_check_NO_COMPUTER_ID = last_minute_update;
            }
            {std::lock_guard lock(this->peers_mutex);
                int nbAlive = 0;
                if ( (peers.empty() || !peers.front()->isAlive()) && update_check_NO_COMPUTER_ID != last_minute_update){
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
                    std::cerr<<"Error: a peer is me (same computerid) " << this->getComputerId() << "\n";
                    getIdentityManager().removeBadPeer(nextP.get(), this->getComputerId());
                    nextP->close();
                    itPeers.erase();
                } else if (this->hasPeerId() && nextP->getKey().getPeerId() == this->getPeerId()) {
                    std::cerr << "Error: a peer is me (same peerId) " << this->getPeerId() << "\n";
                    getIdentityManager().removeBadPeer(nextP.get(), this->getComputerId());
                    nextP->close();
                    itPeers.erase();
                } else if (!nextP->isAlive()){
                    std::cerr << "Error: a peer is me (same peerId) " << this->getPeerId() << "\n";
                    getIdentityManager().removeBadPeer(nextP.get(), this->getComputerId());
                    nextP->close();
                    itPeers.erase();
                }else if ((nextP->getComputerId() == 0 || nextP->getComputerId() == NO_COMPUTER_ID) && nextP->createdAt < now - 2 * 60 * 1000) {
                    std::cerr << "Error: a peer has no id and is old(>2min) : " << this->getPeerId() << "\n";
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

        log(std::to_string(getPeerId() % 100) + " update " + myFs->getDrivePath()+"\n");
        for (const PeerPtr& peer : copy) {
            log(std::to_string(getPeerId() % 100 ) + " update peer " + peer->getPeerId() % 100+"\n");
            // If the peer is fully connected, emit timer message.
            if (peer->isConnected()) {
                this->propagateMessage(peer, *UnnencryptedMessageType::TIMER_SECOND, now_msg);
                if (now - last_minute_update > 1000 * 60) {
                    this->propagateMessage(peer, *UnnencryptedMessageType::TIMER_MINUTE, now_msg);
                }
            }
        }
        // toutes les heures
        if (lastDirUpdate + 1000 * 60 * 60 < now) {
            log(std::to_string(getPeerId() % 100 ) + " REQUEST DIR UPDATE"+"\n");
            myFs->requestDirUpdate("/");
            lastDirUpdate = now;
        }

        if (now - last_minute_update > 1000 * 60) {
            last_minute_update = now;
        }
    }

    /**
    * Create a new thread to listen to connection.
    * It will create a new one for each new connection.
    *
    */
    void PhysicalServer::listen(uint16_t port) {
        if (!hasSocketListener) {
            //try {
                hasSocketListener = true;
                std::thread socketListenerThread([this, port]() {
                    //try {
                    boost::asio::io_service io_service;
                    boost::asio::ip::tcp::endpoint tcp_endpoint(boost::asio::ip::address_v4::any(), port);
                    mySocket = std::unique_ptr<tcp::acceptor>{ new tcp::acceptor{ io_service, tcp_endpoint } };
                    log(std::string("Listen to ") + tcp_endpoint.address().to_string() + " : " + tcp_endpoint.port() + "\n");

                        while (true) {
                            log("wait a connect...\n");
                            std::shared_ptr<tcp::socket> new_socket{ new tcp::socket{io_service} };
                            //create this connection socket
                            mySocket->accept(*new_socket);
                            log("connected to a socketserver\n");
                            PeerPtr peer = Peer::create(*this, new_socket->local_endpoint().address().to_string(), new_socket->local_endpoint().port());
                            std::thread clientSocketThread([this, peer, new_socket]() {
                                //try {
                                    if (initConnection(peer, new_socket, false)) {
                                        if (myInformalState == ServerConnectionState::SOLO) {
                                            myInformalState = ServerConnectionState::HAS_FRIENDS;
                                        }
                                        //FIXME have to listen to the connection from this thread or the socket is closed
                                        peer->run();
                                    }
                                    peer->close();
                                //}
                                //catch (InterruptedException | IOException e) {
                                //    e.printStackTrace();
                                //}
                            });
                            clientSocketThread.detach();
                        }
                    /*}
                    catch (IOException ex) {
                        ex.printStackTrace();
                    }*/
                    hasSocketListener = false;
                });
                socketListenerThread.detach();
            //}
            //catch (std::exception e) {
               // e.printStackTrace();
            //}
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
    bool PhysicalServer::initConnection(PeerPtr peer, std::shared_ptr<tcp::socket> sock, bool initiated_by_me) {

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
                                    << peer->getKey().getPeerId() % 100 << " is already here....."<<"\n";
                            peer->close();
                        } else if (!otherPeer->isAlive() && peer->isAlive()) {
                            std::cerr << getPeerId() % 100 << " warn,  close an old dead  a connection to "
                                    << peer->getKey().getPeerId() % 100 << " is already here....."<<"\n";
                            otherPeer->close();
                            // peers.put(peer->getKey(), peer);
                            peers.push_back(peer);
                            return true;
                        } else if (otherPeer->getKey().getPeerId() != 0) {
                            if (otherPeer->getKey().getPeerId() < getPeerId()) {
                                std::cerr << getPeerId() % 100 << " warn, (I AM LEADER) a connection to "
                                        << peer->getKey().getPeerId() % 100 << " is already here....."<<"\n";
                                peer->close();
                            } else {
                                std::cerr << getPeerId() % 100 << " warn, (I am not leader) a connection to "
                                        << peer->getKey().getPeerId() % 100 << " is already here.....\n";
                                peers.push_back(peer);
                                return true;
                            }
                        } else if (peer->getKey().getPeerId() != 0) {
                            if (peer->getKey().getPeerId() < getPeerId()) {
                                std::cerr << getPeerId() % 100 << " warn, (I AM LEADER) a connection to "
                                        << peer->getKey().getPeerId() % 100 << " is already here.....\n";
                                otherPeer->close();
                                // peers.put(peer->getKey(), peer);
                                peers.push_back(peer);
                                return true;
                            } else {
                                std::cerr << getPeerId() % 100 << " warn, (I am not leader) a connection to "
                                        << peer->getKey().getPeerId() % 100 << " is already here.....\n";
                                peers.push_back(peer);
                                return true;
                            }
                        } else {
                            std::cerr << getPeerId() % 100 << " warn, an unknown connection to "
                                    << peer->getKey().getPeerId() % 100 << " is already here.....\n";
                            peer->close();
                        }
                        std::cout << getPeerId() % 100 << " 'old' accept connection to "
                                << peer->getKey().getPeerId() % 100<<"\n";
                    }
                }

                // test if id is ok
                //TODO check if still needed
                if (peer->getPeerId() == getPeerId() && hasPeerId()) {
                    error(std::to_string(getPeerId() % 100) + " my peerid is equal to  " + peer->getKey().getPeerId() % 100 + "\n");
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
    bool PhysicalServer::connectTo(const std::string& ip, uint16_t port) {
        // check if it's not me
        if (mySocket->local_endpoint().port() == port
            && (mySocket->local_endpoint().address().to_string() == (ip) || ip == ("127.0.0.1"))) {
            log("DON't connect TO ME MYSELF\n");
            return false;
        } else {
            // std::cout<<mySocket.getLocalPort()<<" =?= "<<port<<"\n";
            // std::cout<<mySocket.getInetAddress().getHostAddress()<<" =?= "<<ip<<"\n";
            // std::cout<<mySocket.getInetAddress().getHostName()<<" =?= "<<ip<<"\n";
        }
        log(std::to_string(getPeerId() % 100 ) + " I am " + mySocket->local_endpoint().address().to_string() + ":" + mySocket->local_endpoint().port()+"\n");
        log(std::to_string(getPeerId() % 100 ) + " I want to CONNECT with " + ip + ":" + port+"\n");
        boost::asio::io_service ios;
        tcp::endpoint addr(boost::asio::ip::address::from_string(ip), port);
        std::shared_ptr<tcp::socket> tempSock(new tcp::socket(ios));
        //tempSock->set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 200 });
        //try {
            tempSock->connect(addr);
            //create a new peer.
            //note: this may be well a duplicate of an existing one but:
            // 1- it's one that has been closed, and it's better to let it disapear and create a new one.
            // 2- it's one loaded from the database, but we don't know for sure its cimputerid and identity for now, so the "fusion" will be done later.
            PeerPtr peer = Peer::create(*this, addr.address().to_string(), addr.port());
            if (peer && !peer->isAlive()) {
                //try {
                    if (myInformalState == ServerConnectionState::JUST_BORN) {
                        myInformalState = ServerConnectionState::CONNECTING;
                    }
                    if (initConnection(peer, tempSock, true)) {
                        //FIXME have to listen to the connection from this thread or the socket is closed
                        peer->run();
                        return true;
                    } else {
                        return false;
                    }
                //}
                //catch (InterruptedException | IOException e) {
                //    // e.printStackTrace();
                //    System.err.println(getPeerId() % 100 << " error in initialization : connection close with "
                //        << peer.getPeerId() % 100 << " (" << peer.getPort() << ")");
                //    peer.close();
                //}
            } else {
                log(std::to_string(getPeerId() % 100 ) + " already CONNECTED with " + port+"\n");
                return true;
            }
        /*}
        catch (IOException e) {
            e.printStackTrace();
            try {
                tempSock.close();
            }
            catch (IOException e1) {
            }
            throw new RuntimeException(e);
        }*/
        return false;
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
                if ((*peerIt).get() == peer) {
                    peerIt.erase();
                }
            }
        }
    }
    void PhysicalServer::removeExactPeer(PeerPtr& peer) {
        removeExactPeer(peer.get());
    }

    //public File getJar(String name) {
    //    // list available files
    //    // java.io.File dir = new File(jarFolder);
    //    // File finded = null;
    //    // for(File f : dir.listFiles()){
    //    // if(f.getName().equals(name+".jar")){
    //    // finded = f;
    //    // break;
    //    // }
    //    // }

    //    return new File(jarFolder + "/" + name + ".jar");
    //}

    //public File getOrCreateJar(String name) {
    //    // list available files
    //    java.io.File file = getJar(name);
    //    if (file == null) {
    //        file = new File(jarFolder + "/" + name + ".jar");
    //        try {
    //            file.createNewFile();
    //        }
    //        catch (IOException e) {
    //            e.printStackTrace();
    //        }
    //    }

    //    return file;
    //}

    //@SuppressWarnings("rawtypes")
    //    public void runAlgo(String jarName, String className) {
    //    // TODO: maybe use https://github.com/kamranzafar/JCL to isolate each run
    //    URLClassLoader child;
    //    try {
    //        child = new URLClassLoader(new URL[]{ getJar(jarName).toURI().toURL() }, this->getClass().getClassLoader());

    //        Class classToLoad = Class.forName(className, true, child);
    //        Object instance = classToLoad.newInstance();
    //        @SuppressWarnings("unchecked")
    //            Method method = classToLoad.getDeclaredMethod("run", PhysicalServer.class);
    //        /* Object result = */ method.invoke(instance, this);
    //    }
    //    catch (MalformedURLException | ClassNotFoundException | NoSuchMethodException | SecurityException
    //        | IllegalAccessException | IllegalArgumentException | InvocationTargetException
    //        | InstantiationException e) {
    //        e.printStackTrace();
    //    }

    //}

    FileSystemManager* PhysicalServer::getFileSystem() {
        return myFs.get();
    }


    void PhysicalServer::setPeerId(uint64_t new_peer_id) {
        msg(std::to_string(getPeerId()%100) + " choose my new peer id : " + new_peer_id +" ("+(new_peer_id%100)+")");
        myPeerId = new_peer_id;
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
                std::thread updaterThread([this, path, port]() {
                    this->connectTo(path, port);
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
    //                std::cout<<"write msg " + messageId + " to " + peer->getKey().getPeerId() % 100<<"\n";
    //                writeMessage(peer, messageId, message);
    //                nbEmit++;
    //            } else {
    //                std::cout<<"peer " + peer->getKey().getPeerId() % 100 + " is not alive, can't send msg"<<"\n";
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
        std::vector<std::shared_ptr<AbstractMessageManager>>& list = this->listeners[messageId];
        list.push_back(listener);
    }

    void PhysicalServer::propagateMessage(PeerPtr sender, uint8_t messageId, ByteBuff& message) {
        // update state if needed (sorry to put that here, should be in its own listener)
        if (messageId == *UnnencryptedMessageType::NEW_CONNECTION) {
            if (myInformalState == ServerConnectionState::CONNECTING) {
                myInformalState = ServerConnectionState::CONNECTED;
            }
        }
        // propagate message
        std::vector<std::shared_ptr<AbstractMessageManager>>& lst = listeners[messageId];
        for (std::shared_ptr<AbstractMessageManager>& listener : lst) {
            if (!sender->isAlive()) { return; }
            // receiveMessage get message by value -> it gets a "copy", so it's okay to send the same to evryone.
            listener->receiveMessage(sender, messageId, message);
        }
    }

    void PhysicalServer::init(uint16_t listenPort) {
        this->listen(listenPort);
    }

    void PhysicalServer::connect(const std::string& path, uint16_t port) {

        std::thread updaterThread([this, path, port]() {
            this->connectTo(path, port);
        });
        updaterThread.detach();
    }

    IdentityManager& PhysicalServer::getIdentityManager() {
        return *clusterIdMananger;
    }


    void PhysicalServer::initializeNewCluster() {
        myInformalState = ServerConnectionState::SOLO;

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
        for (PeerPtr& falsePeer : getIdentityManager().getLoadedPeers()) {
            this->connect(falsePeer->getIP(), falsePeer->getPort());
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
        { std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& peer : peers) {
                if(peer)
                    peer->close();
            }
            if (mySocket) {
                mySocket->close();
            }
            this->myInformalState = ServerConnectionState::DISCONNECTED;
        }
    }


} // namespace supercloud
