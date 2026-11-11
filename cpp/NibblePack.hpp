#pragma once
#ifndef NIBBLE_PACK_HPP
#define NIBBLE_PACK_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#   include <intrin.h>
#endif

#if defined(_MSVC_LANG) and _MSVC_LANG >= 201703L
#   define NIBBLE_PACK_CPP_LIB_STRING_VIEW
#elif __cplusplus >= 201703L
#   define NIBBLE_PACK_CPP_LIB_STRING_VIEW
#endif

#if defined(NIBBLE_PACK_CPP_LIB_STRING_VIEW)
#   include <string_view>
#else
#   include "string_view.hpp" // https://github.com/martinmoene/string-view-lite
namespace std {
using string_view = nonstd::string_view;
}
#endif


namespace Niblp {

enum class Tag : uint8_t {
    Null,
    Array,
    Map,
    String,
    Bytes,
    False,
    True,
    NaN,
    pInf,
    mInf,
    ResetToValue,     // absolute value follows; resets the chain
    ResetToFloatZero, // +0.0 into the float chain
    ResetToIntZero,   // 0 into the int chain
    ResetTo,          // a TagR nibble follows: resets to an extreme constant
    Reserved_0,
    End,
};
static_assert(uint32_t(Tag::End) == 0xF, "");

enum class TagR : uint8_t {
    ResetToInt32Min,
    ResetToInt32Max,
    ResetToUInt32Max,
    ResetToInt64Min,
    ResetToInt64Max,
    ResetToUInt64Max,
};

enum class TagV : uint8_t {
    Int_p0,   // 0xxx
    Int_p1,   // 0xxx
    Int_p2,   // 0xxx
    Int_p3,   // 0xxx
    Int_m4,   // 0xxx
    Int_m3,   // 0xxx
    Int_m2,   // 0xxx
    Int_m1,   // 0xxx
    VarIntP0, // 10sx
    VarIntP1, // 10sx
    VarIntM0, // 10sx
    VarIntM1, // 10sx
    VarFloat0,// 110z: z=0 exact hit (XOR = 0)
    VarFloat1,// 110z: z=1 XOR != 0: mantissa & zeros: 4-bit −7..+7 or 1111 + 8-bit absolute
    Tag,      // 1110
    End,      // 1111
};
static_assert(uint32_t(TagV::End) == 0xF, "");

// 65-bit sign-magnitude integer [-UINT64_MAX, +UINT64_MAX], zero is +0 (neg = false)
struct Int65 {
    uint64_t mag = 0; // magnitude
    bool neg = false; // negative flag

    constexpr Int65() noexcept = default;
    constexpr Int65(uint64_t magnitude, bool negative) noexcept
        : mag(magnitude), neg(negative) {}

    // r = *this - b; returns false when |*this - b| would overflow the uint64 magnitude.
    bool sub(Int65 b, Int65& r) const {
        if (neg != b.neg) {
            // opposite signs: magnitudes add, the result keeps this sign
            if (mag > UINT64_MAX - b.mag) { // mag + b.mag would overflow
                return false;
            }
            r.mag = mag + b.mag;
            r.neg = neg;
            return true;
        }
        // same sign: magnitudes subtract, the larger one sets the sign
        if (mag >= b.mag) {
            r.mag = mag - b.mag;
            r.neg = neg;
        }
        else {
            r.mag = b.mag - mag;
            r.neg = not neg; // |b| > |this|: crosses zero, the sign flips
        }
        if (r.mag == 0) {
            r.neg = false;
        }
        return true;
    }

