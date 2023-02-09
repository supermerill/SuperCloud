#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>

namespace supercloud {

	class ByteBuff
	{
	protected:
		//private static final Charset UTF8 = Charset.forName("UTF-8");
		size_t m_position = 0;
		size_t m_limit = 0;
		std::unique_ptr<uint8_t[]> m_buffer;
		size_t m_length = 0;

		void readcheck(const size_t size)
		{
			if (m_limit < m_position + size)
			{
				throw std::runtime_error("Error, ByteBuff.limit() = " + std::to_string(m_limit) + " < " + std::to_string(m_position + size));
			}
		}

		void doubleSize();

	public:

		/**
		 * Init an short buffer
		 */
		ByteBuff() : m_buffer(new uint8_t[16]), m_length(16), m_limit(0) {}

		/**
		 * Init a buffer with a initial size and limit to maximum.
		 *
		 * @param initialCapacity
		 *            size of the first backing array.
		 */
		ByteBuff(const size_t initialCapacity) : m_buffer(new uint8_t[initialCapacity]), m_length(initialCapacity), m_limit(initialCapacity) {}

		/**
		 * Copy the buffIn buffer into this buffer.
		 * After that, pos is at 0 and limit at buffIn.length.
		 *
		 * @param buffIn
		 *            buffer for intitialization
		 */
		ByteBuff(const uint8_t* buffIn, const size_t length) : m_buffer(new uint8_t[length]), m_length(length), m_limit(length)
		{
			this->put(buffIn, length);
			this->rewind();
		}

		/**
		 * Copy the buffIn buffer, or just link it if unsafe is true (if we increase the size, a new byte[] is created, not
		 * linked with the ByteBuffer).
		 *
		 * @param buffIn
		 *            buffe or intiialization
		 * @param unsafe
		 *            true for sharing an array, for a moment. False to copy.
		 */
		ByteBuff(const ByteBuff& buffIn) : m_buffer(new uint8_t[buffIn.m_length]), m_length(buffIn.m_length), m_limit(buffIn.m_limit), m_position(buffIn.m_position) {
			std::copy(buffIn.m_buffer.get(), buffIn.m_buffer.get() + buffIn.m_length, this->m_buffer.get());
		}
		ByteBuff(ByteBuff&&) = default;
		ByteBuff& operator=(const ByteBuff& buffIn) {
			reset();
			expand(buffIn.m_limit - buffIn.m_position);
			std::copy(buffIn.m_buffer.get() + buffIn.m_position, buffIn.m_buffer.get() + buffIn.m_limit, this->m_buffer.get());
			//System.arraycopy(src.m_buffer, src.m_position, this->m_buffer, this->m_position, size);
			this->m_position = buffIn.m_position;
			this->m_limit = buffIn.m_limit;
			return *this;
		}
		ByteBuff& operator=(ByteBuff&&) = default;
		virtual ~ByteBuff() {}


		/**
		 * Set limit to at least position() + size.
		 * Shouldn't be called/used directly. But sometimes i try to re-create this so here it is, available.
		 * @param size min size available.
		 */
		void expand(const size_t size);


		size_t position() const { return m_position; }
		ByteBuff& position(const size_t newPosition) { this->m_position = newPosition; return *this; }

		/// <summary>
		/// Available bytes for get/read.
		/// </summary>
		/// <returns>limit() - position()</returns>
		size_t available() const { return m_limit-m_position; }

		/**
		 * Get the limit, the limit is only used for create().
		 *
		 * @return the limit
		 */
		size_t limit() const { return m_limit; }

		/**
		 * Set the limit, the limit is only used for create().
		 *
		 * @return *this
		 */
		ByteBuff& limit(const size_t newLImit);

		/**
		 * Set the limit to the actual position and set the position to 0. This method is useful to call a create() after.
		 *
		 * @return *this
		 */
		ByteBuff& flip();

		ByteBuff& rewind();

		uint8_t get();
		ByteBuff& put(const uint8_t b);

		std::vector<uint8_t> get(const size_t nbElt);
		std::vector<uint8_t> getAll();
		ByteBuff& put(std::vector<uint8_t>);

		/**
		 * Put data into dest (from position).
		 * @param dest destination array
		 * @param destPos start idx in dest
		 * @param length nb uint8_ts to copy
		 * @return *this
		 */
		ByteBuff& get(uint8_t* dest, const size_t destPos, const size_t length);
		ByteBuff& put(const uint8_t* src, size_t length);

		/**
		 * Copy the scr.limit-src.position uint8_ts from src.position to src.limit into this.position.
		 * @param src soruce array
		 * @return *this.
		 */
		ByteBuff& put(ByteBuff& src);
		ByteBuff& put(ByteBuff& src, size_t size);
		ByteBuff& put(const uint8_t* src, const size_t srcPos, const size_t length);

		/**
		 * Getter to the raw array sed to store data between 0 and limit.
		 * Dangerous! do not modify the ByteBuff as long as you're using it!!
		 * @return The raw array.
		 */
		uint8_t* raw_array();

		//wchar getWChar();
		//ByteBuff& putWChar(const wchar value);

		int16_t getShort();
		ByteBuff& putShort(const int16_t value);

		uint16_t getUShort();
		ByteBuff& putUShort(const uint16_t value);

		ByteBuff& putTrailInt(const int32_t num);
		int32_t getTrailInt();

		int32_t getInt();
		ByteBuff& putInt(const int32_t value);

		int64_t getLong();
		ByteBuff& putLong(const int64_t value);

		uint64_t getULong() { return uint64_t(getLong()); }
		ByteBuff& putULong(const uint64_t value) { return putLong(int64_t(value)); }

		ByteBuff& putSize(const uint64_t num);
		uint64_t getSize();

		float getFloat();
		ByteBuff& putFloat(const float value);

		double getDouble();
		ByteBuff& putDouble(const double value);

		ByteBuff& putUTF8(const std::string str);
		std::string getUTF8();

		ByteBuff& putShortUTF8(const std::string str);
		std::string getShortUTF8();

		/**
		 * Create a uint8_tBuff with other position and limit but the same backing buffer. The new values will be <= current
		 * limit. All changes make to the backing buffer are reported.
		 *
		 * @param start
		 *            buffer start position
		 * @param length
		 *            Set the new limit to position + length
		 * @return A ByteBuff with same buffer but another position and limit.
		 */
		ByteBuff& subBuff(const size_t start, const size_t length);

		/**
		 * Clear it. pos and limit are now at 0.
		 * @return
		 */
		ByteBuff& reset();


		/**
		 * copy content in an array of limit()-position() size.
		 * @return uint8_t[]
		 */
		 //uint8_t* toArray();

		 /**
		  * Read nbBytes from stream an put it into this.
		  * @param in
		  * @param nbBytes
		  * @return *this
		  * @throws IOException
		  */
		ByteBuff& read(std::istream& in, size_t nbBytes);

		/**
		 * Read limit()-position() uint8_ts from stream an put it into this.
		 * @param in
		 * @return *this
		 * @throws IOException
		 */
		ByteBuff& read(std::istream& in);

		/**
		 * write nbBytes from this to outputBuffer
		 * @param out
		 * @param nbBytes
		 * @return
		 * @throws IOException
		 */
		ByteBuff& write(std::ostream& out, size_t nbBytes);

		/**
		 * @throws IOException
		 */
		ByteBuff& write(std::ostream& out);
	};

} // namespace supercloud
