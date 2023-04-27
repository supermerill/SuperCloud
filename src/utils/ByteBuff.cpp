#include "ByteBuff.hpp"

#include <stdexcept>

#include "Utils.hpp"

#include <iomanip>
#include <sstream>
#include <cassert>

namespace supercloud{


void ByteBuff::doubleSize()
{
    uint8_t* old_buffer = this->m_buffer.get();
    const size_t old_length = this->m_length;
    const size_t new_length = std::max(size_t(2), this->m_length * 2);
    uint8_t* new_buffer = new uint8_t[new_length];
    if (old_buffer != nullptr) {
        std::copy(old_buffer, old_buffer + old_length, new_buffer);
    }
    this->m_length = new_length;
    this->m_buffer.reset(new_buffer);
}

/**
 * Set limit to at least this->m_position() + size.
 * Shouldn't be called/used directly. But sometimes i try to re-create this so here it is, available.
 * @param size min size available.
 */
void ByteBuff::expand(const size_t size)
{
    while (this->m_length < this->m_position + size)
    {
        doubleSize();
    }
    if (this->m_limit < this->m_position + size)
    {
        this->m_limit = this->m_position + size;
    }
}


/**
 * Set the limit, the limit is only used for create().
 *
 * @return *this
 */
ByteBuff& ByteBuff::limit(const size_t newLImit)
{
    while (this->m_length < newLImit)
    {
        doubleSize();
    }
    this->m_limit = newLImit;
    return *this;
}

/**
 * Set the limit to the actual this->m_position and set the this->m_position to 0. This method is useful to call a create() after.
 *
 * @return *this
 */
ByteBuff& ByteBuff::flip()
{
    return limit(position()).rewind();
}

ByteBuff& ByteBuff::rewind()
{
    this->m_position = 0;
    return *this;
}
const ByteBuff& ByteBuff::rewind() const
{
    this->m_position = 0;
    return *this;
}

uint8_t ByteBuff::get() const
{
    readcheck(1);
    // return this->m_buffer[this->m_position] then incrementing this->m_position
    return this->m_buffer[this->m_position++];
}

ByteBuff& ByteBuff::put(const uint8_t b)
{
    expand(1);
    this->m_buffer[this->m_position++] = b;
    return *this;
}

std::vector<uint8_t> ByteBuff::get(const size_t nbElt) const
{
    readcheck(nbElt);
    std::vector<uint8_t> newBuff(nbElt);
    std::copy(this->m_buffer.get() + this->m_position, this->m_buffer.get() + this->m_position + nbElt, newBuff.begin());
    //System.arraycopy(this->m_buffer, this->m_position, newBuff, 0, nbElt);
    m_position += nbElt;
    return newBuff;
}

std::vector<uint8_t> ByteBuff::getAll() const
{
    return ByteBuff::get(this->m_limit - this->m_position);
}

ByteBuff& ByteBuff::put(const std::vector<uint8_t>& src)
{
    expand(src.size());
    std::copy(src.begin(), src.end(), this->m_buffer.get() + this->m_position);
    //System.arraycopy(src, 0, this->m_buffer, this->m_position, length);
    this->m_position += src.size();
    return *this;
}

#ifdef _DEBUG
std::vector<uint8_t> ByteBuff::view() const {
    std::vector<uint8_t> newBuff(this->m_limit);
    std::copy(this->m_buffer.get() , this->m_buffer.get() + this->m_limit, newBuff.begin());
    return newBuff;
}
#endif

/**
 * Put data into dest (from this->m_position).
 * @param dest destination array
 * @param destPos start idx in dest
 * @param length nb uint8_ts to copy
 * @return *this
 */
const ByteBuff& ByteBuff::get(uint8_t* dest, const size_t destPos, const size_t length) const
{
    readcheck(length);
    std::copy(this->m_buffer.get() + this->m_position, this->m_buffer.get() + this->m_position + length, dest + destPos);
    //System.arraycopy(this->m_buffer, this->m_position, dest, destPos, length);
    this->m_position += length;
    return *this;
}

ByteBuff& ByteBuff::put(const uint8_t* src, const size_t length)
{
    expand(length);
    std::copy(src, src + length, this->m_buffer.get() + this->m_position);
    //System.arraycopy(src, 0, this->m_buffer, this->m_position, length);
    this->m_position += length;
    return *this;
}

/**
 * Copy the scr.limit-src.position uint8_ts from src.position to src.limit into this.position.
 * @param src soruce array
 * @return *this.
 */
ByteBuff& ByteBuff::put(ByteBuff& src)
{
    assert(src.m_limit > src.m_position);
    const size_t size = src.m_limit - src.m_position;
    expand(size);
    std::copy(src.m_buffer.get() + src.m_position, src.m_buffer.get() + src.m_position + size, this->m_buffer.get() + this->m_position);
    //System.arraycopy(src.m_buffer, src.m_position, this->m_buffer, this->m_position, size);
    this->m_position += size;
    src.m_position += size;
    return *this;
}

ByteBuff& ByteBuff::put(ByteBuff& src, size_t size)
{
    src.readcheck(size);
    expand(size);
    std::copy(src.m_buffer.get() + src.m_position, src.m_buffer.get() + src.m_position + size, this->m_buffer.get() + this->m_position);
    //System.arraycopy(src.m_buffer, src.m_position, this->m_buffer, this->m_position, size);
    this->m_position += size;
    src.m_position += size;
    return *this;
}
const ByteBuff& ByteBuff::get(ByteBuff& dest, size_t size) const
{
    readcheck(size);
    dest.expand(size);
    std::copy(m_buffer.get() + m_position, m_buffer.get() + m_position + size, dest.m_buffer.get() + dest.m_position);
    dest.m_position += size;
    this->m_position += size;
    return *this;
}

ByteBuff& ByteBuff::put(const uint8_t* src, const size_t srcPos, const size_t length)
{
    expand(length);
    std::copy(src + srcPos, src + srcPos + length, this->m_buffer.get() + this->m_position);
    //System.arraycopy(src, srcPos, this->m_buffer, this->m_position, length);
    this->m_position += length;
    return *this;
}

/**
 * Getter to the raw array sed to store data between 0 and limit.
 * @return The raw array.
 */
uint8_t* ByteBuff::raw_array()
{
    return this->m_buffer.get();
}

const uint8_t* ByteBuff::raw_array() const
{
    return this->m_buffer.get();
}
//
//@Override
//std::string tostd::string()
//{
//    return Arrays.tostd::string(this->m_buffer);
//}
//
//@Override
//int32_t hashCode()
//{
//    return buffer.hashCode();
//}
//
//@Override
//boolean equals(const Object ob)
//{
//    return buffer.equals(ob);
//}

//wchar ByteBuff::getWChar()
//{
//    return (wchar)getShort();
//}
//
//ByteBuff& ByteBuff::putWChar(const wchar value)
//{
//    return putShort((short)value);
//}

int16_t ByteBuff::getShort() const
{
    this->readcheck(2);
    const short sh = (int16_t)((this->m_buffer[this->m_position++] & 0xFF) << 8 | this->m_buffer[this->m_position++] & 0xFF);
    return sh;
}

ByteBuff& ByteBuff::putShort(const int16_t value)
{
    expand(2);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF00) >> 8);
    this->m_buffer[this->m_position++] = (uint8_t)(value & 0xFF);
    return *this;
}