    // r = *this + b, implemented as *this - (-b); same overflow contract.
    bool add(Int65 b, Int65& r) const {
        return sub(Int65{b.mag, not b.neg}, r);
    }
};

template <typename value_t>
int32_t countl_zero(value_t value) { // C++20-like
    static_assert(std::is_integral<value_t>::value
            and not std::is_same<value_t, bool>::value,
        "countl_zero requires a non-bool integral type");

    typedef typename std::make_unsigned<value_t>::type unsigned_t;
    const unsigned_t bits = static_cast<unsigned_t>(value);
    const uint32_t width = static_cast<uint32_t>(
        std::numeric_limits<unsigned_t>::digits);

    // Every intrinsic below is undefined (or reports failure) for zero.
    if (bits == 0) {
        return width;
    }
# if defined(__GNUC__) or defined(__clang__)
    if (width <= 64) {
        if (width <= 32) {
            return static_cast<int32_t>(__builtin_clz(static_cast<uint32_t>(bits)))
                - (32 - width);
        }
        return static_cast<int32_t>(__builtin_clzll(static_cast<uint64_t>(bits)))
            - (64 - width);
    }
# elif defined(_MSC_VER)
    if (width <= 64) {
        unsigned long index = 0;
        if (width <= 32) {
            _BitScanReverse(&index, static_cast<uint32_t>(bits));
            return static_cast<int32_t>((31 - index) - (32 - width));
        }
#     if defined(_M_X64) or defined(_M_ARM64)
        _BitScanReverse64(&index, static_cast<uint64_t>(bits));
        return static_cast<int32_t>((63 - index) - (64 - width));
#     else
        const uint32_t hi = static_cast<uint32_t>(
            static_cast<uint64_t>(bits) >> 32);
        if (hi != 0) {
            _BitScanReverse(&index, hi);
            return static_cast<int32_t>(31 - index);
        }
        _BitScanReverse(&index, static_cast<uint32_t>(bits));
        return static_cast<int32_t>(32 + (31 - index));
#     endif
    }
# endif
    int32_t count = 0;
    while (count < width
            and ((bits >> (width - 1 - count)) & unsigned_t(1)) == 0) {
        ++count;
    }
    return count;
}

template <typename value_t>
int32_t countr_zero(value_t value) { // C++20-like
    static_assert(std::is_integral<value_t>::value
            and not std::is_same<value_t, bool>::value,
        "countr_zero requires a non-bool integral type");

    typedef typename std::make_unsigned<value_t>::type unsigned_t;
    const unsigned_t bits = static_cast<unsigned_t>(value);
    const uint32_t width = static_cast<uint32_t>(
        std::numeric_limits<unsigned_t>::digits);

    // Every intrinsic below is undefined (or reports failure) for zero.
    if (bits == 0) {
        return width;
    }
# if defined(__GNUC__) or defined(__clang__)
    if (width <= 64) {
        if (width <= 32) {
            return static_cast<int32_t>(__builtin_ctz(static_cast<uint32_t>(bits)));
        }
        return static_cast<int32_t>(__builtin_ctzll(static_cast<uint64_t>(bits)));
    }
# elif defined(_MSC_VER)
    if (width <= 64) {
        unsigned long index = 0;
        if (width <= 32) {
            _BitScanForward(&index, static_cast<uint32_t>(bits));
            return static_cast<int32_t>(index);
        }
#     if defined(_M_X64) or defined(_M_ARM64)
        _BitScanForward64(&index, static_cast<uint64_t>(bits));
        return static_cast<int32_t>(index);
#     else
        const uint32_t lo = static_cast<uint32_t>(bits);
        if (lo != 0) {
            _BitScanForward(&index, lo);
            return static_cast<int32_t>(index);
        }
        _BitScanForward(&index, static_cast<uint32_t>(bits >> 32));
        return static_cast<int32_t>(32 + index);
#     endif
    }
# endif
    int32_t count = 0;
    while (((bits >> count) & unsigned_t(1)) == 0) {
        ++count;
    }
    return count;
}

template <typename T>
struct is_trivially_copyable_compat
#if (defined(__GNUC__) && __GNUC__ >= 5) || defined(_MSC_VER) \
    || defined(_LIBCPP_VERSION) || defined(_CPPLIB_VER)
    : std::is_trivially_copyable<T> {};
#else
    : std::is_trivial<T> {};
#endif

template <typename to_t, typename from_t>
inline to_t bit_cast(const from_t& from) { // C++20-like
    static_assert(sizeof(to_t) == sizeof(from_t),
        "bit_cast requires types of the same size");
    static_assert(is_trivially_copyable_compat<from_t>::value
            and is_trivially_copyable_compat<to_t>::value,
        "bit_cast requires trivially copyable types");
    union { from_t from; to_t to; } caster;
    caster.from = from;
    return caster.to;
}

class Reader;
class Writer;

class Value {
public:
    Value(Reader* reader);

