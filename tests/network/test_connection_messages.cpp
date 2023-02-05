
//#define CATCH_CONFIG_DISABLE

//#include <catch_main.hpp> // main is in test_connection
#include <catch2/catch.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Parameters.hpp"
#include <filesystem>
#include <functional>

#include "network/PhysicalServer.hpp"
#include "network/ConnectionMessageManager.hpp"

namespace supercloud::test {

    typedef std::shared_ptr<ConnectionMessageManager> PtrMsgMana;


    SCENARIO("testing that the messages are correctly encoded & decoded") {
        std::shared_ptr<PhysicalServer> serv = PhysicalServer::createForTests();
        ServerConnectionState m_state;
        PtrMsgMana messageManager = ConnectionMessageManager::create(serv, m_state);

        GIVEN("SEND_SERVER_ID") {
            ConnectionMessageManager::Data_SEND_SERVER_ID data_1{ rand_u63(), rand_u63(), 2315 };
            ByteBuff buff_1 = messageManager->create_SEND_SERVER_ID_msg(data_1);
            ConnectionMessageManager::Data_SEND_SERVER_ID data_2 = messageManager->get_SEND_SERVER_ID_msg(buff_1);
            ByteBuff buff_2 = messageManager->create_SEND_SERVER_ID_msg(data_2);

            REQUIRE(buff_1.flip().getAll() == buff_2.getAll());
        }


        GIVEN("SEND_SERVER_LIST") {
            ConnectionMessageManager::Data_SEND_SERVER_LIST data_1;
            for (uint16_t i = 0; i < uint16_t(100); i++) {
                if (rand_u8() % 4 == 1) {
                    data_1.registered_computer_id.insert(i);
                }
                if (rand_u8() % 3 == 1) {
                    data_1.registered_peer_id.insert(i);
                }
                if (rand_u8() % 13 == 1) {
                    data_1.connected_computer_id.insert(i);
                }
                if (rand_u8() % 11 == 1) {
                    data_1.connected_peer_id.insert(i);
                }
            }
            ByteBuff buff_1 = messageManager->create_SEND_SERVER_LIST_msg(data_1);
            ConnectionMessageManager::Data_SEND_SERVER_LIST data_2 = messageManager->get_SEND_SERVER_LIST_msg(buff_1);

            REQUIRE(data_1.registered_computer_id == data_2.registered_computer_id);
            REQUIRE(data_1.registered_peer_id == data_2.registered_peer_id);
            REQUIRE(data_1.connected_computer_id == data_2.connected_computer_id);
            REQUIRE(data_1.connected_peer_id == data_2.connected_peer_id);
        }

	}
}
