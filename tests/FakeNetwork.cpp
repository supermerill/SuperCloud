
#include "FakeNetwork.hpp"

namespace supercloud {

    /////////////////////////////// FakeLocalNetwork //////////////////////////////////////////////////////////////////

    void FakeLocalNetwork::addRouter(std::shared_ptr<FakeRouter> router) {
        routers.push_back(router);
    }
    void FakeLocalNetwork::addSocket(std::weak_ptr<FakeSocket> computer) {
        computers.push_back(computer);
    }
    std::optional<std::weak_ptr<FakeSocket>> FakeLocalNetwork::getSocket(const EndPoint& endpoint) {
        std::optional<std::weak_ptr<FakeSocket>>& result = getLocalSocket(endpoint);
        if (result) return *result;
        //search via routers
        for (std::shared_ptr<FakeRouter>& fakerouter : routers) {
            result = fakerouter->getSocket(endpoint);
            if (result) return *result;
        }
        return {};
    }
    std::optional<std::weak_ptr<FakeSocket>> FakeLocalNetwork::getLocalSocket(const EndPoint& endpoint) {
        auto it_weak_ptr_socket = std::find_if(computers.begin(), computers.end(), [&endpoint](const std::weak_ptr<FakeSocket>& sock_wptr) {
            auto sock = sock_wptr.lock();
            if (sock && sock->get_local_ip_network() == "0.0.0") {
                //only consider the machine-ip, not the network-ip
                //TODO ipv6
                std::string ip_sock = sock->local_endpoint().address();
                size_t pos_last_dot = ip_sock.find_last_of('.') + 1;
                assert(pos_last_dot < ip_sock.size());
                ip_sock = ip_sock.substr(pos_last_dot, ip_sock.size() - pos_last_dot);
                std::string ip_search = sock->local_endpoint().address();
                pos_last_dot = ip_search.find_last_of('.') + 1;
                assert(pos_last_dot < ip_search.size());
                ip_search = ip_search.substr(pos_last_dot, ip_search.size() - pos_last_dot);
                return ip_sock == ip_search && sock->local_endpoint().port() == endpoint.port();
            } else {
                return sock && sock->local_endpoint().address() == endpoint.address() && sock->local_endpoint().port() == endpoint.port();
            }
            });
        if (it_weak_ptr_socket != computers.end()) {
            return std::optional<std::weak_ptr<FakeSocket>>{*it_weak_ptr_socket};
        }
        return {};
    }

    /////////////////////////////// FakeRouter //////////////////////////////////////////////////////////////////

    FakeRouter& FakeRouter::addNetwork(std::shared_ptr<FakeLocalNetwork> net) {
        networks.push_back(net);
        return *this;
    }
    FakeRouter& FakeRouter::set_route(const std::string& network, std::shared_ptr<FakeRouter> by) {
        iptables[network] = by ;
        return *this;
    }
    std::optional<std::weak_ptr<FakeSocket>> FakeRouter::getSocket(const EndPoint& endpoint) {
        for (std::shared_ptr<FakeLocalNetwork>& net : networks) {
            if (std::optional<std::weak_ptr<FakeSocket>> sock = net->getLocalSocket(endpoint); sock.has_value()) {
                return sock.value();
            }
        }
        //can't find, ask from another router
        //first, with full ip
        std::string network_ip = endpoint.address();
        while (!network_ip.empty()) {
            if (auto& def_route = iptables.find(network_ip); def_route != iptables.end()) {
                return def_route->second->getSocket(endpoint);
            }
            if (network_ip.find_last_of('.') == std::string::npos) {
                break;
            }
            network_ip = network_ip.substr(0, network_ip.find_last_of('.'));
        }
        //can't find, use default route
        if (auto& def_route = iptables.find(""); def_route != iptables.end()) {
            return def_route->second->getSocket(endpoint);
        }
        //really can't do anything, return null.
        return {};
    }

/////////////////////////////// FakeSocket //////////////////////////////////////////////////////////////////

