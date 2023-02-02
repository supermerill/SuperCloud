#pragma once

#include <string>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

#include <boost/lexical_cast.hpp>

namespace supercloud {
	class Parameters {

	protected:
		std::map<std::string, std::string> data;
		std::filesystem::path filepath;
		static inline const char* TRIM_STR = " \t\n\r\f\v";

	public:
		Parameters(const std::filesystem::path& path) : filepath(path){
			reload();
		}

		void reload() {
			//try {
				std::ifstream in(getFileOrCreate());
				data.clear();
				std::string line;
				while (std::getline(in, line)) {
					//trim start
					line.erase(0, line.find_first_not_of(TRIM_STR));
					//trim end
					line.erase(line.find_last_not_of(TRIM_STR) + 1);
					size_t indexofSpace = line.find(' ');
					if (line.length() > 2 && indexofSpace > 0 && line[0] != '#' && indexofSpace < line.length()) {
						std::string header = line.substr(0, indexofSpace);
						std::string value = line.substr(1 + indexofSpace);
						data[header] = value;
					}
				}
			//}
			//catch (std::exception e) {
			//
			//}
		}

		std::filesystem::path getFileOrCreate() {
			const bool exists = std::filesystem::exists(this->filepath);
			const bool is_directory = std::filesystem::is_directory(this->filepath);
			if (exists && is_directory) {
				this->filepath = this->filepath.parent_path() / (std::string("fic_") + this->filepath.filename().string());
				return getFileOrCreate();
			}
			if (!exists) {
				//fic.createNewFile();
				//don't need to create, it's done by the stream
			}
			return this->filepath;
		}

		void save() {
			//try {
				std::ofstream out(getFileOrCreate());
				for (const auto& entry : data) {
					out<<entry.first<<' '<< entry.second<<'\n';
				}
			//} catch (IOException e) { throw new RuntimeException(e); }
		}

		bool has(const std::string& key) {
			return (data.find(key) != data.end());
		}

		const std::string& get(const std::string& key, const std::string& default = "") {
			auto& it = data.find(key);
			if (it == data.end()) {
				return default;
			}
			return it->second;
		}

		bool getBool(const std::string& key, const bool default = false) {
			auto& it = data.find(key);
			if (it == data.end()) {
				return default;
			}
			return it->second == "true";
		}

		int32_t getInt(const std::string& key, const int32_t default = 0) {
			auto& it = data.find(key);
			if (it == data.end()) {
				return default;
			}
			try {
				return boost::lexical_cast<int32_t>(it->second); // std::stoi is compiler-dependant (as it's based on 'int' and not int32_t)
			}
			catch (std::exception e) {
				return default;
			}
		}

		int64_t getLong(const std::string& key, const int64_t default = 0) {
			auto& it = data.find(key);
			if (it == data.end()) {
				return default;
			}
			try {
				return boost::lexical_cast<int64_t>(it->second); // std::stol is compiler-dependant (as it's based on 'long' and not int64_t)
			}
			catch (std::exception e) {
				return default;
			}
		}
		double getDouble(const std::string& key, const double default = 0) {
			auto& it = data.find(key);
			if (it == data.end()) {
				return default;
			}
			try {
				return std::stod(it->second);
			}
			catch (std::exception e) {
				return default;
			}
		}

		std::string set(const std::string& key, std::string def) {
			std::replace(def.begin(), def.end(), '\n', ' ');
			data[key] = def;
			save();
			return def;
		}

		std::string setString(const std::string& key, std::string def) {
			return set(key, def);
		}

		int64_t setLong(const std::string& key, int64_t def) {
			data[key] = std::to_string(def);
			save();
			return def;
		}

		bool setBool(const std::string& key, bool def) {
			data[key] = std::to_string(def);
			save();
			return def;
		}

		int32_t setInt(const std::string& key, int32_t def) {
			data[key] = std::to_string(def);
			save();
			return def;
		}
	};

}//namespace supercloud