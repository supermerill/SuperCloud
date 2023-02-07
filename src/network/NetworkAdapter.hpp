#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <tuple>

#include "utils/ByteBuff.hpp"

// interface to boost asio or test network.
namespace supercloud {

    class write_error : public std::runtime_error
    {
        using _Mybase = std::runtime_error;
    public:
        explicit write_error(const std::string& _Message) : _Mybase(_Message) {}
        explicit write_error(const char* _Message) : _Mybase(_Message) {}
    };

    class read_error : public std::runtime_error
    {
        using _Mybase = std::runtime_error;
    public:
        explicit read_error(const std::string& _Message) : _Mybase(_Message) {}
        explicit read_error(const char* _Message) : _Mybase(_Message) {}
    };

    class EndPoint {
        std::string m_address;
        uint16_t m_port;
    public:
        EndPoint() : m_address(""), m_port(0) {}
        EndPoint(std::string addr, uint16_t port) : m_address(addr), m_port(port) {}
        EndPoint(const EndPoint&) = default;
        EndPoint& operator=(const EndPoint&) = default;
        std::string address() {
            return m_address;
        }
        uint16_t port() {
            return m_port;
        };
    };

    class Socket {
    protected:
        Socket() {};
    public:
        Socket(const Socket&) = delete; // can't copy
        Socket& operator=(const Socket&) = delete; // can't copy
        virtual EndPoint local_endpoint() const = 0;
        virtual EndPoint remote_endpoint() const = 0;
        virtual std::future<void> connect() = 0;
        virtual void write(ByteBuff& buffer) = 0;
        virtual size_t read(ByteBuff& buffer) = 0;
        virtual bool is_open() const = 0;
        virtual void cancel() = 0;
        virtual void close() = 0;
        virtual std::string get_local_ip_network() const = 0;

        static inline std::string get_ip_network(std::string ip) {
            if (ip.empty()) return "";
            std::string network = ip;
            if (std::count(network.begin(), network.end(), '/') == 1 && false) {
                //i may be able to get it.... TODO:
            } else if (std::count(network.begin(), network.end(), '.') == 3) {
                //seems ipv4
                //we guess a /24 network
                network = network.substr(0, network.find_last_of('.'));
                //TODO use this->socket->remote_endpoint().address().to_v4().netmask
            } else if (size_t nb_delim = std::count(network.begin(), network.end(), ':'); nb_delim > 1) {

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
    };
    class SocketFactory;

    class ServerSocket {
    public:
        static inline std::unique_ptr<SocketFactory> factory;
    protected:
        EndPoint m_endpoint{ "", 0 };
        ServerSocket() {}
    public:
        ServerSocket(const ServerSocket&) = delete; // can't copy
        ServerSocket& operator=(const ServerSocket&) = delete; // can't copy
        virtual void init(uint16_t port) { m_endpoint = EndPoint{ "", port }; };
        virtual EndPoint endpoint() { return m_endpoint; }
        virtual std::shared_ptr<Socket> listen() = 0;
        virtual std::shared_ptr<Socket> client_socket(const EndPoint& to) = 0;
        virtual void close() = 0;
    };

    class SocketFactory {
    public:
        virtual std::shared_ptr<ServerSocket> create() = 0;
    };

}