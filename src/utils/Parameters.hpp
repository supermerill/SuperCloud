#pragma once

#include "Utils.hpp"

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
	public:
		virtual void load() = 0; // deserialize (ensure it)
		virtual void save() = 0; // serialize (ensure it)

		virtual bool has(const std::string& key) = 0;

		virtual const std::string& get(const std::string& key, const std::string& default = "") = 0;
		virtual bool getBool(const std::string & key, const bool default = false) = 0;
		virtual int32_t getInt(const std::string & key, const int32_t default = 0) = 0;
		virtual int64_t getLong(const std::string & key, const int64_t default = 0) = 0;
		virtual double getDouble(const std::string & key, const double default = 0) = 0;

		virtual void set(const std::string & key, const std::string& val) = 0;
		virtual void setString(const std::string & key, const std::string& val) = 0;
		virtual void setLong(const std::string & key, int64_t val) = 0;
		virtual void setBool(const std::string & key, bool val) = 0;
		virtual void setInt(const std::string & key, int32_t val) = 0;
	};

	class InMemoryParameters : public Parameters {

	protected:
		std::map<std::string, std::string> m_data;

	public:
		InMemoryParameters(){}
		InMemoryParameters(const InMemoryParameters&) = default;
		InMemoryParameters& operator=(const InMemoryParameters&) = default;

		//no serialization
		void load() override {}
		void save() override {}

		bool has(const std::string& key) override {
			return (m_data.find(key) != m_data.end());
		}

		const std::string& get(const std::string& key, const std::string& default = "") override {
			auto& it = m_data.find(key);
			if (it == m_data.end()) {
				return default;
			}
			return it->second;
		}

		bool getBool(const std::string& key, const bool default = false) override {
			auto& it = m_data.find(key);
			if (it == m_data.end()) {
				return default;
			}
			return it->second == "true";
		}

		int32_t getInt(const std::string& key, const int32_t default = 0) override {
			auto& it = m_data.find(key);
			if (it == m_data.end()) {
				return default;
			}
			try {
				return boost::lexical_cast<int32_t>(it->second); // std::stoi is compiler-dependant (as it's based on 'int' and not int32_t)
			}
			catch (std::exception e) {
				return default;
			}
		}

		int64_t getLong(const std::string& key, const int64_t default = 0) override {
			auto& it = m_data.find(key);
			if (it == m_data.end()) {
				return default;
			}
			try {
				return boost::lexical_cast<int64_t>(it->second); // std::stol is compiler-dependant (as it's based on 'long' and not int64_t)
			}
			catch (std::exception e) {
				return default;
			}
		}
		double getDouble(const std::string& key, const double default = 0) override {
			auto& it = m_data.find(key);
			if (it == m_data.end()) {
				return default;
			}
			try {
				return std::stod(it->second);
			}
			catch (std::exception e) {
				return default;
			}
		}

		void set(const std::string& key, const std::string& def) override {
			m_data[key] = def;
			std::string& to_modify = m_data[key];
			std::replace(to_modify.begin(), to_modify.end(), '\n', ' ');
		}

		void setString(const std::string& key, const std::string& def) override {
			set(key, def);
		}

		void setLong(const std::string& key, int64_t def) override {
			m_data[key] = std::to_string(def);
		}

		void setBool(const std::string& key, bool def) override {
			m_data[key] = std::to_string(def);
		}

		void setInt(const std::string& key, int32_t def) override {
			m_data[key] = std::to_string(def);
		}
	};

	class ConfigFileParameters : public InMemoryParameters {

	protected:
		std::filesystem::path m_filepath;
		static inline const char* TRIM_STR = " \t\n\r\f\v";

	public:
		ConfigFileParameters(const std::filesystem::path& path) : m_filepath(path) {
			load();
		}
		ConfigFileParameters(const ConfigFileParameters&) = default;
		ConfigFileParameters& operator=(const ConfigFileParameters&) = default;

		void load() override {
			try {
				std::filesystem::path read_path = getFileOrCreate();
				//don't load if no file
				if (!std::filesystem::exists(read_path)) return;
				//load
				std::ifstream in(read_path);
				m_data.clear();
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
						m_data[header] = value;
					}
				}
			}
			catch (std::exception e) {
			
			}
		}

		std::filesystem::path getFileOrCreate() {
			const bool exists = std::filesystem::exists(this->m_filepath);
			const bool is_directory = std::filesystem::is_directory(this->m_filepath);
			if (exists && is_directory) {
				this->m_filepath = this->m_filepath.parent_path() / (std::string("fic_") + this->m_filepath.filename().string());
				return getFileOrCreate();
			}
			if (!exists) {
				//check if there is a bak to use
				std::filesystem::path ficBak = m_filepath;
				ficBak.replace_filename(ficBak.filename().string() + ".bak");
				if (std::filesystem::exists(this->m_filepath) && !std::filesystem::is_directory(this->m_filepath)) {
					return ficBak;
				}
				//don't need to create, it's done by the stream
				//fic.createNewFile();
			}
			return this->m_filepath;
		}

		void save() override {

			// get synch
			std::filesystem::path fic(m_filepath);
			std::filesystem::path ficBak = fic;
			ficBak.replace_filename(ficBak.filename().string() + ".bak");
			std::filesystem::path ficTemp(m_filepath);
			ficTemp.replace_filename(ficTemp.filename().string() + std::string("_1"));
			// create a bak
			bool exists_bak = std::filesystem::exists(ficBak);
			bool exists = std::filesystem::exists(fic);
			if (exists_bak && exists) std::filesystem::remove(ficBak);
			if (exists) std::filesystem::rename(fic, ficBak);
			// create an other file
			try {
				std::ofstream out(ficTemp);
				for (const auto& entry : m_data) {
					out << entry.first << ' ' << entry.second << '\n';
				}
				// swap temp & fic file (rename erase fic)
				std::filesystem::rename(ficTemp, fic);
			}
			catch (std::exception e) { error(std::string("Error while serializing parameters: ") + e.what()); }
		}
	};

}//namespace supercloud