
#include "BoostAsioNetwork.hpp"
#include "utils/Utils.hpp"

namespace supercloud {

    std::future<void> BoostAsioSocket::connect() {
        tcp::endpoint addr(boost::asio::ip::address::from_string(m_connect_to.address()), m_connect_to.port());
        return m_socket.async_connect(addr, boost::asio::use_future);
    }

    void BoostAsioSocket::write(ByteBuff& buffer) {
        boost::system::error_code error_code;
        boost::asio::write(m_socket, boost::asio::buffer(buffer.raw_array() + buffer.position(), buffer.limit() - buffer.position()), error_code);
        buffer.position(buffer.limit());
        if (error_code) {
            throw write_error{ std::string("Error when writing :") + error_code.value() + " " + error_code.message() };
        }
    }

    size_t BoostAsioSocket::read(ByteBuff& buffer) {
        boost::system::error_code error_code;
        size_t bytesRead = boost::asio::read(m_socket, boost::asio::buffer(buffer.raw_array() + buffer.position(), buffer.limit() - buffer.position()), error_code);
        if (error_code) {
            throw read_error{ std::string("Error when reading :") + error_code.value() + " " + error_code.message() };
        }
        buffer.position(buffer.position() + bytesRead);
        return bytesRead;
    }

    EndPoint BoostAsioSocket::local_endpoint() const {
        return EndPoint{ m_socket.local_endpoint().address().to_string(), m_socket.local_endpoint().port() };
    }
    EndPoint BoostAsioSocket::remote_endpoint() const {
        log(std::string("remote_endpoint for ") + m_socket.is_open());
        try {
            return EndPoint{
                m_socket.remote_endpoint().address().to_string(),
                m_socket.remote_endpoint().port()
            };
        }
        catch (std::exception e) {
            error("Error, this network don't allow remote_endpoint()");
        }
        return { "",0 };
    }

    void BoostAsioSocket::close(){
        m_socket.shutdown(tcp::socket::shutdown_both);
        m_socket.close();
    }

    std::string BoostAsioSocket::get_local_ip_network() const {
        try {
            std::string network = m_socket.remote_endpoint().address().to_string();
            if (std::count(network.begin(), network.end(), '/') == 1 && false) {
                //i may be able to get it.... TODO:
            } else if (m_socket.remote_endpoint().address().is_v4() && std::count(network.begin(), network.end(), '.') == 3) {
                //seems ipv4
                //we guess a /24 network
                network = network.substr(0, network.find_last_of('.'));
                //TODO use this->socket->remote_endpoint().address().to_v4().netmask
            } else if (size_t nb_delim = std::count(network.begin(), network.end(), ':'); nb_delim > 1 && m_socket.remote_endpoint().address().is_v6()) {

                //seems ipv6
                // we remove the last 4 groups
                if (nb_delim == 7) {
                    network = network.substr(0, network.find_last_of(':'));
                    network = network.substr(0, network.find_last_of(':'));
                    network = network.substr(0, network.find_last_of(':'));
                    network = network.substr(0, network.find_last_of(':'));
                } else {
                    //??
                    network = network.substr(0, network.find_last_of(':'));
                }
            } else {
                //other...
            }
            return network;
        }
        catch (std::exception e) {
            error("Error, this network don't allow remote_endpoint()");
        }
        return "";
    }

    void BoostAsioServerSocket::init(uint16_t port) {
        ServerSocket::init(port);
        boost::asio::ip::tcp::endpoint tcp_endpoint(boost::asio::ip::address_v4::any(), port);
        m_acceptor = std::unique_ptr<tcp::acceptor>{ new tcp::acceptor{ *m_io_service, tcp_endpoint } };
    }

    std::shared_ptr<Socket> BoostAsioServerSocket::listen() {
        if (m_acceptor) {
            std::shared_ptr<BoostAsioSocket> new_socket{ new BoostAsioSocket{m_io_service,{}} };
            m_acceptor->accept(new_socket->get_asio_socket());
            return new_socket;
        }
        throw read_error("Error, you can't listen a ServerSocket that isn't init()");
    }

    std::shared_ptr<Socket> BoostAsioServerSocket::client_socket(const EndPoint& to) {
        std::shared_ptr<Socket> client_socket{ new BoostAsioSocket{this->m_io_service, to} };
        return client_socket;
    }

    void BoostAsioServerSocket::close() {
        if(m_acceptor)
            m_acceptor->close();
    }

}
