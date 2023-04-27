#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>

namespace supercloud {

	/// <summary>
	/// a unit8_t vector, but with easier way to read/write into/from it.
	/// 
	/// note: const mean it can't be written, but you can still change the current position.
	/// The right way should be to create an iterator(or a view) to avoid sharing that mutable position, but it's working as-is for now. Just don't share one between threads.
	/// </summary>
	class ByteBuff
	{
	protected:
		//private static final Charset UTF8 = Charset.forName("UTF-8");
		mutable size_t m_position = 0;
		size_t m_limit = 0;
		std::unique_ptr<uint8_t[]> m_buffer;
		size_t m_length = 0;

		void readcheck(const size_t size) const
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


		bool operator==(const ByteBuff& other) const {
			bool ok = m_position == other.m_position && m_limit == other.m_limit;
			for (int i = 0; i < m_limit && ok; i++) {
				ok = m_buffer[i] == other.m_buffer[i];
			}
			return ok;
		}

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
		const ByteBuff& rewind() const;

		uint8_t get() const;
		ByteBuff& put(const uint8_t b);

		std::vector<uint8_t> get(const size_t nbElt) const;
		/// <summary>
		/// Get all data from the current position to the end of the buffer.
		/// The new position of the iterator is now at the end.
		/// </summary>
		/// <returns>a vector with limit()-position() bytes</returns>
		std::vector<uint8_t> getAll() const;
		ByteBuff& put(const std::vector<uint8_t>&);
#ifdef _DEBUG
		std::vector<uint8_t> view() const;
#endif

		/**
		 * Put data into dest (from position).
		 * @param dest destination array
		 * @param destPos start idx in dest
		 * @param length nb uint8_ts to copy
		 * @return *this
		 */
		ByteBuff& put(const uint8_t* src, size_t length);
		ByteBuff& put(const uint8_t* src, const size_t srcPos, const size_t length);
		const ByteBuff& get(uint8_t* dest, const size_t destPos, const size_t length) const;

		/**
		 * Copy the scr.limit-src.position uint8_ts from src.position to src.limit into this.position.
		 * @param src soruce array
		 * @return *this.
		 */
		ByteBuff& put(ByteBuff& src);
		ByteBuff& put(ByteBuff& src, size_t size);
		const ByteBuff& get(ByteBuff& dest, size_t size) const;

		/**
		 * Getter to the raw array sed to store data between 0 and limit.
		 * Dangerous! do not modify the ByteBuff as long as you're using it!!
		 * @return The raw array.
		 */
		uint8_t* raw_array();
		const uint8_t* raw_array() const;

		//wchar getWChar();
		//ByteBuff& putWChar(const wchar value);

		int16_t getShort() const;
		ByteBuff& putShort(const int16_t value);

		uint16_t getUShort() const;
		ByteBuff& putUShort(const uint16_t value);

		int32_t getTrailInt() const;
		ByteBuff& putTrailInt(const int32_t num);

		int32_t getInt() const;
		ByteBuff& putInt(const int32_t value);

		uint32_t getUInt() const { return uint32_t(getInt()); }
		ByteBuff& putUInt(const uint32_t value) { return putInt(int32_t(value)); }

		int64_t getLong() const;
		ByteBuff& putLong(const int64_t value);

		uint64_t getULong() const { return uint64_t(getLong()); }
		ByteBuff& putULong(const uint64_t value) { return putLong(int64_t(value)); }

		size_t getSize() const;
		ByteBuff& putSize(const size_t num);

		float getFloat() const;
		ByteBuff& putFloat(const float value);

		double getDouble() const;
		ByteBuff& putDouble(const double value);

		std::string getUTF8() const;
		ByteBuff& putUTF8(const std::string str);

		std::string getShortUTF8() const;
		ByteBuff& putShortUTF8(const std::string str);

		/**
		 * Create a uint8_tBuff with other position and limit. The new values will be <= current
		 * limit.
		 *
		 * @param start
		 *            buffer start position
		 * @param length
		 *            Set the new limit to position + length
		 * @return A ByteBuff with its own buffer and another position and limit.
		 */
		ByteBuff subBuff(const size_t start, const size_t length) const;

		/// <summary>
		///  Clear it. (just set position and limit to 0).
		/// </summary>
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
		const ByteBuff& write(std::ostream& out, size_t nbBytes) const;

		/**
		 * @throws IOException
		 */
		const ByteBuff& write(std::ostream& out) const;
	};

} // namespace supercloud
