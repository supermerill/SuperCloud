
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

	void addChunkToFile(FsStorage& fs, FsFilePtr file, const std::string& str) {
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
		virtual DateTime getCurrrentTime() { return get_current_time_milis(); }
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
		FsDirectoryInMemory new_dir{ FsElt::createId(FsType::DIRECTORY, 2, 88), clock->getCurrrentTime(), "new dir", CUGA_7777, dir1->getId() };
		new_dir.replaceContent({ new_file_id }, FsObjectCommit{ new_file_id , clock->getCurrrentTime() , {{0, new_file_id}} });
		FsDirectoryInMemory new_root_dir{ FsElt::createId(FsType::DIRECTORY, 3, 88), clock->getCurrrentTime(), "newrootdir", CUGA_7777, FsID(FsType::DIRECTORY) };
		FsFileInMemory new_file{ new_file_id, clock->getCurrrentTime(), "new file", CUGA_7777, new_dir.getId() };
		FsFileInMemory new_root_file{ FsElt::createId(FsType::FILE, 4, 88), clock->getCurrrentTime(), "newrootfile", CUGA_7777, FsID(FsType::DIRECTORY) };
		FsDirectoryInMemory new_root_state{ root->getId(),  root->getDate(), root->getName(), root->getCUGA(), root->getParent() };
		{
			new_root_state.replaceContent(root->getCurrent(), root->getCommit(0));
			new_root_state.replaceContent(root->getCurrent(), root->getCommit(1));
			std::vector<FsID> current = root->getCurrent();
			current.push_back(new_root_dir.getId());
			current.push_back(new_root_file.getId());
			FsObjectCommit commit{ FsElt::createId(FsType::FILE, 5, 88), new_root_file.getDate(), {{0,new_root_dir.getId()},{0,new_root_file.getId()}} };
			new_root_state.replaceContent(current, commit);
		}
		FsDirectoryInMemory new_dir1_state{ dir1->getId(),  dir1->getDate(), dir1->getName(), dir1->getCUGA(), dir1->getParent() };
		{
			new_dir1_state.replaceContent(dir1->getCurrent(), dir1->getCommit(0));
			std::vector<FsID> current = dir1->getCurrent();
			current.push_back(new_dir.getId());
			FsObjectCommit commit{ FsElt::createId(FsType::FILE, 5, 88), new_root_file.getDate(), {{0,new_dir.getId()}} };
			new_dir1_state.replaceContent(current, commit);
		}
		std::unordered_map<FsID, const FsElt*> commits;
		commits[new_dir.getId()] = &new_dir;
		commits[new_root_dir.getId()] = &new_root_dir;
		commits[new_file.getId()] = &new_file;
		commits[new_root_file.getId()] = &new_root_file;
		commits[new_root_state.getId()] = &new_root_state;
		commits[new_dir1_state.getId()] = &new_dir1_state;
		bool res = fs.mergeObjectCommit(new_dir, commits);
		REQUIRE(res);
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().size() == 1);
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().front() == new_file_id);
		REQUIRE(fs.loadDirectory(dir1->getId())->getCurrent().size() == 1);
		REQUIRE(fs.loadFile(new_file_id));
		res = fs.mergeObjectCommit(new_root_dir, commits);
		REQUIRE(res);
		REQUIRE(fs.loadDirectory(fs.getRoot())->getCurrent().size() == 2);
		res = fs.mergeObjectCommit(new_file, commits);
		REQUIRE(res);
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().size() == 1);
		REQUIRE(fs.loadFile(new_file_id));
		res = fs.mergeObjectCommit(new_root_file, commits);
		REQUIRE(res);
		REQUIRE(fs.loadDirectory(fs.getRoot())->getCurrent().size() == 2);

		res = fs.mergeObjectCommit(new_root_state, commits);
		res = fs.mergeObjectCommit(new_dir1_state, commits);
		REQUIRE(fs.loadDirectory(fs.getRoot())->getCurrent().size() == 4);
		REQUIRE(fs.loadDirectory(dir1->getId())->getCurrent().size() == 2);

		REQUIRE(fs.checkFilesystem());

		REQUIRE(contains(fs.loadDirectory(fs.getRoot())->getCurrent(), new_root_dir.getId()));
		REQUIRE(fs.loadDirectory(FsElt::createId(FsType::DIRECTORY, 2, 88))->getCurrent().size() == 1);
		REQUIRE(contains(fs.loadDirectory(fs.getRoot())->getCurrent(), new_root_file.getId()));

	}
	SCENARIO("Test FsStorageInMemory merge commits (other order)") {
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
		FsDirectoryInMemory new_dir{ FsElt::createId(FsType::DIRECTORY, 2, 88), clock->getCurrrentTime(), "new dir", CUGA_7777, dir1->getId() };
		new_dir.replaceContent({ new_file_id }, FsObjectCommit{ new_file_id , clock->getCurrrentTime() , {{0, new_file_id}} });
		FsDirectoryInMemory new_root_dir{ FsElt::createId(FsType::DIRECTORY, 3, 88), clock->getCurrrentTime(), "newrootdir", CUGA_7777, FsID(FsType::DIRECTORY) };
		FsFileInMemory new_file{ new_file_id, clock->getCurrrentTime(), "new file", CUGA_7777, new_dir.getId() };
		FsFileInMemory new_root_file{ FsElt::createId(FsType::FILE, 4, 88), clock->getCurrrentTime(), "newrootfile", CUGA_7777, FsID(FsType::DIRECTORY) };
		FsDirectoryInMemory new_root_state{ root->getId(),  root->getDate(), root->getName(), root->getCUGA(), root->getParent() };
		{
			new_root_state.replaceContent(root->getCurrent(), root->getCommit(0));
			new_root_state.replaceContent(root->getCurrent(), root->getCommit(1));
			std::vector<FsID> current = root->getCurrent();
			current.push_back(new_root_dir.getId());
			current.push_back(new_root_file.getId());
			FsObjectCommit commit{ FsElt::createId(FsType::FILE, 5, 88), new_root_file.getDate(), {{0,new_root_dir.getId()},{0,new_root_file.getId()}} };
			new_root_state.replaceContent(current, commit);
		}
		FsDirectoryInMemory new_dir1_state{ dir1->getId(),  dir1->getDate(), dir1->getName(), dir1->getCUGA(), dir1->getParent() };
		{
			new_dir1_state.replaceContent(dir1->getCurrent(), dir1->getCommit(0));
			std::vector<FsID> current = dir1->getCurrent();
			current.push_back(new_dir.getId());
			FsObjectCommit commit{ FsElt::createId(FsType::FILE, 5, 88), new_root_file.getDate(), {{0,new_dir.getId()}} };
			new_dir1_state.replaceContent(current, commit);
		}
		std::unordered_map<FsID, const FsElt*> commits;
		commits[new_dir.getId()] = &new_dir;
		commits[new_root_dir.getId()] = &new_root_dir;
		commits[new_file.getId()] = &new_file;
		commits[new_root_file.getId()] = &new_root_file;
		commits[new_root_state.getId()] = &new_root_state;
		commits[new_dir1_state.getId()] = &new_dir1_state;
		bool res = true;
		res &= fs.mergeObjectCommit(new_root_state, commits);
		res &= fs.mergeObjectCommit(new_dir1_state, commits);
		res &= fs.mergeObjectCommit(new_root_dir, commits);
		res &= fs.mergeObjectCommit(new_root_file, commits);
		res &= fs.mergeObjectCommit(new_dir, commits);
		res &= fs.mergeObjectCommit(new_file, commits);
		REQUIRE(res);
		REQUIRE(fs.checkFilesystem());
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().size() == 1);
		REQUIRE(fs.loadDirectory(new_dir.getId())->getCurrent().front() == new_file_id);
		REQUIRE(fs.loadFile(new_file_id));
		REQUIRE(fs.loadDirectory(fs.getRoot())->getCurrent().size() == 4);
		REQUIRE(fs.loadDirectory(dir1->getId())->getCurrent().size() == 2);


		REQUIRE(contains(fs.loadDirectory(fs.getRoot())->getCurrent(), new_root_dir.getId()));
		REQUIRE(fs.loadDirectory(FsElt::createId(FsType::DIRECTORY, 2, 88))->getCurrent().size() == 1);
		REQUIRE(contains(fs.loadDirectory(fs.getRoot())->getCurrent(), new_root_file.getId()));

	}
}