    bool is_array() const;
    void array(const std::function<void(uint32_t idx, const Value& value)>& handler) const;

    bool is_map() const;
    void map(const std::function<void(const Value& key, const Value& value)>& handler) const;

    bool is_number() const;

    bool is_null() const;

    bool is_int() const;
    int64_t as_int() const;

    bool is_uint() const;
    uint64_t as_uint() const;

    bool is_float() const;
    float as_float32() const;
    double as_float64() const;

    bool is_bool() const;
    bool as_bool() const;

    bool is_string() const;
    std::string_view as_string() const;

    bool is_bytes() const;
    const uint8_t* as_bytes() const;
    size_t size_bytes_B() const;

private:
    friend class Reader;
    Reader* m_reader = nullptr;
    union {
        Reader* reader;
        std::pair<const uint8_t*, size_t> bytes;
        std::pair<const char*, size_t> string;
        uint64_t u64;
        int64_t i64;
        float f32;
        double f64;
        bool boolean;
    } m_union = {};
    enum class Variant : uint8_t {
        Undefined,
        Array,
        Map,
        UInt,
        Int,
        Float,
        Null,
        Bool,
        String,
        Bytes,
    } m_variant = Variant::Undefined;
};

class Reader {
public:
    void assign(const void* data, size_t size_B);
    void reset();
    void parse(const std::function<void(const Value& value)>& handler);

private:
    friend class Value;
    uint8_t nibble();
    bool nextTag(Value& value);
    bool nextTagV(Value& value);

    double m_prevFloat = 0.0;
    double m_prev2Float = 0.0;
    uint32_t m_prevTrailZeros = 0; // count of zeros on the right of float-XOR-delta
    Int65 m_prevInt;  // second-order chain, [INT64_MIN, UINT64_MAX]
    Int65 m_prev2Int;
    Int65 m_prevSize; // second-order chain of the bytes sizes
    Int65 m_prev2Size;
    const uint8_t* m_data = nullptr;
    size_t m_size_B = 0;
    size_t m_nibbleIdx = 0;
    bool m_reset = false;
    bool m_sizeChain = false;
};

class Writer;
class Map;

class Array {
public:
    Array(Writer* writer);
    Array& value(int32_t number);
    Array& value(uint32_t number);
    Array& value(int64_t number);
    Array& value(uint64_t number);
    Array& value(float number);
    Array& value(double number);
    template <typename bool_t,
        typename std::enable_if<std::is_same<bool_t, bool>::value, int>::type = 0>
    Array& value(const bool_t boolean);
    Array& value(std::nullptr_t = nullptr);
    Array& value(std::string_view string);

    Array& bytes(const void* data, uint32_t size_B);

    Array& array(const std::function<void(Array)>& handler);
    Array& map(const std::function<void(Map)>& handler);

private:
    Writer* m_writer = nullptr;
};

class MapValue {
public:
    MapValue(Writer* writer, Map* map);

    Map& value(int32_t number);
    Map& value(uint32_t number);
    Map& value(int64_t number);
    Map& value(uint64_t number);
    Map& value(float number);
    Map& value(double number);
    template <typename bool_t,
        typename std::enable_if<std::is_same<bool_t, bool>::value, int>::type = 0>
    Map& value(const bool_t boolean);
    Map& value(std::nullptr_t = nullptr);
    Map& value(std::string_view string);

    Map& bytes(const void* data, uint32_t size_B);

    Map& array(const std::function<void(Array)>& handler);
    Map& map(const std::function<void(Map)>& handler);

private:
    Writer* m_writer = nullptr;
    Map* m_map = nullptr;
};

class Map {
public:
    Map(Writer* writer);