uint16_t ByteBuff::getUShort() const
{
    this->readcheck(2);
    const short sh = uint16_t((this->m_buffer[this->m_position++] & 0xFF) << 8 | this->m_buffer[this->m_position++] & 0xFF);
    return sh;
}

ByteBuff& ByteBuff::putUShort(const uint16_t value)
{
    expand(2);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF00) >> 8);
    this->m_buffer[this->m_position++] = (uint8_t)(value & 0xFF);
    return *this;
}

static int32_t getByteLengthTrailInt(const int32_t num)
{
    if (((num << (32 - 7)) >> (32 - 7)) == num)
    {
        // 7 bit sur un uint8_t (8)
        return 1;
    } else if (((num << (32 - 14)) >> (32 - 14)) == num)
    {
        // 14 bitsdeux uint8_t (16)
        return 2;
    } else if (((num << (32 - 21)) >> (32 - 21)) == num)
    {
        // 21 bit sur trois uint8_t (24)
        return 3;
    } else if (((num << (32 - 28)) >> (32 - 28)) == num)
    {
        // 28 bits stockÃ© sur quatre uint8_t (32)
        return 4;
    } else
    {
        // quatre uint8_t (32) stockÃ© sur cinq (40)
        return 5;
    }

}

std::string hex(uint8_t byte) {
    std::stringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << (int32_t)byte;
    return ss.str();
}

