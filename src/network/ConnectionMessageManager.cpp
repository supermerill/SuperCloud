#include "ConnectionMessageManager.hpp"
#include "ServerIdDb.hpp"

#include <chrono>

namespace supercloud{

    void ConnectionMessageManager::register_listener() {
        //		clusterManager.registerListener(GET_SERVER_ID, this); passed directly by peer
        //		clusterManager.registerListener(GET_LISTEN_PORT, this); passed directly by peer
        clusterManager.registerListener(GET_SERVER_LIST, this->me);
        clusterManager.registerListener(GET_SERVER_PUBLIC_KEY, this->me);
        clusterManager.registerListener(GET_VERIFY_IDENTITY, this->me);
        clusterManager.registerListener(GET_SERVER_AES_KEY, this->me);
        //		clusterManager.registerListener(SEND_LISTEN_PORT, this); used directly by peer
        //		clusterManager.registerListener(SEND_SERVER_ID, this); used directly by peer
        clusterManager.registerListener(SEND_SERVER_LIST, this->me);
        clusterManager.registerListener(SEND_SERVER_PUBLIC_KEY, this->me);
        clusterManager.registerListener(SEND_VERIFY_IDENTITY, this->me);
        clusterManager.registerListener(SEND_SERVER_AES_KEY, this->me);
    }

    void ConnectionMessageManager::receiveMessage(uint64_t senderId, uint8_t messageId, ByteBuff& message) {
        log(std::to_string(clusterManager.getPeerId() % 100) + " receive message from " + senderId % 100);
        if (messageId == AbstractMessageManager::GET_SERVER_PUBLIC_KEY) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " receive GET_SERVER_PUBLIC_KEY from " + senderId % 100);
            clusterManager.getServerIdDb().sendPublicKey(clusterManager.getPeer(senderId));
        }
        if (messageId == AbstractMessageManager::SEND_SERVER_PUBLIC_KEY) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " receive SEND_SERVER_PUBLIC_KEY from " + senderId % 100);
            clusterManager.getServerIdDb().receivePublicKey(clusterManager.getPeer(senderId), message);
        }
        if (messageId == AbstractMessageManager::GET_VERIFY_IDENTITY) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " receive GET_VERIFY_IDENTITY from " + senderId % 100);
            //			sendIdentity(p, createMessageForIdentityCheck(p, true), true);
            clusterManager.getServerIdDb().answerIdentity(clusterManager.getPeer(senderId), message);
        }
        if (messageId == AbstractMessageManager::SEND_VERIFY_IDENTITY) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " receive SEND_VERIFY_IDENTITY from " + senderId % 100);
            clusterManager.getServerIdDb().receiveIdentity(clusterManager.getPeerPtr(senderId), message);
        }
        if (messageId == AbstractMessageManager::GET_SERVER_AES_KEY) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " receive GET_SERVER_AES_KEY from " + senderId % 100);
            clusterManager.getServerIdDb().sendAesKey(clusterManager.getPeer(senderId), ServerIdDb::AES_PROPOSAL);
        }
        if (messageId == AbstractMessageManager::SEND_SERVER_AES_KEY) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " receive SEND_SERVER_AES_KEY from " + senderId % 100);
            clusterManager.getServerIdDb().receiveAesKey(clusterManager.getPeer(senderId), message);
        }
        if (messageId == GET_SERVER_LIST) {
            log(std::to_string(clusterManager.getPeerId() % 100) + "he ( " + senderId % 100 + " ) want my server list");
            {std::lock_guard lock(this->clusterManager.getServerIdDb().synchronize());
                sendServerList(senderId, this->clusterManager.getServerIdDb().getRegisteredPeers());
            }
        }
        if (messageId == SEND_LISTEN_PORT) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " received SEND_LISTEN_PORT from " + senderId % 100);
            clusterManager.getPeer(senderId).setPort(message.getInt());
        }
        if (messageId == SEND_SERVER_LIST) {
            log(std::to_string(clusterManager.getPeerId() % 100) + " received SEND_SERVER_LIST from " + senderId % 100);

            //			System.out.println(p.getMyServer().getId()%100+" read "+myId+" for "+p.getKey().getOtherServerId()%100);
            short senderComputerId = message.getShort();
            //add this id in our list, to be sure we didn't use it and we can transmit it.
            {
                std::lock_guard lock(this->clusterManager.getServerIdDb().synchronize());
                log(std::to_string(clusterManager.getPeerId() % 100) + " receive peer computerId:  " + senderComputerId + " from " + senderId % 100);
                this->clusterManager.getServerIdDb().addPeer(senderComputerId);
            }
            size_t nb = message.getTrailInt();
            for (int i = 0; i < nb; i++) {
                long id = message.getLong();
                std::string ip = message.getUTF8();
                int port = message.getTrailInt();
                short computerId = message.getShort();

                //add this id in our list, to be sure we didn't use it and we can transmit it.
                {
                    std::lock_guard lock(this->clusterManager.getServerIdDb().synchronize());
                    log(std::to_string(clusterManager.getPeerId() % 100) + " receive a distant computerId:  " + computerId + " of " + id % 100 + " from " + senderId % 100);
                    this->clusterManager.getServerIdDb().addPeer(computerId);
                }
                //				System.out.println(p.getMyServer().getId()%100+" i have found "+ip+":"+ port);


                //					InetSocketAddress addr = new InetSocketAddress(ip,port);
                if (computerId > 0 && computerId != clusterManager.getComputerId() && computerId != senderComputerId
                    && id != clusterManager.getPeerId() && id != senderId) {
                    //note: connectTO can throw exception if it can't be joinable.
                    //new Thread(()->clusterManager.connectTo(ip, port)).start();
                    //std::thread t([clusterManager&, ip&, port]() { clusterManager.connectTo(ip, port); });
                    std::thread t([this, ip, port]() { this->clusterManager.connect(ip, port); });
                    t.detach();
                }
            }
            clusterManager.getServerIdDb().addToReceivedServerList(this->clusterManager.getPeerPtr(senderId));
            clusterManager.chooseComputerId();
        }

    }


    void ConnectionMessageManager::sendServerList(uint64_t sendTo, std::vector<PeerPtr> list) {
        ByteBuff buff;
        //put our id
        buff.putShort(clusterManager.getServerIdDb().getComputerId());
        //put data
        buff.putTrailInt(list.size());
        for (PeerPtr& peer : list) {
            buff.putLong(peer->getPeerId());
            buff.putUTF8(peer->getIP());
            buff.putTrailInt(peer->getPort());
            buff.putShort(peer->getComputerId());
            //				log(/*serv.getListenPort()+*/" SEND SERVER "+peer.getPort()+ " to "+p.getKey().getPort());
        }
        buff.flip();
        clusterManager.getPeer(sendTo).writeMessage(SEND_SERVER_LIST, buff);
    }


    void ConnectionMessageManager::sendServerId(Peer& peer) {
        ByteBuff buff;
        buff.putLong(clusterManager.getPeerId());
        buff.putLong(clusterManager.getServerIdDb().getClusterId());
        //clusterManager.writeMessage(peer, SEND_SERVER_ID, buff.flip());
        peer.writeMessage(SEND_SERVER_ID, buff.flip());
    }


    void ConnectionMessageManager::sendListenPort(Peer& peer) {
        ByteBuff buff;
        buff.putInt(clusterManager.getListenPort());
        peer.writeMessage(SEND_LISTEN_PORT, buff.flip());
    }


} // namespace supercloud
