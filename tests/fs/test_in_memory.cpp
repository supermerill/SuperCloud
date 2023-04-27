
//#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include <filesystem>
#include <functional>
#include <sstream>

#include "fs/inmemory/FsStorageInMemory.hpp"
#include "fs/inmemory/FsDirInMemory.hpp"
#include "fs/inmemory/FsFileInMemory.hpp"
#include "fs/inmemory/FsChunkInMemory.hpp"

namespace supercloud::test::inmemory {
	ByteBuff StringBuff(const std::string& str){
		return ByteBuff{ (uint8_t*)str.c_str(), str.size() };
	}

	void addChunkToFile(FsStorage& fs, FsFilePtr& file, const std::string& str) {
		fs.addChunkToFile(file, (uint8_t*)&str[0], str.size());
	}

	std::string toString(ByteBuff&& buff) {
		std::string str;
		for (int i = 0; i < buff.limit(); i++) {
			str.push_back(*((char*)(buff.raw_array() + i)));
		}
		return str;
	}
	class MyClock : public Clock {
	public:
		virtual DateTime getCurrentTime() override { return this->get_current_time_milis(); }
	};

	SCENARIO("Test FsStorageInMemory") {
		std::shared_ptr<MyClock> clock = std::make_shared<MyClock>();

		FsStorageInMemory fs{ 42, clock };

		FsDirPtr root = fs.createNewRoot();
		FsDirPtr dir1 = fs.createNewDirectory(root, "dir1");
		FsDirPtr dir11 = fs.createNewDirectory(dir1, "dir11");
		FsFilePtr fic11_1 = fs.createNewFile(dir11, "fic11_1.txt");
		FsFilePtr fi_readme = fs.createNewFile(root, "README.txt");

		addChunkToFile(fs, fi_readme, ("Premier test de système de fichiers"));
		addChunkToFile(fs, fic11_1, ("bla bla"));
		addChunkToFile(fs, fic11_1, ("\n deuxième ligne"));


		std::filesystem::path tmp_path{ std::filesystem::temp_directory_path() /= std::tmpnam(nullptr) };
		fs.serialize(tmp_path);

		FsStorageInMemory fs2{ 0, clock };
		fs2.deserialize(tmp_path);

		REQUIRE(fs.checkFilesystem());
		REQUIRE(fs2.checkFilesystem());



		FsDirPtr root_bis = FsElt::toDirectory(fs2.load(fs2.getRoot()));
		REQUIRE(root_bis->getName() == root->getName());
		REQUIRE(root_bis->getName() == "");
		REQUIRE(root_bis->getCurrent() == root->getCurrent());
		REQUIRE(root_bis->getCurrent().size() == 2);
		REQUIRE(fs2.getDirs(root_bis).size() == 1);
		REQUIRE(fs2.getFiles(root_bis).size() == 1);
		FsDirPtr dir1_bis = FsElt::toDirectory(fs2.getDirs(root_bis)[0]);
		REQUIRE(fs2.getDirs(dir1_bis).size() == 1);
		REQUIRE(fs2.getFiles(dir1_bis).size() == 0);
		REQUIRE(dir1_bis->getName() == dir1->getName());
		REQUIRE(dir1_bis->getName() == "dir1");
		REQUIRE(dir1_bis->getCurrent() == dir1->getCurrent());
		FsDirPtr dir11_bis = FsElt::toDirectory(fs2.getDirs(dir1_bis)[0]);
		REQUIRE(fs2.getDirs(dir11_bis).size() == fs.getDirs(dir11).size());
		REQUIRE(fs2.getDirs(dir11_bis).size() == 0);
		REQUIRE(fs2.getFiles(dir11_bis).size() == 1);
		REQUIRE(dir11_bis->getName() == dir11->getName());
		REQUIRE(dir11_bis->getName() == "dir11");
		REQUIRE(dir11_bis->getCurrent() == dir11->getCurrent());
		FsFilePtr fic11_1_bis = FsElt::toFile(fs2.getFiles(dir11_bis)[0]);
		REQUIRE(fic11_1_bis->getName() == fic11_1->getName());
		REQUIRE(fic11_1_bis->getName() == "fic11_1.txt");
		REQUIRE(fic11_1_bis->getCurrent() == fic11_1->getCurrent());
		REQUIRE(fic11_1_bis->getCurrent().size() == 2);
		REQUIRE(fic11_1_bis->getCurrent() == fic11_1->getCurrent());
		std::string strchunk = toString(FsChunkInMemory::readAll(*FsElt::toChunk(fs2.load(fic11_1_bis->getCurrent()[0]))));
		REQUIRE(strchunk == std::string("bla bla"));

		REQUIRE(FsElt::getComputerId(root->getId()) == 0);
		REQUIRE(FsElt::getComputerId(root_bis->getId()) == 0);
		REQUIRE(FsElt::getComputerId(dir1->getId()) == 42);
		REQUIRE(FsElt::getComputerId(dir1_bis->getId()) == 42);
		REQUIRE(FsElt::getComputerId(dir1->getCommits().back().id) == 42);
		REQUIRE(FsElt::getComputerId(dir1_bis->getCommits().back().id) == 42);
		REQUIRE(FsElt::getComputerId(dir11->getId()) == 42);
		REQUIRE(FsElt::getComputerId(dir11_bis->getId()) == 42);
		REQUIRE(FsElt::getComputerId(dir11->getCommits().back().id) == 42);
		REQUIRE(FsElt::getComputerId(dir11_bis->getCommits().back().id) == 42);
		REQUIRE(FsElt::getComputerId(fic11_1->getId()) == 42);
		REQUIRE(FsElt::getComputerId(fic11_1_bis->getId()) == 42);
		REQUIRE(FsElt::getComputerId(fic11_1->getCommits().back().id) == 42);
		REQUIRE(FsElt::getComputerId(fic11_1_bis->getCommits().back().id) == 42);

		std::filesystem::remove(tmp_path);
	}