ByteBuff& ByteBuff::putTrailInt(const int32_t num)
{
    //does it have only 7 significant bits?
    if ( ((num << (32-7))>>(32-7)) == num)
    {
        // 6 bit + signe sur un uint8_t (8)
        expand(1);
        this->m_buffer[this->m_position++] = uint8_t(num & 0x7F);
    } else if (((num << (32 - 14)) >> (32 - 14)) == num)
    {
        // 13 bits + signe : deux uint8_t (16)
        expand(2);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0x3F00) >> 8 | 0x80);
        this->m_buffer[this->m_position++] = (uint8_t)(num & 0xFF);
    } else if (((num << (32 - 21)) >> (32 - 21)) == num)
    {
        // 20 bit + signe sur trois uint8_t (24)
        expand(3);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0x1F0000) >> 16 | 0xC0);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0xFF00) >> 8);
        this->m_buffer[this->m_position++] = (uint8_t)(num & 0xFF);
    } else if (((num << (32 - 28)) >> (32 - 28)) == num)
    {
        // 27 bits + signe stocke sur quatre uint8_t (32)
        expand(4);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0x0F000000) >> 24 | 0xE0);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0xFF0000) >> 16);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0xFF00) >> 8);
        this->m_buffer[this->m_position++] = (uint8_t)(num & 0xFF);
    }else
    {
        // quatre uint8_t (32) stocke sur cinq (40)
        expand(5);
        this->m_buffer[this->m_position++] = (uint8_t)0xF0;
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0xFF000000) >> 24);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0xFF0000) >> 16);
        this->m_buffer[this->m_position++] = (uint8_t)((num & 0xFF00) >> 8);
        this->m_buffer[this->m_position++] = (uint8_t)(num & 0xFF);
    }
    return *this;
}

int32_t ByteBuff::getTrailInt() const
{
    readcheck(1);
    const uint8_t b = this->m_buffer[this->m_position++];
    if ((b & 0x80) == 0)
    {
        return int32_t(int8_t(b | (b & 0x40) << 1));
    } else if ((b & 0x40) == 0)
    {
        readcheck(1);
        const uint8_t b2 = this->m_buffer[this->m_position++];
        return int16_t((b & 0x3F) << 8 | b2 & 0xFF | (b & 0x20) << 9 | (b & 0x20) << 10);
    } else if ((b & 0x20) == 0)
    {
        readcheck(2);
        const uint8_t b2 = this->m_buffer[this->m_position++];
        const uint8_t b3 = this->m_buffer[this->m_position++];
        return int32_t((b & 0x1F) << 16 | (b2 & 0xFF) << 8 | b3 & 0xFF & 0xFF | ((b & 0x10) == 0 ? 0x00
            : 0XFFF00000));
    } else if ((b & 0x10) == 0)
    {
        readcheck(3);
        const uint8_t b2 = this->m_buffer[this->m_position++];
        const uint8_t b3 = this->m_buffer[this->m_position++];
        const uint8_t b4 = this->m_buffer[this->m_position++];
        return int32_t((b & 0X0F) << 24 | (b2 & 0xFF) << 16 | (b3 & 0xFF) << 8
            | b4 & 0xFF | ((b & 0x08) == 0 ? 0x00 : 0XF0000000));
    } else if ((b & 0x08) == 0)
    {
        readcheck(4);
        return int32_t((this->m_buffer[this->m_position++] & 0xFF) << 24
            | (this->m_buffer[this->m_position++] & 0xFF) << 16
            | (this->m_buffer[this->m_position++] & 0xFF) << 8
            | this->m_buffer[this->m_position++] & 0xFF);
    } else
    {
        // error, unknow int32_t
        throw std::runtime_error("Error, unknow trail int32_t : 0x" + to_hex_str(b & 0x000000FF));
    }

}

size_t ByteBuff::getSize() const
{
    readcheck(1);
    const uint8_t b = this->m_buffer[this->m_position];
    if ((b & 0xFF) == 0xFF) {
        //ulong
        ++this->m_position;
        return getULong();
    } else {
        //trailint
        return size_t(getTrailInt());
    }

}

