#include "Utils.hpp"

#include "network/ClusterManager.hpp";

#include <chrono>
#include <mutex>
#include <sstream>

namespace std {

	void counting_semaphore::release() {
		std::lock_guard<decltype(mutex_)> lock(mutex_);
		++count_;
		condition_.notify_one();
	}

	void counting_semaphore::release(size_t number) {
		std::lock_guard<decltype(mutex_)> lock(mutex_);
		count_ += number;
		condition_.notify_all();
	}

	void counting_semaphore::acquire() {
		std::unique_lock<decltype(mutex_)> lock(mutex_);
		while (!count_) // Handle spurious wake-ups.
			condition_.wait(lock);
		--count_;
	}

	void counting_semaphore::acquire(size_t number) {
		std::unique_lock<decltype(mutex_)> lock(mutex_);
		while (number > 0) {
			while (!count_) // Handle spurious wake-ups.
				condition_.wait(lock);
			size_t removed_count = std::min(number, count_);
			number -= removed_count;
			count_ -= removed_count;
		}
	}

	bool counting_semaphore::try_acquire() {
		std::lock_guard<decltype(mutex_)> lock(mutex_);
		if (count_) {
			--count_;
			return true;
		}
		return false;
	}

	void counting_semaphore::drain() {
		std::lock_guard<decltype(mutex_)> lock(mutex_);
		count_ = 0;
	}
}

namespace supercloud{


	int64_t get_current_time_milis() {
		std::chrono::time_point time = std::chrono::system_clock::now(); // get the current time
		std::chrono::duration since_epoch = time.time_since_epoch(); // get the duration since epoch
		return std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
	}

	std::string id2str[256];
	void init_id2str(){
		id2str[*UnnencryptedMessageType::NO_MESSAGE] = "NO_MESSAGE";
		id2str[*UnnencryptedMessageType::GET_SERVER_ID] = "GET_SERVER_ID";
		id2str[*UnnencryptedMessageType::SEND_SERVER_ID] = "SEND_SERVER_ID";
		id2str[*UnnencryptedMessageType::GET_SERVER_LIST] = "GET_SERVER_LIST";
		id2str[*UnnencryptedMessageType::SEND_SERVER_LIST] = "SEND_SERVER_LIST";
		id2str[*UnnencryptedMessageType::GET_SERVER_PUBLIC_KEY] = "GET_SERVER_PUBLIC_KEY";
		id2str[*UnnencryptedMessageType::SEND_SERVER_PUBLIC_KEY] = "SEND_SERVER_PUBLIC_KEY";
		id2str[*UnnencryptedMessageType::GET_VERIFY_IDENTITY] = "GET_VERIFY_IDENTITY";
		id2str[*UnnencryptedMessageType::SEND_VERIFY_IDENTITY] = "SEND_VERIFY_IDENTITY";
		id2str[*UnnencryptedMessageType::GET_SERVER_AES_KEY] = "GET_SERVER_AES_KEY";
		id2str[*UnnencryptedMessageType::SEND_SERVER_AES_KEY] = "SEND_SERVER_AES_KEY";
		id2str[*UnnencryptedMessageType::GET_CONNECTION_ESTABLISHED] = "GET_CONNECTION_ESTABLISHED";
		id2str[*UnnencryptedMessageType::SEND_CONNECTION_ESTABLISHED] = "SEND_CONNECTION_ESTABLISHED";
		id2str[*UnnencryptedMessageType::REVOKE_COMPUTER_ID] = "REVOKE_COMPUTER_ID";
		id2str[*UnnencryptedMessageType::PRIORITY_CLEAR] = "PRIORITY_CLEAR";
		id2str[*UnnencryptedMessageType::NEW_CONNECTION] = "NEW_CONNECTION";
		id2str[*UnnencryptedMessageType::CONNECTION_CLOSED] = "CONNECTION_CLOSED"; 
		id2str[*UnnencryptedMessageType::TIMER_SECOND] = "TIMER_SECOND";
		id2str[*UnnencryptedMessageType::TIMER_MINUTE] = "TIMER_MINUTE";
		id2str[*UnnencryptedMessageType::FIRST_ENCODED_MESSAGE] = "FIRST_ENCODED_MESSAGE";
		id2str[*UnnencryptedMessageType::GET_SERVER_DATABASE] = "GET_SERVER_DATABASE";
		id2str[*UnnencryptedMessageType::SEND_SERVER_DATABASE] = "SEND_SERVER_DATABASE";
	}
	std::string messageId_to_string(uint8_t type) {
		if (id2str[*UnnencryptedMessageType::GET_SERVER_ID].empty()) {
			init_id2str();
		}
		return id2str[type];
	}


	std::string u8_hex(uint8_t data)
	{
		std::stringstream stream;
		stream << std::hex << std::setfill('0') << std::setw(2) << uint16_t(data);
		return stream.str();
	}
	std::string to_hex(std::vector<uint8_t> vec)
	{
		std::stringstream stream;
		stream << std::hex << std::setfill('0');
		for (uint8_t val : vec) {
			stream << std::setw(2) << uint16_t(val);
		}
		return stream.str();
	}
	std::vector<uint8_t> from_hex(std::string serialized)
	{
		size_t len = serialized.length();
		std::vector<uint8_t> out;
		for (size_t i = 0; i < len; i += 2) {
			std::istringstream strm(serialized.substr(i, 2));
			uint16_t x;
			strm >> std::hex >> x;
			out.push_back(uint8_t(x));
		}
		return out;
	}


