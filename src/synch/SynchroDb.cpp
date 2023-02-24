#include "SynchroDb.hpp"

#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include "fs/base/FsFile.hpp"
#include "fs/base/FsDirectory.hpp"

#include <fstream>

namespace supercloud {


    void SynchroDb::launch() {

    }


    void SynchroDb::update() {

    }


    void SynchroDb::updateFetchTime(ComputerId cid) {
        m_current_states[cid].last_fetch_date = m_network->getCurrentTime();
    }

    bool SynchroDb::mergeCommit(FsEltPtr to_merge) {
        /// When we fetch data, we receive FsElt with only the last commits that should be useful for us. (it shouldn't send data much older than what we have).
        /// The object only have the most basic implementation (ids of commits with ids of content, with dates).
        /// This method use this information to modify our own implementation of the FsStorage to insert the data we diddn't known yet.
        /// Note: we may only get the state of the last commit (for each object) since the last fetch. Each server have an incomplete knowledge of the past commit, as these
        ///  can be destroyed at will to avoid clutter. The important thing is that a commit id is unique and the state of the FsElt for his commit is the same evrywhere.
        
        SynchState& my_state = m_current_states[to_merge->getOwner()];
        FsID last_id = my_state.last_commit;
        if (FsElt::isChunk(to_merge->getId())) {
            //chunk is immutable. its commit is its id.
            if (last_id < to_merge->getId()) {
                //update
                my_state.last_commit = to_merge->getId();
                my_state.last_commit_date = to_merge->getDate();
                my_state.last_commit_received_date = m_network->getCurrentTime();

                //it's not needed to send chunks. the file commit already has their ids.
                assert(false);
            }
        } else if (FsElt::isFile(to_merge->getId())) {
            FsObjectPtr file = FsElt::toObject(to_merge);
            assert(file->getCommitsSize() > 0);
            const FsObject::Commit& commit = file->getCommits(file->getCommitsSize() - 1);
            //update
            my_state.last_commit = commit.id;
            my_state.last_commit_date = commit.date;
            my_state.last_commit_received_date = m_network->getCurrentTime();
            //ask fs to update the file
            return m_file_storage->mergeFileCommit(file);
        } else if (FsElt::isDirectory(to_merge->getId())) {
            FsObjectPtr dir = FsElt::toObject(to_merge);
            assert(dir->getCommitsSize() > 0);
            const FsObject::Commit& commit = dir->getCommits(dir->getCommitsSize() - 1);
            //update
            my_state.last_commit = commit.id;
            my_state.last_commit_date = commit.date;
            my_state.last_commit_received_date = m_network->getCurrentTime();
            //ask fs to update the file
            return m_file_storage->mergeDirectoryCommit(dir);
        }
        return false;
    }
    
    void SynchroDb::save(std::filesystem::path& file) {
        ByteBuff buffer;
        buffer.putSize(this->m_current_states.size());
        for (auto& cid_2_sycstate : m_current_states) {
            buffer.serializeComputerId(cid_2_sycstate.first);
            buffer.putULong(cid_2_sycstate.second.last_commit);
            buffer.putLong(cid_2_sycstate.second.last_commit_date);
            buffer.putLong(cid_2_sycstate.second.last_commit_received_date);
            buffer.putLong(cid_2_sycstate.second.last_fetch_date);
        }
        buffer.flip();
        auto myfile = std::fstream(file, std::ios::out | std::ios::binary);
        myfile.write((char*)buffer.raw_array() + buffer.position(), buffer.limit());
        myfile.close();
    }

    void SynchroDb::load(std::filesystem::path& file) {
        std::ifstream infile(file, std::ios_base::binary);
        std::vector<char> vector_buffer{ std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>() };
        infile.close();
        ByteBuff buffer{ (uint8_t*)vector_buffer.data(), vector_buffer.size() };

        m_current_states.clear();
        const size_t nb_elts = buffer.getSize();
        for (size_t idx = 0; idx < nb_elts; ++idx) {
            ComputerId elt = buffer.deserializeComputerId();
            FsID commit = buffer.getULong();
            DateTime commit_date = buffer.getLong();
            DateTime received_date = buffer.getLong();
            DateTime fetch_date = buffer.getLong();
            m_current_states[elt] = { commit, commit_date, received_date, fetch_date };
        }

    }

}

