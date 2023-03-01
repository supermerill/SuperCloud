
//#define CATCH_CONFIG_DISABLE

#include <catch_main.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include <filesystem>
#include <functional>
#include <sstream>

#include "network/PhysicalServer.hpp"
#include "synch/SynchTreeMessageManager.hpp"

namespace supercloud::test::inmemory {

    typedef std::shared_ptr<SynchTreeMessageManager> MsgManaPtr;

    FsID newid() {
        return FsElt::createId(rand_u16() % 1 == 0 ? FsType::FILE : FsType::DIRECTORY, rand_u63(), ComputerId(rand_u63()& COMPUTER_ID_MASK));
    }

	SCENARIO("Test SynchTreeMessage") {

        std::shared_ptr<PhysicalServer> serv = PhysicalServer::createForTests();
        ServerConnectionState m_state;
        MsgManaPtr messageManager = SynchTreeMessageManager::create(serv);

        GIVEN("GET_TREE") {
            SynchTreeMessageManager::TreeRequest data_1{ {newid(),newid(),newid()}, rand_u63(), rand_u63(), get_current_time_milis() };
            ByteBuff buff_1 = messageManager->writeTreeRequestMessage(data_1);
            SynchTreeMessageManager::TreeRequest data_2 = messageManager->readTreeRequestMessage(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeTreeRequestMessage(data_2);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());

            REQUIRE(data_1.roots == data_2.roots);
            REQUIRE(data_1.depth == data_2.depth);
            REQUIRE(data_1.last_fetch_time == data_2.last_fetch_time);
            REQUIRE(data_1.last_commit_received == data_2.last_commit_received);
        }


        GIVEN("SEND_TREE") {
            /*
            * 

        struct TreeAnswerElt {
            FsID elt_id;
            uint16_t elt_depth;
            size_t elt_size;
            FsID last_commit_id;
            DateTime last_commit_time;
        };
        struct TreeAnswerEltChange : TreeAnswerElt {
            std::vector<FsID> state;
        };
        struct TreeAnswerEltDeleted : TreeAnswerElt {
            FsID renamed_to; //can be 0 if just deleted
        };
            */
            SynchTreeMessageManager::TreeAnswer data_1{ ComputerId(rand_u63() & COMPUTER_ID_MASK), get_current_time_milis(), {}, {}, {} };
            for (uint16_t i = 0; i < uint16_t(100); i++) {
                if (rand_u8() % 4 == 1) {
                    //(FsID id, uint16_t depth, size_t size, DateTime date, std::string name, CUGA puga, FsID parent, uint32_t group, std::vector<FsID> state)
                    data_1.created.push_back(SynchTreeMessageManager::FsObjectTreeAnswerPtr{ new SynchTreeMessageManager::FsObjectTreeAnswer{
                        newid(), rand_u16(), rand_u63(), get_current_time_milis(), "test", rand_u16(), newid(), uint32_t(rand_u63()), std::vector<FsID>{ newid(), newid() }} });
                    if (rand_u8() % 2 == 1) {
                        data_1.created.back()->setCommit(rand_u63(), rand_u63());
                    }
                }
                if (rand_u8() % 4 == 2) {
                    /*
            FsID elt_id;
            uint16_t elt_depth;
            size_t elt_size;
            FsID last_commit_id;
            DateTime last_commit_time;
            std::vector<FsID> state;*/
                    data_1.modified.push_back(SynchTreeMessageManager::TreeAnswerEltChange{ 
                        newid(), rand_u16(), rand_u63(), rand_u63(), get_current_time_milis(), std::vector<FsID>{ newid() ,newid() ,newid() ,newid() } });
                }
                if (rand_u8() % 4 == 3) {
                    /*
            sID elt_id;
            uint16_t elt_depth;
            size_t elt_size;
            FsID last_commit_id;
            DateTime last_commit_time;
            FsID renamed_to;*/
                    data_1.deleted.push_back(SynchTreeMessageManager::TreeAnswerEltDeleted{ 
                        newid(), rand_u16(), rand_u63(), rand_u63(), get_current_time_milis(), newid() });
                }
            }
            ByteBuff buff_1 = messageManager->writeTreeAnswerMessage(data_1);
            SynchTreeMessageManager::TreeAnswer data_2 = messageManager->readTreeAnswerMessage(buff_1.rewind());
            ByteBuff buff_2 = messageManager->writeTreeAnswerMessage(data_2);

            REQUIRE(data_1.from == data_2.from);
            REQUIRE(data_1.answer_time == data_2.answer_time);
            REQUIRE(data_1.created.size() == data_2.created.size());
            for (int i = 0; i < data_1.created.size(); ++i) {
                REQUIRE(*data_1.created[i] == *data_2.created[i]);
            }
            REQUIRE(data_1.modified == data_2.modified);
            REQUIRE(data_1.deleted == data_2.deleted);

            REQUIRE(buff_1.rewind().getAll() == buff_2.rewind().getAll());
        }

	}
}
