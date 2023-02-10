
//#define CATCH_CONFIG_DISABLE

#include <catch_main.hpp>
#include "utils/ByteBuff.hpp"
#include "utils/Utils.hpp"
#include <filesystem>
#include <functional>

#include "network/PhysicalServer.hpp"

namespace supercloud::test {

	SCENARIO("Test hex serialization") {

		for (int num = 0; num < 1000; num++) {
			std::vector<uint8_t> vec;
			for (size_t nb = 0; nb < (num>0?rand_u8():0); nb++) {
				vec.push_back(rand_u8());
			}
			std::string serialized = to_hex(vec);
			std::vector<uint8_t> deserialized = from_hex(serialized);
			std::string reserialized = to_hex(deserialized);
			REQUIRE(serialized == reserialized);
			REQUIRE(vec == deserialized);
		}


	}

    SCENARIO("ByteBuff TrailInt") {
		GIVEN(("TrailInt 0")) {
			ByteBuff buff;
			buff.putTrailInt(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == 0);
			}
		}
		GIVEN(("TrailInt 1")) {
			ByteBuff buff;
			buff.putTrailInt(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == 1);
			}
		}
		GIVEN(("TrailInt -1")) {
			ByteBuff buff;
			buff.putTrailInt(-1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == -1);
			}
		}
		GIVEN(("TrailInt 63")) {
			ByteBuff buff;
			buff.putTrailInt(63).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == 63);
			}
		}
		GIVEN(("TrailInt -64")) {
			ByteBuff buff;
			buff.putTrailInt(-64).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getTrailInt() == -64);
			}
		}
		GIVEN(("TrailInt 2bits")) {
			int nb_bits = 2;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = (1 << (7 * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
		GIVEN(("TrailInt 3bits")) {
			int nb_bits = 3;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = (1 << (7 * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
		GIVEN(("TrailInt 4bits")) {
			int nb_bits = 4;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = (1 << (7 * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
		GIVEN(("TrailInt 5bits")) {
			int nb_bits = 5;
			int32_t val = (1 << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check positive start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val - 1;
			THEN("check negative start at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = 0x7FFFFFFF;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = -val;
			THEN("check negative quasi-end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
			val = val - 1;
			THEN("check negative end at " + std::to_string(val)) {
				buff.putTrailInt(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getTrailInt() == val);
			}
		}
    }

	SCENARIO("ByteBuff Byte") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().put(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().put(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 1);
			}
		}
		GIVEN(("0xFF")) {
			buff.reset().put(0xFF).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 0xFF);
			}
		}
		GIVEN(("0x7F")) {
			buff.reset().put(0x7F).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.get() == 0x7F);
			}
		}
	}

	SCENARIO("ByteBuff Int32") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putInt(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putInt(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == 1);
			}
		}
		GIVEN(("-1")) {
			buff.reset().putInt(-1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == -1);
			}
		}
		GIVEN(("0x7FFFFFFF = " + std::to_string(int32_t(0x7FFFFFFF)))) {
			buff.reset().putInt(0x7FFFFFFF).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 4);
				REQUIRE(buff.getInt() == 0x7FFFFFFF);
			}
		}
	}

	SCENARIO("ByteBuff UInt32") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putUInt(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getUInt() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putUInt(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getUInt() == 1);
			}
		}
		GIVEN(("-1 = " + std::to_string(uint32_t(-1)))) {
			buff.reset().putUInt(uint32_t(-1)).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getUInt() == uint32_t(-1));
			}
		}
		GIVEN(("max = " + std::to_string(std::numeric_limits<uint32_t>::max()))) {
			buff.reset().putUInt(std::numeric_limits<uint32_t>::max()).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getUInt() == std::numeric_limits<uint32_t>::max());
			}
		}
	}

	SCENARIO("ByteBuff Int64") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putLong(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putLong(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == 1);
			}
		}
		GIVEN(("-1")) {
			buff.reset().putLong(-1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == -1);
			}
		}
		GIVEN(("max = " + std::to_string(std::numeric_limits<int64_t>::max()))) {
			buff.reset().putLong(std::numeric_limits<int64_t>::max()).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getLong() == std::numeric_limits<int64_t>::max());
			}
		}
	}

	SCENARIO("ByteBuff UInt64") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putULong(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putULong(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == 1);
			}
		}
		GIVEN(("-1 = " + std::to_string(uint64_t(-1)))) {
			buff.reset().putULong(uint64_t(-1)).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == uint64_t(-1));
			}
		}
		GIVEN(("max = " + std::to_string(std::numeric_limits<uint64_t>::max()))) {
			buff.reset().putULong(std::numeric_limits<uint64_t>::max()).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 8);
				REQUIRE(buff.getULong() == std::numeric_limits<uint64_t>::max());
			}
		}
	}

	SCENARIO("ByteBuff Trail UInt64 (size)") {
		ByteBuff buff;
		GIVEN(("0")) {
			buff.reset().putSize(0).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getSize() == 0);
			}
		}
		GIVEN(("1")) {
			buff.reset().putSize(1).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getSize() == 1);
			}
		}
		GIVEN(("TrailULong 4bits")) {
			int nb_bits = 4;
			size_t val = (size_t(1) << (7 * (nb_bits - 1)));
			ByteBuff buff;
			THEN("check start at " + std::to_string(val)) {
				buff.putSize(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getSize() == val);
			}
			val = (1 << (size_t(7) * nb_bits - 1)) - 1;
			THEN("check positive end at " + std::to_string(val)) {
				buff.putSize(val).flip();
				REQUIRE(buff.limit() == nb_bits);
				REQUIRE(buff.getSize() == val);
			}
		}
		GIVEN(std::string("TrailULong just over 4bits")) {
			size_t val = (size_t(1) << (7 * 4));
			THEN("check positive start at " + std::to_string(val)) {
				buff.putSize(val).flip();
				REQUIRE(buff.limit() == 9);
				REQUIRE(buff.getSize() == val);
			}
		}
		GIVEN(("-1 = " + std::to_string(size_t(-1)))) {
			buff.reset().putSize(size_t(-1)).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 9);
				REQUIRE(buff.getSize() == size_t(-1));
			}
		}
		GIVEN(("max = " + std::to_string(std::numeric_limits<size_t>::max()))) {
			buff.reset().putSize(std::numeric_limits<size_t>::max()).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 9);
				REQUIRE(buff.getSize() == std::numeric_limits<size_t>::max());
			}
		}
	}

	SCENARIO("ByteBuff String") {
		ByteBuff buff;
		GIVEN((" empty string")) {
			buff.reset().putUTF8("").flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 1);
				REQUIRE(buff.getUTF8() == "");
			}
		}
		GIVEN((" '1' ")) {
			buff.reset().putUTF8("1").flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 2);
				REQUIRE(buff.getUTF8() == "1");
			}
		}
		GIVEN((" 'a bigger string' ")) {
			buff.reset().putUTF8("a bigger string").flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == std::string("a bigger string").length() + 1);
				REQUIRE(buff.getUTF8() == "a bigger string");
			}
		}
		GIVEN(("a very long string (20k cars)")) {
			std::stringstream ss;
			for (int i = 0; i < 20000; i++) {
				ss << char('a' + char(i % 20));
			}
			std::string str = ss.str();
			buff.reset().putUTF8(str).flip();
			THEN("check size and value") {
				REQUIRE(buff.limit() == 3+ 20000);
				REQUIRE(buff.getUTF8() == str);
			}
		}
		//TODO utf chars
	}
}