ByteBuff& ByteBuff::putSize(const size_t num)
{
    //does it have less than 29 significant bits? (can't be negative as it's unsigned)
    if ( (num>>28) == 0) {
        //use trail int
        return putTrailInt(int32_t(num));
    } else {
        //use putULong after the marker byte.
        put(uint8_t(0xFF));
        putULong(num);
        return *this;
    }
}

int32_t ByteBuff::getInt() const
{
    readcheck(4);
    return (this->m_buffer[this->m_position++] & 0xFF) << 24
        | (this->m_buffer[this->m_position++] & 0xFF) << 16
        | (this->m_buffer[this->m_position++] & 0xFF) << 8
        | this->m_buffer[this->m_position++] & 0xFF;
}

ByteBuff& ByteBuff::putInt(const int32_t value)
{
    expand(4);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF000000) >> 24);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF0000) >> 16);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF00) >> 8);
    this->m_buffer[this->m_position++] = (uint8_t)(value & 0xFF);
    return *this;
}


int64_t ByteBuff::getLong() const
{
    readcheck(8);
    //int64_t val = int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 56
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 48
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 40
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 32
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 24
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 16
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 8
    //    | int64_t(this->m_buffer[this->m_position++] & 0xFFL);
    //m_position -= 8;
    //std::stringstream ss;
    //ss << std::hex << std::setw(2) << std::setfill('0')
    //    << (int)this->m_buffer[this->m_position]
    //    << (int)this->m_buffer[this->m_position + 1]
    //    << (int)this->m_buffer[this->m_position + 2]
    //    << (int) this->m_buffer[this->m_position + 3]
    //    << (int) this->m_buffer[this->m_position + 4]
    //    << (int) this->m_buffer[this->m_position + 5]
    //    << (int) this->m_buffer[this->m_position + 6]
    //    << (int) this->m_buffer[this->m_position + 7];
    //std::cout << "read long " << ss.str() << " => " << val << "\n";
    return int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 56
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 48
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 40
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 32
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 24
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 16
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL) << 8
        | int64_t(this->m_buffer[this->m_position++] & 0xFFL);
}

ByteBuff& ByteBuff::putLong(const int64_t value)
{
    expand(8);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF00000000000000l) >> 56);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF000000000000l) >> 48);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF0000000000l) >> 40);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF00000000l) >> 32);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF000000l) >> 24);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF0000l) >> 16);
    this->m_buffer[this->m_position++] = (uint8_t)((value & 0xFF00l) >> 8);
    this->m_buffer[this->m_position++] = (uint8_t)(value & 0xFFl);

    //std::stringstream ss;
    //ss << std::hex << std::setw(2) << std::setfill('0') 
    //    << (int) this->m_buffer[this->m_position - 8]
    //    << (int) this->m_buffer[this->m_position - 8 + 1]
    //    << (int) this->m_buffer[this->m_position - 8 + 2]
    //    << (int) this->m_buffer[this->m_position - 8 + 3]
    //    << (int) this->m_buffer[this->m_position - 8 + 4]
    //    << (int) this->m_buffer[this->m_position - 8 + 5]
    //    << (int) this->m_buffer[this->m_position - 8 + 6]
    //    << (int) this->m_buffer[this->m_position - 8 + 7];
    //std::cout << "write long " << ss.str()
    //    << " => "<< value
    //    << "\n";
    return *this;
}

float ByteBuff::getFloat() const
{
    //return Float.int32_tBitsToFloat(getInt());
    int32_t fl = getInt();
    return *(float*)&fl;
}

ByteBuff& ByteBuff::putFloat(const float value)
{
    //return putInt(Float.floatToRawIntBits(value));
    putInt(*(int32_t*)&value);
    return *this;
}

double ByteBuff::getDouble() const
{
    //return Double.longBitsToDouble(getLong());
    int64_t fl = getLong();
    return *(double*)&fl;
}

ByteBuff& ByteBuff::putDouble(const double value)
{
    //return putLong(Double.doubleToLongBits(value));
    putLong(*(int64_t*)&value);
    return *this;
}

ByteBuff& ByteBuff::putUTF8(const std::string str)
{
    //const ByteBuffer buffUTF8 = UTF8.encode(str);
    putSize(str.size());
    put((uint8_t*)str.data(), 0, str.size());
    return *this;
}

