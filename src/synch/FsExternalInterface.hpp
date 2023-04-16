#pragma once

#include "fs/base/FsStorage.hpp"
#include "fs/base/FsElt.hpp"
#include "utils/Utils.hpp"

#include <filesystem>
#include <unordered_map>
#include <map>
#include <optional>

namespace supercloud {

    class SynchroDb;

    // choose to use the network or the internal FS database to answer calls.
    class FsExternalInterface {
    protected:
        std::shared_ptr<SynchroDb> m_db;

        FsExternalInterface() {}
    public:

        static inline std::shared_ptr <FsExternalInterface> create(std::shared_ptr<SynchroDb> synch_service) {
            std::shared_ptr<FsExternalInterface> me = std::shared_ptr<FsExternalInterface>{ new FsExternalInterface() };
            me->m_db = synch_service;
            return me;
        }

        struct RequestAnswer {
            uint16_t error_code; // 0 if no error TODO:enum
            std::string error_message; // empty if no error
            RequestAnswer() : error_code(1), error_message("error") {} // for future/promise
            RequestAnswer(std::string msg, uint16_t code = uint16_t (-1)) : error_code(code), error_message(msg) {}
        };

        struct ObjectRequestAnswer : RequestAnswer {
            bool is_file;
            FsObjectPtr object; //empty if error
            ObjectRequestAnswer() : RequestAnswer("error", 1), is_file(false), object() {} // for future/promise
            ObjectRequestAnswer(bool is_fic, FsObjectPtr obj) : RequestAnswer("", 0), is_file(is_fic), object(obj) {}
            ObjectRequestAnswer(std::string msg, uint16_t code = 0) : RequestAnswer(msg, code), is_file(false), object() {}
        };
        /// <summary>
        /// request a fs object at a path.
        /// </summary>
        /// <param name="path"></param>
        /// <returns></returns>
        std::future<ObjectRequestAnswer> get(const std::filesystem::path& path);
        std::future<ObjectRequestAnswer> get(FsID id);
        std::future<ObjectRequestAnswer> contains(FsDirPtr dir, const std::string& name);

        //used to get the content of a directory
        struct MultiObjectRequestAnswer : RequestAnswer {
            std::vector<FsObjectPtr> objects;
            MultiObjectRequestAnswer() : RequestAnswer("Error", 1), objects() {} // for future/promise
            MultiObjectRequestAnswer(std::vector<FsObjectPtr> objs) : RequestAnswer("", 0), objects(objs) {}
            MultiObjectRequestAnswer(std::string msg, uint16_t code = 0) : RequestAnswer(msg, code), objects() {}
        };
        std::future<MultiObjectRequestAnswer> getAll(std::vector<std::filesystem::path> paths);
        std::future<MultiObjectRequestAnswer> getAll(std::vector<FsID> ids);

        /// <summary>
        /// request data from a file oebjct
        /// </summary>
        /// <param name="path"></param>
        /// <returns></returns>
        std::future<RequestAnswer> getData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size);
        std::future<RequestAnswer> writeData(FsFilePtr file, uint8_t* buffer, size_t offset, size_t size);
        std::future<ObjectRequestAnswer> resize(FsFilePtr file, size_t new_size);

        /// <summary>
        /// no future yet? (it's not needed?) Maybe it's more future-proof to use them anyway...
        /// </summary>
        /// <param name="obj"></param>
        /// <returns></returns>
        int createFile(FsDirPtr parent_dir, const std::string& name, CUGA rights);
        int createDirectory(FsDirPtr parent_dir, const std::string& name, CUGA rights);
        int removeObject(FsObjectPtr obj);
        int moveObject(FsObjectPtr obj, const std::filesystem::path& new_path);
        int setCUGA(FsObjectPtr obj, CUGA rights);

        ComputerId getOurComputerId();
        uint32_t getUserId(ComputerId cid);

    protected:
        void get(const std::filesystem::path& path, std::shared_ptr<std::promise<ObjectRequestAnswer>> promise_ptr, size_t max_retries = 1);
        void get(FsID id, std::shared_ptr<std::promise<ObjectRequestAnswer>> promise_ptr);
        void contains(FsDirPtr dir, const std::string& name, std::shared_ptr<std::promise<ObjectRequestAnswer>> promise_ptr);
        void getAll(std::vector<std::filesystem::path> paths, std::shared_ptr<std::promise<MultiObjectRequestAnswer>> promise_ptr);
        void getAll(std::vector<FsID> ids, std::shared_ptr<std::promise<MultiObjectRequestAnswer>> promise_ptr);
        void getData(const FsFilePtr file, uint8_t* buffer, const size_t offset, const size_t size, std::shared_ptr<std::promise<RequestAnswer>> promise_ptr, size_t max_retries = 1, std::map<FsID, FsChunkPtr> cache = {});
        void writeData(FsFilePtr file, const uint8_t* buffer, const size_t offset, const size_t size, std::shared_ptr<std::promise<RequestAnswer>> promise_ptr, size_t max_retries = 1, std::map<FsID, FsChunkPtr> cache = {});
        void resize(FsFilePtr file, size_t new_size, std::shared_ptr<std::promise<ObjectRequestAnswer>> promise_ptr, size_t max_retries = 1, std::map<FsID, FsChunkPtr> cache = {});

    };

}