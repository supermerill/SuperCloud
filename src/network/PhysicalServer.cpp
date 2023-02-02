#include "PhysicalServer.hpp"

#include <chrono>
#include <limits>
#include <thread>

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "ServerIdDb.hpp"
#include "../utils/Utils.hpp"
#include "fs/stubFileSystemManager.hpp"


namespace supercloud {


    PhysicalServer::PhysicalServer(std::shared_ptr<FileSystemManager> fs, const std::filesystem::path& folderPath) :
        myFs(fs), myPeerId(rand_u63()){
        messageManager = ConnectionMessageManager::create(*this);
        clusterIdMananger = std::make_shared<ServerIdDb>(*this, folderPath / "clusterIds.properties");
        clusterIdMananger->load();
        //if (update) {
        //    launchUpdater();
        //}
    }


    uint16_t PhysicalServer::getComputerId() {
        return getServerIdDb().getComputerId();
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

    void PhysicalServer::update() {
        bool quickUpdate = true;
        while (getComputerId() == NO_COMPUTER_ID) {
            if (quickUpdate) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            quickUpdate = false;

            // in case of something went wrong, recheck.
            if (getServerIdDb().getClusterId() != NO_CLUSTER_ID) {
                chooseComputerId();
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        {std::lock_guard lock(this->peers_mutex);
            log(std::to_string(getPeerId() % 100) + "======== peers =======");
            for (PeerPtr& p : this->peers) {
                log(std::to_string(getPeerId() % 100) + p->getIP() + ":" + p->getPort() + " " + p->getComputerId() + " " + p->isAlive());
            }
            log(std::to_string(getPeerId() % 100) + "======================");
        }

        int64_t now = get_current_time_milis(); // just like java (new Date()).getTime();

        //clean peer list
        {std::lock_guard lock(this->peers_mutex);
            //remove peers that are me
        for (auto itPeers = custom::it{ peers }; itPeers.has_next();) {
                Peer* nextP = itPeers.next().get();
                if (nextP->getComputerId() == this->getComputerId()) {
                    std::cerr<<"Error: a peer is me (same computerid) " << this->getComputerId() << "\n";
                    getServerIdDb().removeBadPeer(nextP, this->getComputerId());
                    itPeers.erase();
                } else if (nextP->getKey().getPeerId() == this->getPeerId()) {
                    std::cerr << "Error: a peer is me (same peerId) " << this->getPeerId() << "\n";
                    getServerIdDb().removeBadPeer(nextP, this->getComputerId());
                    itPeers.erase();
                } else { 
                    ++itPeers;
                }
            }
            //remove old connection from same peer
            //remove old connection from unregistered peers
            std::unordered_map<uint16_t, Peer*> states;
            for (const PeerPtr& p : peers) {
                if (states.find(p->getComputerId()) == states.end() || states[p->getComputerId()]->getState() </*lowerThan*/ (p->getState())) {
                    states[p->getComputerId()] = p.get();
                }
            }

            for (auto itPeers = custom::it{ peers }; itPeers.has_next();) {
                Peer& nextP = *itPeers.next();
                if (nextP.getComputerId() <= 0 && nextP.createdAt < now - 2 * 60 * 1000) {
                    std::cerr<<"Error: a peer has no id and is old(>2min) : " << this->getPeerId()<<"\n";
                    if (nextP.isAlive()) {
                        nextP.close();
                    }
                    itPeers.erase();
                } else if (nextP.getState() </*.lowerThan*/ (states[nextP.getComputerId()]->getState())) {
                    std::cerr<<"Error: multiple peer for " << this->getPeerId() << " : deleting the one with status " << nextP.getState()<<"\n";
                    if (nextP.isAlive()) {
                        nextP.close();
                    }
                    itPeers.erase();
                }
            }
        }

        log(std::to_string(getPeerId() % 100) + " update " + myFs->getDrivePath()+"\n");
        for (const PeerPtr& peer : peers) {
            log(std::to_string(getPeerId() % 100 ) + " update peer " + peer->getPeerId() % 100+"\n");
            //if we emit something to someone, wait a bit more for the next update.
            quickUpdate = quickUpdate || peer->ping();
        }
        // toutes les heures
        if (lastDirUpdate + 1000 * 60 * 60 < now) {
            log(std::to_string(getPeerId() % 100 ) + " REQUEST DIR UPDATE"+"\n");
            myFs->requestDirUpdate("/");
            lastDirUpdate = now;
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
                            PeerPtr peer{ new Peer{ *this, new_socket->local_endpoint().address().to_string(), new_socket->local_endpoint().port() } };
                            std::thread clientSocketThread([this, peer, new_socket]() {
                                //try {
                                    if (initConnection(peer, new_socket)) {
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
    bool PhysicalServer::initConnection(PeerPtr peer, std::shared_ptr<tcp::socket> sock) {

        if (peer->connect(sock)) {

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
                        log(std::to_string(getPeerId() % 100 ) + " 'new' accept connection to " + peer->getKey().getPeerId() % 100 + "\n");
                        // peers.put(peer->getKey(), peer);
                        if (!peers.contains(peer)) {
                            log(std::to_string(getPeerId() % 100 ) + " 'warn' PROPAGATE " + peer->getPeerId() % 100 + "!\n");
                            // new peer: propagate!
                            peers.push_back(peer);
                            for (PeerPtr& oldPeer : peers) {
                                if (oldPeer->isAlive()) {
                                   log(std::to_string(getPeerId() % 100 ) + " 'warn' PROPAGATE to " + oldPeer->getPeerId() % 100+"\n");
                                    // MyServerList.get().write(peers, oldPeer);
                                    messageManager->sendServerList(oldPeer->getPeerId(), peers);
                                    oldPeer->flush();
                                }
                            }
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
                if (peer->getPeerId() == getPeerId()) {
                    std::cerr << getPeerId() % 100 << " my peerid is equal to  " << peer->getKey().getPeerId() % 100<<"\n";
                    return rechooseId();
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
            PeerPtr peer;
            for (PeerPtr& searchPeer : getPeersCopy()) {
                if (searchPeer->getPort() == port && searchPeer->getIP() == (ip)) {
                    peer = searchPeer;
                    break;
                }
            }
            // Peer peer = peers.get(new PeerKey(addr.getAddress(), addr.getPort()));
            if (!peer) {
                peer = PeerPtr{ new Peer{*this, addr.address().to_string(), addr.port()} };
            }
            if (peer && !peer->isAlive()) {
                //try {
                    if (myInformalState == ServerConnectionState::JUST_BORN) {
                        myInformalState = ServerConnectionState::CONNECTING;
                    }
                    if (initConnection(peer, tempSock)) {
                        if (myInformalState == ServerConnectionState::CONNECTING) {
                            myInformalState = ServerConnectionState::CONNECTED;
                        }
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

    PeerList PhysicalServer::getPeersCopy() {
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

    bool PhysicalServer::rechooseId() {
        std::cout<<"ERROR: i have to choose an other id...."<<"\n";
        // change id
        myPeerId = rand_u63();
        if (myPeerId <= 0)
            myPeerId = -myPeerId;
        // reconnect all connections
        PeerList oldConnection;
        { std::lock_guard lock(peers_mutex);
            oldConnection = peers;
            peers.clear();
        }
        for (PeerPtr& oldp : oldConnection) {
            oldp->close();
            std::cout<<"RECONNECT " << oldp->getIP() + " : " << oldp->getPort()<<"\n";
            return connectTo(oldp->getIP(), oldp->getPort());
        }
        return false;
    }
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

   //void PhysicalServer::writeEmptyMessage(uint8_t messageId, std::ostream& out) {
   //     //try {
   //         out.write(5);
   //         out.write(5);
   //         out.write(5);
   //         out.write(5);
   //         out.write(messageId);
   //         out.write(messageId);
   //     /*}
   //     catch (IOException e) {
   //         e.printStackTrace();
   //         throw new RuntimeException(e);
   //     }*/
   // }

    //void writeMessage(uint8_t messageId, OutputStream out, ByteBuff& message) {
    //}

    //uint8_tBuff PhysicalServer::readMessage(InputStream in) {
    //    try {
    //        uint8_tBuff buffIn = new uint8_tBuff(4);
    //        in.read(buffIn.array(), 0, 4);
    //        int nbuint8_ts = buffIn.getInt();
    //        buffIn.limit(nbuint8_ts).rewind();
    //        in.read(buffIn.array(), 0, nbuint8_ts);
    //        return buffIn;
    //    }
    //    catch (IOException e) {
    //        e.printStackTrace();
    //        throw new RuntimeException(e);
    //    }
    //}

    //void requestUpdate(uint64_t since) {
    //    // TODO Auto-generated method stub

    //}

    //@Override
    //    public void requestChunk(uint64_t fileId, uint64_t chunkId) {
    //    // TODO Auto-generated method stub

    //}

    //@Override
    //    public void propagateDirectoryChange(uint64_t directoryId, uint8_t[] changes) {
    //    // TODO Auto-generated method stub

    //}

    //@Override
    //    public void propagateFileChange(uint64_t directoryId, uint8_t[] changes) {
    //    // TODO Auto-generated method stub

    //}

    //@Override
    //    public void propagateChunkChange(uint64_t directoryId, uint8_t[] changes) {
    //    // TODO Auto-generated method stub

    //}

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

    //@Override
    //    public bool writeMessage(uint64_t senderId, uint8_t messageId, uint8_tBuff message) {
    //    Peer p = getPeer(senderId);
    //    if (p != null) {
    //        writeMessage(p, messageId, message);
    //        return true;
    //    } else {
    //        return false;
    //    }
    //}

    //public void writeMessage(Peer p, uint8_t messageId, uint8_tBuff message) {
    //    p.writeMessage(messageId, message);
    //}

    //imho, it's dangerous
    //FIXME
    Peer& PhysicalServer::getPeer(uint64_t senderId) {
        {std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& peer : peers) {
                if (peer->getPeerId() == senderId) {
                    return *peer;
                }
            }
        }
        throw new std::runtime_error("Error: getPeer with unknown id");
    }
    PeerPtr PhysicalServer::getPeerPtr(uint64_t senderId) {
        {std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& peer : peers) {
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

    void PhysicalServer::propagateMessage(uint64_t senderId, uint8_t messageId, ByteBuff& message) {
        std::vector<std::shared_ptr<AbstractMessageManager>>& lst = listeners[messageId];
        for (std::shared_ptr<AbstractMessageManager>& listener : lst) {
            listener->receiveMessage(senderId, messageId, message);
        }
    }

    ConnectionMessageManager& PhysicalServer::message() {
        return *messageManager;
    }

    void PhysicalServer::init(uint16_t listenPort) {
        this->listen(listenPort);
    }

    bool PhysicalServer::connect(const std::string& path, uint16_t port) {

        std::thread updaterThread([this, path, port]() {
            this->connectTo(path, port);
        });
        updaterThread.detach();
        return true;
    }

    ServerIdDb& PhysicalServer::getServerIdDb() {
        return *clusterIdMananger;
    }

    void PhysicalServer::chooseComputerId() {
        // do not run this method in multi-thread (i use getServerIdDb() because i'm the only one to use it, to avoid possible dead sync issues)
        { //std::lock_guard choosecomplock(getServerIdDb().synchronize());
            std::lock_guard choosecomplock(clusterIdMananger->synchronize());

            // check if we have a clusterId
            log(std::to_string(getPeerId() % 100 ) + " getServerIdDb().myComputerId=" + getServerIdDb().getComputerId()+"\n");
            if (getServerIdDb().getComputerId() == NO_COMPUTER_ID) {
                //first, wait a bit to let us contact everyone.
                PeerList lstpeerCanConnect;
                { std::lock_guard peerlock(this->peers_mutex);
                    lstpeerCanConnect = peers;
                }
                log(std::to_string(getPeerId() % 100 ) + " lstpeerCanConnect.size=" + lstpeerCanConnect.size()+"\n");
                //log(std::to_string(getPeerId() % 100 ) + " receivedServerList=" + getServerIdDb().getReceivedServerList()+"\n";
                for (PeerPtr& okP : getServerIdDb().getReceivedServerList()) {
                    if (lstpeerCanConnect.contains(okP)) {
                        lstpeerCanConnect.erase_from_ptr(okP);
                    }
                }
                log(std::to_string(getPeerId() % 100 ) + " lstpeerCanConnect.restsize=" + lstpeerCanConnect.size()+"\n");
                if (lstpeerCanConnect.empty()) {
                    // ok, i am connected with everyone i can access and i have received every serverlist.

                    // choose a random one
                    uint16_t choosenId = (uint16_t)(rand_u63()%(std::numeric_limits<uint16_t>::max()));
                    // while it's not already taken
                    while (getServerIdDb().isChoosen(choosenId)) {
                        log(std::to_string(getPeerId() % 100 ) + " ClusterId " + choosenId + " is already taken, i will choose a new one\n");
                        choosenId = (uint16_t)(rand_u63() % (std::numeric_limits<uint16_t>::max()));
                        if (choosenId < 0) {
                            log(std::to_string(getPeerId() % 100 ) + " choosenId " + choosenId + " < 0"+"\n");
                            choosenId = (uint16_t)-choosenId;
                            log(std::to_string(getPeerId() % 100 ) + " now choosenId " + choosenId + " > 0 !!"+"\n");
                        }
                    }

                    log(std::to_string(getPeerId() % 100 ) + " CHOOSE A NEW COMPUTER ID is =" + choosenId+"\n");
                    getServerIdDb().setComputerId( choosenId);
                    // emit this to everyone
                    // writeBroadcastMessage(AbstractMessageManager.GET_SERVER_PUBLIC_KEY, new uint8_tBuff());
                    { std::lock_guard peerlock(this->peers_mutex);
                        log(std::to_string(getPeerId() % 100 ) + " want to send it to " + peers.size()+"\n");
                        for (PeerPtr& peer : peers) {
                            log(std::to_string(getPeerId() % 100 ) + " want to send it to peers " + peer->getPeerId() % 100 + "  " + peer->getComputerId()+"\n");
                            // as we can't emit directly (no message to encode), a request to them should trigger a request from them.
                            getServerIdDb().requestPublicKey(*peer);
                        }
                    }
                }

            } else {
                // yes
                // there are a conflict?

                if (getServerIdDb().getPublicKey(getServerIdDb().getComputerId()) != PublicKey{}
                        && getServerIdDb().getPublicKey(getServerIdDb().getComputerId()) != getServerIdDb().getPublicKey()) {
                    log(std::string("id is already there, with my id : ") + getServerIdDb().getComputerId()+"\n");
                    log(std::string(", my pub key : ") + getServerIdDb().getPublicKey()+"\n");
                    log(std::string(",  their: ") + getServerIdDb().getPublicKey(getServerIdDb().getComputerId()) +"\n");
                    //log(std::string(", my pub key : " + Arrays.toString(getServerIdDb().publicKey.getEncoded())+"\n");
                    //log(std::string(",  their: " + Arrays.toString(getServerIdDb().id2PublicKey.get(getServerIdDb().myComputerId).getEncoded())+"\n");
                    // yes
                    // i choose my id recently?
                    int64_t now = get_current_time_milis(); // just like java (new Date()).getTime();
                    if (now - getServerIdDb().getTimeChooseId() < 10000) {
                        // change!
                        // how: i have to reset all my connections?
                        // getServerIdDb().timeChooseId
                       error(std::to_string(getPeerId() % 100) + " ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.\n");
                        throw std::runtime_error("ERROR! my ComputerId is already taken. Please destroy this instance/peer/server and create an other one.");
                    } else {
                        error(std::to_string(getPeerId() % 100) + " Error, an other server/instance has picked the same ComputerId as me. Both of us should be destroyed and re-created, at a time when we can communicate with each other.\n");
                    }
                } else {
                    // no
                    // nothing todo do ! everything is ok!!
                    log("ComputerId : everything ok, nothing to do\n");
                }

            }
        }

    }

    void PhysicalServer::initializeNewCluster() {
        myInformalState = ServerConnectionState::SOLO;

        log(std::to_string(getPeerId() % 100 ) + " CHOOSE A NEW COMPUTER ID to 1 "+"\n");
        getServerIdDb().setComputerId(1);
        if (getServerIdDb().getClusterId() == -1)
            getServerIdDb().newClusterId();
        log(std::to_string(getPeerId() % 100 ) + " creation of cluster " + getServerIdDb().getClusterId()+"\n");
        getServerIdDb().requestSave();
    }

    uint16_t PhysicalServer::getComputerId(uint64_t senderId) {
        if (senderId == this->myPeerId) return getComputerId();
        PeerPtr p = getPeerPtr(senderId);
        if (p && p->hasState(Peer::PeerConnectionState::CONNECTED_W_AES)) {
            return p->getComputerId();
        }
        return NO_COMPUTER_ID;
    }

    uint64_t PhysicalServer::getPeerIdFromCompId(uint16_t compId) {
        if (this->getComputerId() == compId) return this->myPeerId;
        { std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& p : peers) {
                if (p->isAlive() && p->getComputerId() == compId) {
                    return p->getPeerId();
                }
            }
        }
        return -1;
    }

    int PhysicalServer::connect() {
        int success = 0;
        for (PeerPtr& falsePeer : getServerIdDb().getLoadedPeers()) {
            try {
                if (this->connect(falsePeer->getIP(), falsePeer->getPort())) {
                    success++;
                }
            }
            catch (std::exception e) {
                log(std::string("can't connect to ") + falsePeer->getIP() + " : " + falsePeer->getPort() + "\n");
            }
        }
        return success;
    }

    size_t PhysicalServer::getNbPeers() {
        size_t nb = 0;
        { std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& p : peers) {
                if (p->isAlive() && p->hasState(Peer::PeerConnectionState::CONNECTED_W_AES)) nb++;
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

    bool PhysicalServer::isConnecting() {
        size_t nb = 0;
        { std::lock_guard lock(this->peers_mutex);
            for (PeerPtr& p : peers) {
                if (p->isAlive()) nb++;
            }
        }
        return this->getComputerId() <= 0 && nb > 0;
    }


} // namespace supercloud