	SCENARIO("Test FsStorageInMemory merge commits") {
		std::shared_ptr<MyClock> clock = std::make_shared<MyClock>();

		FsStorageInMemory fs{ 42, clock };

		FsDirPtr root = fs.createNewRoot();
		FsDirPtr dir1 = fs.createNewDirectory(root, "dir1");
		FsDirPtr dir11 = fs.createNewDirectory(dir1, "dir11");
		FsFilePtr fic11_1 = fs.createNewFile(dir11, "fic11_1.txt");
		FsFilePtr fi_readme = fs.createNewFile(root, "README.txt");

		addChunkToFile(fs, fi_readme, ("Premier test de système de fichiers"));
		addChunkToFile(fs, fic11_1, ("bla bla"));
		addChunkToFile(fs, fic11_1, ("\n deuxième ligne"));
		REQUIRE(fs.checkFilesystem());

		//fake commits from cid 88
		FsID new_file_id = FsElt::createId(FsType::FILE, 1, 88);
		FsFileInMemory new_root_file{ FsElt::createId(FsType::FILE, 4, 88), clock->getCurrentTime(), "newrootfile", CUGA_7777, 0, root->getId(), uint16_t(1), 0, {},{},0,0,{} };
		FsDirectoryInMemory new_root_dir{ FsElt::createId(FsType::DIRECTORY, 3, 88), clock->getCurrentTime(), "newrootdir", CUGA_7777,  0/*groupid*/, root->getId(),uint16_t(1), 0,
			{},{},0,0,0,0 };
		FsDirectoryInMemory new_dir{ FsElt::createId(FsType::DIRECTORY, 2, 88), clock->getCurrentTime(), "new dir", CUGA_7777, 0/*groupid*/, dir1->getId()/*parent*/, uint16_t(new_root_dir.getDepth()+1)/*depth*/, 0/*renamedfrom*/,
			{ new_file_id } , {FsObjectCommit{ new_file_id , clock->getCurrentTime() , {{0, new_file_id}} }}, 0, 0, 0, 0 };
		FsFileInMemory new_file{ new_file_id, clock->getCurrentTime(), "new file", CUGA_7777, 0, new_dir.getId(), uint16_t(new_dir.getDepth()+1), 0, {},{},0,0,{} };
		//FsDirectoryInMemory new_root_state{ root->getId(),  root->getCreationTime(), root->getName(), root->getCUGA(), 0, root->getParent(), 0, 0, { };
		FsDirectoryInMemoryFactory new_root_factory{ root.get() };
		assert(new_root_factory.current_state.size() == 2);
		new_root_factory.replaceContent(
			{ new_root_factory.current_state[0], new_root_factory.current_state[1], new_root_dir.getId() , new_root_file.getId() },
			FsObjectCommit{ FsElt::createId(FsType::FILE, 5, 88), new_root_file.getCreationTime(), {{0,new_root_dir.getId()},{0,new_root_file.getId()}} }
			);
		FsDirectoryInMemoryPtr new_root_state = new_root_factory.create(root.get());
		
		//FsDirectoryInMemory new_dir1_state{ dir1->getId(),  dir1->getCreationTime(), dir1->getName(), dir1->getCUGA(), dir1->getParent() };
		FsDirectoryInMemoryFactory new_dir1_factory{ dir1.get() };
		assert(new_dir1_factory.current_state.size() == 1);
		new_dir1_factory.replaceContent(
			{ new_dir1_factory.current_state[0], new_dir.getId() },
			FsObjectCommit{ FsElt::createId(FsType::FILE, 5, 88), new_root_file.getCreationTime(), {{0,new_dir.getId()}} }
		);
		FsDirectoryInMemoryPtr new_dir1_state = new_dir1_factory.create(dir1.get());
		
		std::vector<const FsObject*> commits = {
		&new_dir, &new_root_dir, &new_file, &new_root_file, new_root_state.get(), new_dir1_state.get() };
		fs.mergeObjectsCommit(commits);
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().size() == 1);
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().front() == new_file_id);
		REQUIRE(fs.loadFile(new_file_id));
		REQUIRE(fs.loadDirectory(fs.getRoot())->getCurrent().size() == 4);
		REQUIRE(fs.loadDirectory(dir1->getId())->getCurrent().size() == 2);

		REQUIRE(fs.checkFilesystem());

		REQUIRE(contains(fs.loadDirectory(fs.getRoot())->getCurrent(), new_root_dir.getId()));
		REQUIRE(fs.loadDirectory(FsElt::createId(FsType::DIRECTORY, 2, 88))->getCurrent().size() == 1);
		REQUIRE(contains(fs.loadDirectory(fs.getRoot())->getCurrent(), new_root_file.getId()));

	}
}