    std::string FakeSocket::get_local_ip_network() const {
        return Socket::get_ip_network(this->local_endpoint().address());
    }
    std::future<void> FakeSocket::connect()  {
        std::shared_ptr<std::promise<void>> notify_connection{ new std::promise<void> {} };
        std::future<void> future = notify_connection->get_future();
        std::thread connector([notify_connection, this]() {
            bool result = this->m_server.connect(this);
            notify_connection->set_value();
            });
        connector.detach();
        return future;
    }
    void FakeSocket::write(ByteBuff& buffer) {
        if (!m_open || !m_fifo_mutex) throw write_error("Error: can't write: socket closed");
        size_t write_count = buffer.position();
        {
            std::lock_guard lock{ *m_fifo_mutex };
            if (!m_open) throw write_error("Error: can't write: socket closed");
            this->m_other_side->m_fifo.insert(this->m_other_side->m_fifo.end(), buffer.raw_array() + buffer.position(), buffer.raw_array() + buffer.limit());
            buffer.position(buffer.limit());
            write_count = buffer.position() - write_count;
            //log(this->local_endpoint().address() + ":" + this->local_endpoint().port() + " send " + write_count + " bytes to " + m_other_side->local_endpoint().address() + ":" + m_other_side->local_endpoint().port());
            this->m_other_side->m_fifo_available.release(write_count);
        }
    }
    size_t FakeSocket::read(ByteBuff& buffer) {
        if (m_fifo_available.id == "")
            m_fifo_available.id = this->local_endpoint().address() + ":" + this->local_endpoint().port();
        if (!m_open || !m_fifo_mutex) throw read_error("Error: can't read: socket closed");


        if (!m_open) error("Error: throw exception don't work!");

        size_t old_pos = buffer.position();
        m_fifo_available.acquire(buffer.available());
        {
            std::lock_guard lock{ *m_fifo_mutex };
            if (!m_open) throw read_error("Error: can't read: socket closed");
            if (!m_open) error("Error: throw exception don't work!");
            assert(this->m_fifo.size() >= buffer.available());
            while (buffer.available() > 0 && !this->m_fifo.empty()) {
                buffer.put(this->m_fifo.front());
                this->m_fifo.pop_front();
            }
        }
        //log(this->local_endpoint().address() + ":" + this->local_endpoint().port() + " read " + (buffer.position() - old_pos) + " bytes from " + m_other_side->local_endpoint().address() + ":" + m_other_side->local_endpoint().port());
        return buffer.position() - old_pos;
    }

    void FakeSocket::ask_for_connect(FakeSocket* other_side) {
        //create the new socket server-side.
        std::shared_ptr<FakeSocket> new_socket = std::shared_ptr<FakeSocket>{ new FakeSocket{EndPoint{m_listen_from.address(), next_port.fetch_add(1)}, other_side->m_listen_from, m_server } };
        //set new port on both (see above and below)
        other_side->m_listen_from = EndPoint{ other_side->m_listen_from.address(), next_port.fetch_add(1) };
        new_socket->m_connect_to = other_side->m_listen_from;
        other_side->m_connect_to = new_socket->m_listen_from;
        //create connection
        new_socket->m_other_side = other_side;
        other_side->m_other_side = new_socket.get();
        new_socket->m_fifo_mutex.reset(new std::mutex{});
        other_side->m_fifo_mutex = new_socket->m_fifo_mutex;
        //reset fields that may be problematic
        new_socket->m_fifo_available.drain();
        other_side->m_fifo_available.drain();
        new_socket->m_fifo.clear();
        other_side->m_fifo.clear();
        //set sockets as open
        {std::lock_guard lock{ *new_socket->m_fifo_mutex };
            new_socket->m_open = true;
            other_side->m_open = true;
        }
        // propagate to server thread.
        m_server.ask_listen(new_socket);
        //return true;
    }

    void FakeSocket::close() {
        if (m_open && this->m_fifo_mutex) {
            std::lock_guard lock{ *this->m_fifo_mutex };
            m_open = false;
            if (m_other_side) {
                this->m_other_side->m_open = false;
                this->m_other_side->m_other_side = nullptr;
                this->m_other_side->m_fifo_available.release(uint32_t(-1));
            }
            m_other_side = nullptr;
            this->m_fifo_available.release(uint32_t(-1));
        }
    };

    /////////////////////////////// FakeServerSocket //////////////////////////////////////////////////////////////////

    void FakeServerSocket::init(uint16_t port)  {
        ServerSocket::init(port);
        m_listener.reset(new FakeSocket{ m_endpoint, EndPoint{"",0}, *this });
        for (auto& net : m_fake_networks) {
            net->addSocket(std::weak_ptr{ m_listener });
        }
    }
    std::shared_ptr<Socket> FakeServerSocket::listen() {
        //clean
        temp_new_socket.reset();
        // allow our listening FakeSocket to accept a connection
        wait_for_listen.release();
        //now block until listener has a connection;
        wait_for_connect.acquire();
        // return the newly socket created by our listening FakeSocket
        return temp_new_socket;
    }
    void FakeServerSocket::ask_listen(std::shared_ptr<FakeSocket> socket_connected) {
        // wait for server socket to listen
        wait_for_listen.acquire();
        // create & share your new socket
        temp_new_socket = socket_connected;
        //release it
        wait_for_connect.release();
    }
    std::shared_ptr<Socket> FakeServerSocket::client_socket(const EndPoint& to)  {
        std::shared_ptr<Socket> connection_socket = std::shared_ptr<Socket>{ new FakeSocket{ EndPoint{m_endpoint.address(), 0}, to, *this } };
        return connection_socket;
    }
    bool FakeServerSocket::connect(FakeSocket* to_connect) {
        assert(!m_fake_networks.empty());
        //choose an interface
        std::shared_ptr<FakeLocalNetwork> interface_to_use;
        for (auto& net : m_fake_networks) {
            if (to_connect->get_local_ip_network() == net->getNetworkIp()) {
                interface_to_use = net;
                break;
            }
        }
        if (!interface_to_use) {
            interface_to_use = m_fake_networks.front();
        }
        std::optional<std::weak_ptr<FakeSocket>> listener = interface_to_use->getSocket(to_connect->request_endpoint());
        if (listener.has_value()) {
            if (auto ptr = listener.value().lock(); ptr) {
                ptr->ask_for_connect(to_connect);
                return true;
            }
        }
        return false;
    }
}
