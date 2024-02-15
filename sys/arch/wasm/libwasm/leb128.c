
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/null.h>



// borrowed from clang's LEB128.h clang
 
/**
 * Utility function to encode a SLEB128 value to a buffer.
 * 
 * @return Returns the length in bytes of the encoded value.
 */
unsigned
encodeSLEB128(int64_t value, uint8_t *p, unsigned padTo)
{
    uint8_t *orig_p = p;
    unsigned count = 0;
    bool more;
    do {
        uint8_t byte = value & 0x7f;
        // NOTE: this assumes that this signed shift is an arithmetic right shift.
        value >>= 7;
        more = !((((value == 0 ) && ((byte & 0x40) == 0)) || ((value == -1) && ((byte & 0x40) != 0))));
        count++;
        if (more || count < padTo)
            byte |= 0x80; // Mark this byte to show that more bytes will follow.
        *p++ = byte;
    } while (more);

    // Pad with 0x80 and emit a terminating byte at the end.
    if (count < padTo) {
        uint8_t padValue = value < 0 ? 0x7f : 0x00;
        for (; count < padTo - 1; ++count)
            *p++ = (padValue | 0x80);
        *p++ = padValue;
    }
    return (unsigned)(p - orig_p);
}
 
 
/**
 * Utility function to encode a ULEB128 value to a buffer.
 * 
 * @return Returns the length in bytes of the encoded value.
 */
unsigned
encodeULEB128(uint64_t value, uint8_t *p, unsigned padTo)
{
    uint8_t *orig_p = p;
    unsigned count = 0;
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        count++;
        if (value != 0 || count < padTo)
            byte |= 0x80; // Mark this byte to show that more bytes will follow.
        *p++ = byte;
    } while (value != 0);
 
    // Pad with 0x80 and emit a null byte at the end.
    if (count < padTo) {
        for (; count < padTo - 1; ++count)
            *p++ = '\x80';
        *p++ = '\x00';
    }
 
    return (unsigned)(p - orig_p);
}
 
/*!
 * Utility function to decode a ULEB128 Value.
 * 
 * If \p error is non-null, it will point to a static error message, if an error occured. It will not be modified on success.
 * 
 * @param n NULL is accepted.
 * @param end NULL is accepted.
 * @param error NULL is accepted.
 */
uint64_t
decodeULEB128(const uint8_t *p, unsigned *n, const uint8_t *end, const char **error)
{
    const uint8_t *orig_p = p;
    uint64_t value = 0;
    unsigned shift = 0;
    do {
        if (p == end) {
            if (error)
                *error = "malformed uleb128, extends past end";
            value = 0;
            break;
        }
        uint64_t Slice = *p & 0x7f;
        if ((shift >= 63) &&
            ((shift == 63 && (Slice << shift >> shift) != Slice) ||
            (shift > 63 && Slice != 0))) {
            if (error)
                *error = "uleb128 too big for uint64";
            value = 0;
            break;
        }
        value += Slice << shift;
        shift += 7;
    } while (*p++ >= 128);

    if (n)
        *n = (unsigned)(p - orig_p);
    
    return value;
}
 

/*!
 * Utility function to decode a SLEB128 value.
 *
 * If \p error is non-null, it will point to a static error message, if an error occured. It will not be modified on success.
 *
 * @param n NULL is accepted.
 * @param end NULL is accepted.
 * @param error NULL is accepted.
 */
int64_t
decodeSLEB128(const uint8_t *p, unsigned *n, const uint8_t *end, const char **error)
{
    const uint8_t *orig_p = p;
    int64_t value = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
        if (p == end) {
            if (error)
                *error = "malformed sleb128, extends past end";
            if (n)
                *n = (unsigned)(p - orig_p);
            return 0;
        }
        byte = *p;
        uint64_t Slice = byte & 0x7f;
        if ((shift >= 63) &&
            ((shift == 63 && Slice != 0 && Slice != 0x7f) ||
            (shift > 63 && Slice != (value < 0 ? 0x7f : 0x00)))) {
            if (error)
                *error = "sleb128 too big for int64";
            if (n)
                *n = (unsigned)(p - orig_p);
            return 0;
        }
        value |= Slice << shift;
        shift += 7;
        ++p;
    } while (byte >= 128);
    
    // Sign extend negative numbers if needed.
    if (shift < 64 && (byte & 0x40))
        value |= UINT64_MAX << shift;
    
    if (n)
        *n = (unsigned)(p - orig_p);
    
    return value;
}

/**
 * Utility function to get the size of the ULEB128-encoded value.
 * 
 * @return 
 */
unsigned
getULEB128Size(uint64_t value)
{
    unsigned size = 0;
    do {
        value >>= 7;
        size += sizeof(int8_t);
    } while (value);
    
    return size;
}
 
/**
 * Utility function to get the size of the SLEB128-encoded value.
 *
 * @return 
 */
unsigned
getSLEB128Size(int64_t value)
{
    unsigned size = 0;
    int sign = value >> (8 * sizeof(value) - 1);
    bool isMore;
 
    do {
        unsigned byte = value & 0x7f;
        value >>= 7;
        isMore = value != sign || ((byte ^ sign) & 0x40) != 0;
        size += sizeof(int8_t);
    } while (isMore);
    
    return size;
}
