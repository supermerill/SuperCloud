#pragma once

#include "networkAdapter.hpp"
#include <boost/asio.hpp>
#include "utils/Utils.hpp"


using boost::asio::ip::tcp;

namespace supercloud {

    class BoostAsioSocket : public Socket {
        std::shared_ptr<boost::asio::io_service> m_io_service;
        tcp::socket m_socket;
        EndPoint m_connect_to;
    public:
        BoostAsioSocket(std::shared_ptr<boost::asio::io_service> io_service, const EndPoint& to) : m_socket(*io_service), m_io_service(io_service), m_connect_to(to){ }
        BoostAsioSocket(const BoostAsioSocket&) = delete; // can't copy
        BoostAsioSocket& operator=(const BoostAsioSocket&) = delete; // can't copy
        virtual ~BoostAsioSocket() {
            log("WARNING, closing SOCKET");
        }
        virtual EndPoint local_endpoint() const override;
        virtual EndPoint remote_endpoint() const override;
        virtual std::string get_local_ip_network() const override;
        virtual std::future<void> connect() override;
        virtual void write(ByteBuff& buffer) override;
        virtual size_t read(ByteBuff& buffer) override;
        virtual bool is_open() const override { return m_socket.is_open(); }
        virtual void cancel() override { m_socket.cancel(); }
        virtual void close() override;

        tcp::socket& get_asio_socket() { return m_socket; }
    };


    class BoostAsioServerSocket : public ServerSocket {

        std::shared_ptr<boost::asio::io_service> m_io_service;
        std::unique_ptr<tcp::acceptor> m_acceptor;

    public:
        BoostAsioServerSocket() : ServerSocket() { 
            if (!m_io_service) {
                m_io_service = std::unique_ptr<boost::asio::io_service>{ new boost::asio::io_service() };
            }
        }
        BoostAsioServerSocket(const BoostAsioServerSocket&) = delete; // can't copy
        BoostAsioServerSocket& operator=(const BoostAsioServerSocket&) = delete; // can't copy
        virtual ~BoostAsioServerSocket() {
            log("ERROR, closing BoostAsioServerSocket");
        }

        virtual void init(uint16_t port) override;
        virtual std::shared_ptr<Socket> listen() override;
        virtual std::shared_ptr<Socket> client_socket(const EndPoint& to) override;
        virtual void close() override;
    };


    class BoostAsioSocketFactory : public SocketFactory {
    public:
        virtual std::shared_ptr<ServerSocket> create() override {
            return std::shared_ptr<ServerSocket>{new BoostAsioServerSocket()};
        }
    };

}
