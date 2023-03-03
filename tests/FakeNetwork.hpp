#pragma once

#include "network/networkAdapter.hpp"
#include <boost/asio.hpp>
#include "utils/Utils.hpp"

#include <deque>
#include <optional>
#include <atomic>

using boost::asio::ip::tcp;

namespace supercloud {

    class FakeRouter;
    class FakeSocket;
    class FakeServerSocket;

    class FakeLocalNetwork {
        //class c network, without the last dot
        std::string network_ip;
        std::vector<std::weak_ptr<FakeSocket>> computers;
        std::vector<std::shared_ptr<FakeRouter>> routers;

    public:
        FakeLocalNetwork(const std::string& net) : network_ip(net){}
        std::string getNetworkIp() { return network_ip; }
        void addRouter(std::shared_ptr<FakeRouter> router);
        void addSocket(std::weak_ptr<FakeSocket> computer);
        std::optional<std::weak_ptr<FakeSocket>> getSocket(const EndPoint& endpoint);
        std::optional<std::weak_ptr<FakeSocket>> getLocalSocket(const EndPoint& endpoint);
    };

    class FakeRouter {
    protected:
        std::vector<std::shared_ptr<FakeLocalNetwork>> networks;
        std::map<std::string, std::shared_ptr<FakeRouter>> iptables;
        FakeRouter() {}
    public:
        static inline FakeRouter& createFakeRouter(std::vector<std::shared_ptr<FakeLocalNetwork>> nets) {
            std::shared_ptr<FakeRouter> ptr = std::shared_ptr<FakeRouter>{ new FakeRouter() };
            ptr->networks = nets;
            for (auto& netptr : nets) {
                netptr->addRouter(ptr);
            }
            return *ptr;
        }
        FakeRouter& addNetwork(std::shared_ptr<FakeLocalNetwork> net);
        FakeRouter& set_route(const std::string& network, std::shared_ptr<FakeRouter> by);
        std::optional<std::weak_ptr<FakeSocket>> getSocket(const EndPoint& endpoint);

    };

    class FakeNatRouter : public FakeRouter {
    protected:
        FakeNatRouter() : FakeRouter() {}
    public:
        static inline void createFakeNatRouter(std::shared_ptr<FakeLocalNetwork> local, std::shared_ptr<FakeLocalNetwork> global) {
            std::shared_ptr<FakeNatRouter> ptr = std::shared_ptr<FakeNatRouter>{ new FakeNatRouter() };
            ptr->addNetwork(global);
            ptr->addNetwork(local);
            local->addRouter(ptr);
        }
    };


    class FakeSocket : public Socket {
        //to create new port for new connections
        static inline std::atomic<uint16_t> next_port{ 1000 };
        //local endpoint
        EndPoint m_listen_from;
        //not the remote endpoint, it's the one used to connect()
        EndPoint m_connect_to;
        // read queue with mutex & semaphore for thread-safety and waiting.
        std::deque<uint8_t> m_fifo;
        //mutex to access to m_fifo, to m_other_side and to modify m_open. May be nullptr if m_open is false.
        std::shared_ptr<std::mutex> m_fifo_mutex;
        std::counting_semaphore m_fifo_available{ 0 };
        //to write into the other queue
        FakeSocket* m_other_side;
        //if you have m_other_side, and so can read & write;
        bool m_open = false;
        //if we use connect(), then it's used to wait for the server socket thread to listen
        std::counting_semaphore wait_for_data{ 0 };
        // our creator, god bless him.
        FakeServerSocket& m_server;
    public:
        FakeSocket(const EndPoint& from, const EndPoint& to, FakeServerSocket& creator) : m_listen_from(from), m_connect_to(to), m_server(creator) { }
        FakeSocket(const FakeSocket&) = delete; // can't copy
        FakeSocket& operator=(const FakeSocket&) = delete; // can't copy
        virtual ~FakeSocket() {
            log("ERROR, closing SOCKET");
            close();
        }
        virtual EndPoint local_endpoint() const override {
            return m_listen_from;
        }
        virtual EndPoint remote_endpoint() const override {
            return EndPoint{ "",0 };
            //throw std::runtime_error("Error, remote_endpoint is problematic");
        }
        EndPoint request_endpoint() const {
            return m_connect_to;
        }
        virtual std::string get_local_ip_network() const override;
        virtual std::future<void> connect() override;
        virtual void write(ByteBuff& buffer) override;
        virtual size_t read(ByteBuff& buffer) override;
        virtual bool is_open() const override { return m_open; }
        virtual void cancel() override { close(); }
        virtual void close() override;

        void ask_for_connect(FakeSocket* other_side);
    };


    class FakeServerSocket : public ServerSocket {

        std::shared_ptr<FakeLocalNetwork> m_fake_network;
        std::shared_ptr<FakeSocket> listener;
        std::shared_ptr<FakeSocket> temp_new_socket;
        std::counting_semaphore wait_for_connect{ 0 };
        std::counting_semaphore wait_for_listen{ 0 };
    public:
        FakeServerSocket(const std::string& ip, std::shared_ptr<FakeLocalNetwork> net) : ServerSocket() ,m_fake_network(net){
            m_endpoint = { ip, m_endpoint.port() };
        }
        FakeServerSocket(const FakeServerSocket&) = delete; // can't copy
        FakeServerSocket& operator=(const FakeServerSocket&) = delete; // can't copy
        virtual ~FakeServerSocket() {
            log("ERROR, closing FakeServerSocket");
        }

        virtual void init(uint16_t port) override;
        virtual std::shared_ptr<Socket> listen() override;
        void ask_listen(std::shared_ptr<FakeSocket> socket_connected);
        virtual std::shared_ptr<Socket> client_socket(const EndPoint& to) override;
        bool connect(FakeSocket* to_connect);
        virtual void close() override {/*TODO?*/}
    };


    class FakeSocketFactory : public SocketFactory {
        std::string m_ip = "";
        std::shared_ptr<FakeLocalNetwork> m_net;
    public:
        void setNextInstanceConfiguration(const std::string& ip, std::shared_ptr<FakeLocalNetwork> net) { m_ip = ip; m_net = net; }
        virtual std::shared_ptr<ServerSocket> create() override {
            auto& ptr = std::shared_ptr<FakeServerSocket>{ new FakeServerSocket(m_ip, m_net) };
            return ptr;
        }
    };

}