std::string ByteBuff::getUTF8() const
{
    const size_t size = getSize();
    readcheck(size);
    std::string newBuff((char*)this->m_buffer.get() + this->m_position, size);
    //System.arraycopy(this->m_buffer, this->m_position, newBuff.data(), 0, size);
    m_position += size;
    return newBuff;
}

ByteBuff& ByteBuff::putShortUTF8(const std::string str)
{
    putShort(int16_t(std::min(size_t(16000), str.size())));
    put((uint8_t*)str.data(), 0, std::min(size_t(16000), str.size()));
    return *this;
}

std::string ByteBuff::getShortUTF8() const
{
    const int16_t size = getShort();
    readcheck(size);
    std::string newBuff((char*)this->m_buffer.get() + this->m_position, size);
    //System.arraycopy(this->m_buffer, this->m_position, newBuff.data(), 0, size);
    m_position += size;
    return newBuff;
}

/**
 * Create a uint8_tBuff with other this->m_position and limit but the same backing buffer. The new values will be <= current
 * limit. All changes make to the backing buffer are reported.
 *
 * @param start
 *            buffer start this->m_position
 * @param length
 *            Set the new limit to this->m_position + length
 * @return A ByteBuff with same buffer but another this->m_position and limit.
 */
// UNSAFE
// use another Class that can detect when the buffer is erased.
// Or shared_ptr
//ByteBuff ByteBuff::subBuff(const int32_t start, const int32_t length)
//{
//    ByteBuff retVal();
//    retVal.m_buffer = this.m_buffer;
//    retVal.m_position = Math.min(start, this.limit);
//    retVal.m_limit = Math.min(this.limit, start + length);
//    return retVal;
//}
ByteBuff ByteBuff::subBuff(const size_t start, const size_t length) const {
    assert(start + length < limit());
    ByteBuff retVal(length);
    std::copy(this->m_buffer.get() + start, this->m_buffer.get() + start + length, retVal.m_buffer.get());
    return retVal;
}

/**
 * Clear it. pos and limit are now at 0.
 * @return
 */
ByteBuff& ByteBuff::reset() {
    this->m_limit = 0;
    this->m_position = 0;
    return *this;
}


/**
 * copy content in an array of limit()-position() size.
 * @return uint8_t[]
 */
//uint8_t* toArray() {
//    readcheck.available());
//    uint8_t[] newBuff = new uint8_t.available()];
//    System.arraycopy(this->m_buffer, this->m_position, newBuff, 0, limit - this->m_position);
//    return newBuff;
//}

/**
 * Read nbBytes from stream an put it into this.
 * @param in
 * @param nbBytes
 * @return *this
 * @throws IOException
 */
ByteBuff& ByteBuff::read(std::istream& in, size_t nbBytes) {
    expand(nbBytes);
    //in.read(this->m_buffer, this->m_position, nbBytes);
    in.read((char*)this->m_buffer.get() + this->m_position, nbBytes);
    this->m_position += nbBytes;
    return *this;
}

/**
 * Read limit()-position() uint8_ts from stream an put it into this.
 * @param in
 * @return *this
 * @throws IOException
 */
ByteBuff& ByteBuff::read(std::istream& in) {
    readcheck(limit() - this->m_position);
    //in.read(this->m_buffer, this->m_position, limit() - this->m_position);
    in.read((char*)this->m_buffer.get() + this->m_position, limit() - this->m_position);
    this->m_position = limit();
    return *this;
}

/**
 * write nbBytes from this to outputBuffer
 * @param out
 * @param nbBytes
 * @return
 * @throws IOException
 */
const ByteBuff& ByteBuff::write(std::ostream& out, size_t nbBytes) const {
    readcheck(nbBytes);
    //out.write(this->m_buffer, this->m_position, nbBytes);
    out.write((char*)this->m_buffer.get() + this->m_position, nbBytes);
    this->m_position += nbBytes;
    return *this;
}

const ByteBuff& ByteBuff::write(std::ostream& out) const {
    readcheck(limit() - this->m_position);
    out.write((char*)this->m_buffer.get() + this->m_position, limit() - this->m_position);
    this->m_position = this->m_limit;
    return *this;
}


} // namespace supercloud
