#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <random>
#include <stdexcept>
#include <vector>

//#define SLOW_NETWORK_FOR_DEBUG 1

namespace supercloud{
	//FIXME constexpr
	inline const uint16_t NO_COMPUTER_ID = uint16_t(-1);
	inline const uint64_t NO_CLUSTER_ID = uint64_t(-1);
	inline const uint64_t NO_PEER_ID = uint64_t(-1);

	typedef std::string InetAdress;
	typedef std::pair<InetAdress, uint16_t> InetSocketAddress;

	template<class NUMERIC>
	std::string to_hex_str(NUMERIC data)
	{
		std::stringstream stream;
		stream << std::hex << data;
		return stream.str();
	}

	uint64_t rand_u63();
	uint16_t rand_u16();
	uint8_t rand_u8();

	//inline void  compareDirect(const std::string& fileName, int min, int max);

	//inline void  compare(const std::string& fileName, int min, int max);

	int64_t get_current_time_milis();

	std::string messageId_to_string(uint8_t msgType);

	//template<typename NUMERIC>
	//std::string& operator+(std::string& lhs, NUMERIC rhs) {
	//	return lhs = lhs + std::to_string(rhs);
	//}

	inline std::string& operator+(std::string& lhs, bool rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, char rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, uint8_t rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, uint16_t rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, int32_t rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, uint64_t rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, int64_t rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, float rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string& operator+(std::string& lhs, double rhs) {
		return lhs = lhs + std::to_string(rhs);
	}
	inline std::string operator+(std::string&& lhs, uint64_t rhs) {
		return lhs + std::to_string(rhs);
	}

	//template<class NUMERIC>
	//std::string operator+(std::string_view lhs, NUMERIC rhs) {
	//	return lhs + std::to_string(rhs);
	//}
	//template<class NUMERIC>
	//std::string operator+(const char* lhs, NUMERIC rhs) {
	//	return lhs + std::to_string(rhs);
	//}

	template<class NUMERIC>
	int compare(NUMERIC x, NUMERIC y) {
		return (x < y) ? -1 : ((x == y) ? 0 : 1);
	}

	template<class PTR>
	bool contains(const std::vector<PTR>& list, const PTR& test) {
		for (const PTR& obj : list)
			if (obj == test)
				return true;
		return false;
	}
	template<class TYPE>
	bool contains_reference(const std::vector<TYPE>& list, const TYPE& test) {
		for (const TYPE& obj : list)
			if (&obj == &test)
				return true;
		return false;
	}

	namespace custom {
		/// Little wrapper class over a vector to easier iteration on it when a erase is needed.
		// it can go both direction with ++/next() and --/previous()
		// usage stl-like:
		//   for(better_it it = ++better_it{my_vec}; it.has_next(); ++it){
		//     if(it->bad()) it->erase();
		//   }
		// usage java-like:
		//   better_it it(my_vec);
		//   while(it.has_next()){
		//     my_type& obj = it.next();
		//     if(obj.bad()) it.erase();
		//   }
		// usage with foreach macro:
		//   foreach(it, vec){
		//     if(it->bad()) it->erase();
		//   }
		template<class T>
		class it {
		public:
			std::vector<T>* vec;
			int32_t pos;
			it(std::vector<T>* v, int32_t p) : vec(v), pos(p) {}
		public:
			it(std::vector<T>& v) : vec(&v), pos(-1) {}

			/// check if the current position of this iterator point to a valid value.
			bool valid() { return pos >= 0 && pos < vec->size(); }
			/// get the current value of the iterator, don't call it if valid() return false.
			T& operator*() { return (*vec)[pos]; }
			T* operator->() { return &(*vec)[pos]; }
			/// get the current value of the iterator with bounds check.
			T& get() { return vec->at(pos); }
			/// get the current std::iterator position. It's before begin() at creation, and only get the begin() value after the first next().
			typename std::vector<T>::iterator position() { return vec->begin() + pos; }
			typename std::vector<T>::iterator begin() { return vec->begin(); }
			typename std::vector<T>::iterator end() { return vec->end(); }
			/// return true if there is a value after the current one.
			bool has_next() { return (pos + 1) < int32_t(vec->size()); }
			bool has_previous() { return (pos - 1) >= 0; }
			// go to the next position, and return the value. Don't call it if has_next() return false
			T& next() { assert(has_next()); return (*vec)[++pos]; }
			T& previous() { assert(has_next()); return (*vec)[--pos]; }
			// go to the next position, and return the value with bounds checks.
			T& go_next() { return vec->at(++pos); }
			T& go_previous() { return vec->at(--pos); }
			//go before begin(), ready to call next() to go to the first element
			void go_start() { pos = (-1); }
			//go to end(), ready to call previous() to go to the last element
			void go_end() { pos = vec.size(); }
			//prefix/suffix operators. Allow to go to the end() position or before the begin() one.
			it& operator++() { ++pos; return *this; }
			it operator++(int) { ++pos; return it{ vec, pos - 1 }; }
			it& operator--() { --pos; return *this; }
			it operator--(int) { --pos; return it{ vec, pos + 1 }; }
			// erase the value at the curent position. Then go to the previous position.
			void erase() { if (pos >= 0 && pos < vec->size()) { vec->erase(vec->begin() + pos); pos = std::max(-1, pos - 1); } }

		};

#define foreach(itname,vecname) for(custom::it itname = ++custom::it{vecname}; itname.has_next(); ++itname)
	}

	void error(std::string str);
	void msg(std::string str);
	void log(std::string str);

	class operation_not_implemented : public std::runtime_error
	{
		using _Mybase = std::runtime_error;
	public:
		explicit operation_not_implemented(const std::string& _Message) : _Mybase(_Message) {}
		explicit operation_not_implemented(const char* _Message) : _Mybase(_Message) {}
	};

} // namespace supercloud