	std::recursive_mutex stdout_mutex;
	void error(std::string str) { 
		std::lock_guard lock(stdout_mutex);  
		std::cerr << str; 
		if(str[str.size()-1] != '\n') std::cerr << "\n"; 
	}
	void msg(std::string str) { 
		std::lock_guard lock(stdout_mutex); 
		std::cout << str; 
		if (str[str.size() - 1] != '\n') std::cout << "\n"; 
	}
#if _DEBUG
	void log(std::string str) {
		std::lock_guard lock(stdout_mutex);
		std::cout << str;
		if (str[str.size() - 1] != '\n') std::cout << "\n";
	}
#else
	void log(std::string str) {}
#endif

	std::recursive_mutex* loglock() {
		return &stdout_mutex;
	}


	// RANDOM=============================

		// https://stackoverflow.com/questions/5008804/generating-a-random-integer-from-a-range
	std::random_device rd;     // Only used once to initialise (seed) engine
	std::mt19937 rng(rd());    // Random-number engine used (Mersenne-Twister in this case)
	std::uniform_int_distribution<uint64_t> uni_u63(0, uint64_t(std::numeric_limits<int64_t>::max())); // Guaranteed unbiased
	std::uniform_int_distribution<uint16_t> uni_u16(0, std::numeric_limits<uint16_t>::max()); // Guaranteed unbiased
	std::uniform_int_distribution<uint16_t> uni_u8(0, std::numeric_limits<uint8_t>::max()); // Guaranteed unbiased
	uint64_t rand_u63() {
		return uni_u63(rng);
	}
	uint16_t rand_u16() {
		return uni_u16(rng);
	}
	uint8_t rand_u8() {
		return uint8_t(uni_u8(rng));
	}

	std::vector<std::string> split(const std::string& input, char delim) {
		std::vector<std::string> result;
		std::stringstream ss(input);
		std::string item;

		while (std::getline(ss, item, delim)) {
			result.push_back(item);
		}
		return result;
	}
	std::string concatenate(const std::vector<std::string>& input, char delim) {
		std::stringstream ss;
		auto it = input.begin();
		if (it != input.end()) {
			ss << *it;
			++it;
			for (; it != input.end(); ++it) {
				ss << delim << *it;
			}
		}
		return ss.str();
	}


	///END RANDOM=========================

	//inline void  compareDirect(const std::string& fileName, int min, int max) {

	//	//File fic1 = new File("C:/Users/Admin/Videos/" + fileName);
	//	//File fic2 = new File("Q:/" + fileName);

	//	//FileChannel in1 = new FileInputStream(fic1).getChannel();
	//	//FileChannel in2 = new FileInputStream(fic2).getChannel();

	//	//ByteBuffer buff1 = ByteBuffer.allocate(max - min);
	//	//ByteBuffer buff2 = ByteBuffer.allocate(max - min);

	//	//int taille1 = in1.read(buff1);
	//	//int taille2 = in2.read(buff2);
	//	//if (taille1 != taille2) {
	//	//	System.out.println("not same length : " + taille1 + " != " + taille2);
	//	//}
	//	//int lastError = -2;
	//	//int idx = 0;
	//	//while (buff1.position() < buff1.limit()) {
	//	//	byte by1 = buff1.get();
	//	//	byte by2 = buff2.get();
	//	//	if (by1 != by2 || (by1 == 0 && lastError == idx - 1)) {
	//	//		if (lastError != idx - 1) {
	//	//			System.out.println("Error at byte " + idx + " : " + by1 + " != " + by2);
	//	//		}
	//	//		lastError = idx;
	//	//	} else if (lastError == idx - 1) {
	//	//		System.out.println("No more Error at byte " + idx + " : " + by1 + " == " + by2);
	//	//	}
	//	//	idx++;
	//	//}

	//	////			for(int i=0;i<100;i++){
	//	////			b1 = in1.read();
	//	////			b2 = in2.read();
	//	////			idx++;
	//	////			System.out.println("Next byte : "+idx+" : "+b1+" != "+b2);
	//	////			}
	//	////		}

	//	//in1.close();
	//	//in2.close();

	//}

	//inline void  compare(const std::string& fileName, int min, int max) {

	//	//File fic1 = new File("C:/Users/Admin/Videos/" + fileName);
	//	//File fic2 = new File("Q:/" + fileName);

	//	//BufferedInputStream in1 = new BufferedInputStream(new FileInputStream(fic1));
	//	//BufferedInputStream in2 = new BufferedInputStream(new FileInputStream(fic2));

	//	//int idx = 0;
	//	//int b1 = in1.read();
	//	//int b2 = in2.read();
	//	//int lastError = -2;
	//	//while (b1 >= 0) {
	//	//	b1 = in1.read();
	//	//	b2 = in2.read();
	//	//	if (b1 != b2 || (b1 == 0 && lastError == idx - 1)) {
	//	//		if (lastError != idx - 1) {
	//	//			System.out.println("Error at byte " + idx + " : " + b1 + " != " + b2);
	//	//		}
	//	//		lastError = idx;
	//	//	} else if (lastError == idx - 1) {
	//	//		System.out.println("No more Error at byte " + idx + " : " + b1 + " == " + b2);
	//	//	}
	//	//	idx++;
	//	//}

	//	////			for(int i=0;i<100;i++){
	//	////			b1 = in1.read();
	//	////			b2 = in2.read();
	//	////			idx++;
	//	////			System.out.println("Next byte : "+idx+" : "+b1+" != "+b2);
	//	////			}
	//	////		}

	//	//in1.close();
	//	//in2.close();

	//}


} // namespace supercloud