    MapValue& key(int32_t number);
    MapValue& key(uint32_t number);
    MapValue& key(int64_t number);
    MapValue& key(uint64_t number);
    MapValue& key(std::string_view string);
    MapValue& key(const void* data, uint32_t size_B);
    MapValue& key(const std::function<void(Array)>& handler);

private:
    friend class Writer;
    friend class Array;
    Writer* m_writer = nullptr;
    MapValue m_mapValue;
};

class Writer {
public:
    std::vector<uint8_t> buffer;
    double tolerance = 0.0001; // 0..1
    void tolerance_prc(float prc);
    float tolerance_prc() const;
    void reset();

    void array(const std::function<void(Array)>& handler);
    void map(const std::function<void(Map)>& handler);

# if defined(NIBBLE_PACK_STATISTICS)
    std::array<uint32_t, 9> idist; // 3, 5, 7, 10, 17, 27, 38, 49, 64 bits
    std::array<uint32_t, 9> fdist; // VarFloat::E11M52 - VarFloat::E3M4
# endif

private:
    friend class Array;
    friend class Map;
    friend class MapValue;
    void nibble(uint8_t nibble);
    void allign();
    void write_raw(const void* data, size_t size_B);

    //void value(int32_t number);
    //void value(uint32_t number);
    void value(int64_t number);
    void value(uint64_t number);
    //void value(float number);
    void value(double number);
    void value(std::string_view string);

    // Tolerance of the value quantization (fraction of the value, 0 = lossless).
    void value_bool(const bool boolean);

    enum class VarInt : uint8_t {
        //  0 .. 3
        p0, p1, p2, p3,
        // -4 .. -1
        m4, m3, m2, m1,
        //  4 .. 19
        p4_19,
        // -20 .. -5
        m20_5,
        // ±int  +2  7 bits 1.5 B   10±x 1xxx 0xxx
        pInt7,
        mInt7,
        // ±int  +3 10 bits   2 B   10±x 1xxx 1xxx 0xxx
        pInt10,
        mInt10,
        // ±int  +7 17 bits   3 B   10±x 1xxx 1xxx 1xxx 0xxx xxxx
        pInt17,
        mInt17,
        // ±int +10 27 bits 4.5 B   10±x 1xxx 1xxx 1xxx 1xxx 1xxx 0xxx xxxx xxxx
        pInt27,
        mInt27,
        // ±int +11 38 bits   6 B   10±x 1xxx 1xxx 1xxx 1xxx 1xxx 1xxx 0xxx xxxx xxxx xxxx xxxx
        pInt38,
        mInt38,
        // ±int +11 49 bits 7.5 B   10±x 1xxx 1xxx 1xxx 1xxx 1xxx 1xxx 1xxx 0xxx xxxx xxxx xxxx xxxx xxxx xxxx
        pInt49,
        mInt49,
        // ±int +15 64 bits   9 B   10±x 1xxx 1xxx 1xxx 1xxx 0xxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
        pInt64,
        mInt64,
    };
    static VarInt matchInt(uint64_t magnitude, bool negative);
        static uint32_t nibblesCount(VarInt variant);

    void valueInt(uint64_t magnitude, bool negative, Int65& prev, Int65& prev2);
    void writeInt(VarInt variant, bool negative, uint64_t bits);

    double m_prevFloat = 0.0;
    double m_prev2Float = 0.0;
    uint32_t m_prevTrailZeros = 0;
    Int65 m_prevInt;
    Int65 m_prev2Int;
    Int65 m_prevSize;
    Int65 m_prev2Size;
    bool m_isHighNibbleEmpty = true;
};

template <typename bool_t,
    typename std::enable_if<std::is_same<bool_t, bool>::value, int>::type>
Array& Array::value(const bool_t boolean) {
    m_writer->value_bool(boolean);
    return *this;
}

template <typename bool_t,
    typename std::enable_if<std::is_same<bool_t, bool>::value, int>::type>
Map& MapValue::value(const bool_t boolean) {
    m_writer->value_bool(boolean);
    return *m_map;
}

} // namespace Niblp

#endif // NIBBLE_PACK_HPP
