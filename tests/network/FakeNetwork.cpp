
#include "FakeNetwork.hpp"

namespace supercloud {

    /////////////////////////////// FakeLocalNetwork //////////////////////////////////////////////////////////////////

    void FakeLocalNetwork::addRouter(std::shared_ptr<FakeRouter> router) {
        routers.push_back(router);
    }
    void FakeLocalNetwork::addSocket(FakeSocket* computer) {
        computers.push_back(computer);
    }
    std::optional<FakeSocket*> FakeLocalNetwork::getSocket(const EndPoint& endpoint) {
        std::optional<FakeSocket*>& result = getLocalSocket(endpoint);
        if (result) return *result;
        //search via routers
        for (std::shared_ptr<FakeRouter>& fakerouter : routers) {
            result = fakerouter->getSocket(endpoint);
            if (result) return *result;
        }
        return {};
    }
    std::optional<FakeSocket*> FakeLocalNetwork::getLocalSocket(const EndPoint& endpoint) {
        auto it = std::find_if(computers.begin(), computers.end(), [&endpoint](const FakeSocket* sock) {
            return sock->local_endpoint().address() == endpoint.address() && sock->local_endpoint().port() == endpoint.port();
            });
        if (it != computers.end()) {
            return *it;
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
    std::optional<FakeSocket*> FakeRouter::getSocket(const EndPoint& endpoint) {
        for (std::shared_ptr<FakeLocalNetwork>& net : networks) {
            if (std::optional<FakeSocket*> sock = net->getLocalSocket(endpoint); sock.has_value()) {
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
        size_t write_count = buffer.position();
        {
            std::lock_guard lock(this->m_other_side->m_fifo_mutex);
            if (!open) throw new write_error("Error: can't write: socket closed");
            this->m_other_side->m_fifo.insert(this->m_other_side->m_fifo.end(), buffer.raw_array() + buffer.position(), buffer.raw_array() + buffer.limit());
            buffer.position(buffer.limit());
            write_count = buffer.position() - write_count;
        }
        this->m_other_side->m_fifo_available.release(write_count);
    }
    size_t FakeSocket::read(ByteBuff& buffer) {
        if (!open) throw new read_error("Error: can't read: socket closed");
        size_t old_pos = buffer.position();
        m_fifo_available.acquire(buffer.available());
        {
            std::lock_guard lock(this->m_fifo_mutex);
            if (!open) throw new read_error("Error: can't read: socket closed");
            assert(this->m_fifo.size() >= buffer.available());
            while (buffer.available() > 0 && !this->m_fifo.empty()) {
                buffer.put(this->m_fifo.front());
                this->m_fifo.pop_front();
            }
        }
        return buffer.position() - old_pos;
    }

    void FakeSocket::ask_for_connect(FakeSocket* other_side) {
        //create the new socket server-side.
        std::shared_ptr<FakeSocket> new_socket = std::shared_ptr<FakeSocket>{ new FakeSocket{EndPoint{m_listen_from.address(), next_port.fetch_add(1)}, m_connect_to, m_server } };
        //set new port on both (see above and below)
        other_side->m_listen_from = EndPoint{ other_side->m_listen_from.address(), next_port.fetch_add(1) };
        //create connection
        new_socket->m_other_side = other_side;
        other_side->m_other_side = new_socket.get();
        //set sockets as open
        {std::lock_guard lock(new_socket->m_fifo_mutex);
            new_socket->open = true;
        }
        {std::lock_guard lock(other_side->m_fifo_mutex);
            other_side->open = true;
        }
        // propagate to server thread.
        m_server.ask_listen(new_socket);
        //return true;
    }

    /////////////////////////////// FakeServerSocket //////////////////////////////////////////////////////////////////

    void FakeServerSocket::init(uint16_t port)  {
        ServerSocket::init(port);
        listener.reset(new FakeSocket{ m_endpoint, EndPoint{"",0}, *this });
        m_fake_network->addSocket(listener.get());
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
        std::optional<FakeSocket*> listener = m_fake_network->getSocket(to_connect->request_endpoint());
        if (listener.has_value()) {
            listener.value()->ask_for_connect(to_connect);
            return true;
        } else {
            return false;
        }
    }
}
