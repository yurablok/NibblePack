#include "NibblePack.hpp"
#include "fp_t.hpp"
using Niblp::fp_t;

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>


namespace c { namespace {

constexpr int32_t int3min = -4;
constexpr int32_t int3max = 3;
constexpr int32_t int3nom = -int3min;
constexpr int32_t int3nop = int3max + 1;
static_assert(int3nom + int3nop == 1 << 3, "");
constexpr int32_t int5sum = int3nom + int3nop + (1 << 5);
constexpr int32_t int5min = -(int5sum / 2);
constexpr int32_t int5max = (int5sum / 2) - 1;
constexpr int32_t int5fom = int3min - 1;
constexpr int32_t int5fop = int3max + 1;
constexpr int32_t int5nom = -int5min + int5fom + 1;
constexpr int32_t int5nop = int5max - int5fop + 1;
static_assert(int5nom + int5nop == 1 << 5, "");

}} // namespace c::_


namespace Niblp {

Value::Value(Reader* reader) : m_reader(reader) {}

bool Value::is_array() const {
    return m_variant == Variant::Array;
}
void Value::array(const std::function<void(uint32_t idx, const Value& value)>& handler) const {
    uint32_t idx = 0;
    Value value(m_reader);
    while (m_reader->nextTagV(value)) {
        if (handler) {
            const auto prevNibbleIdx = m_reader->m_nibbleIdx;
            handler(idx, value);
            if (m_reader->m_nibbleIdx == prevNibbleIdx) {
                if (value.is_array()) {
                    value.array(nullptr);
                }
                else if (value.is_map()) {
                    value.map(nullptr);
                }
            }
        }
        else if (value.is_array()) {
            value.array(nullptr);
        }
        else if (value.is_map()) {
            value.map(nullptr);
        }
        ++idx;
    }
}

bool Value::is_map() const {
    return m_variant == Variant::Map;
}
void Value::map(const std::function<void(const Value& key, const Value& value)>& handler) const {
    Value key(m_reader);
    Value value(m_reader);
    while (m_reader->nextTagV(key)) {
        if (not m_reader->nextTagV(value)) {
            break;
        }
        if (handler) {
            const auto prevNibbleIdx = m_reader->m_nibbleIdx;
            handler(key, value);
            if (m_reader->m_nibbleIdx == prevNibbleIdx) {
                if (value.is_array()) {
                    value.array(nullptr);
                }
                else if (value.is_map()) {
                    value.map(nullptr);
                }
            }
        }
        else if (value.is_array()) {
            value.array(nullptr);
        }
        else if (value.is_map()) {
            value.map(nullptr);
        }
    }
}

bool Value::is_number() const {
    return is_int() or is_float();
}

bool Value::is_int() const {
    return m_variant == Variant::Int or m_variant == Variant::UInt;
}
int64_t Value::as_int() const {
    return m_union.i64;
}
bool Value::is_uint() const {
    return m_variant == Variant::UInt;
}
uint64_t Value::as_uint() const {
    return m_union.u64;
}

bool Value::is_float() const {
    return m_variant == Variant::Float;
}
float Value::as_float32() const {
    return static_cast<float>(as_float64());
}
double Value::as_float64() const {
    switch (m_variant) {
    case Variant::UInt:     return static_cast<double>(m_union.u64);
    case Variant::Int:      return static_cast<double>(m_union.i64);
    case Variant::Float:    return m_union.f64;
    default: assert(false); return std::numeric_limits<double>::quiet_NaN();
    }
}

bool Value::is_null() const {
    return m_variant == Variant::Null;
}

bool Value::is_bool() const {
    return m_variant == Variant::Bool;
}
bool Value::as_bool() const {
    return m_union.boolean;
}

bool Value::is_string() const {
    return m_variant == Variant::String;
}
std::string_view Value::as_string() const {
    return { m_union.string.first, m_union.string.second };
}

bool Value::is_bytes() const {
    return m_variant == Variant::Bytes;
}
const uint8_t* Value::as_bytes() const {
    return m_union.bytes.first;
}
size_t Value::size_bytes_B() const {
    return m_union.bytes.second;
}

void Reader::assign(const void* data, size_t size_B) {
    m_data = static_cast<const uint8_t*>(data);
    m_size_B = size_B;
    reset();
}

void Reader::reset() {
    m_nibbleIdx = 0;
    m_prevInt = {};
    m_prev2Int = {};
    m_prevSize = {};
    m_prev2Size = {};
    m_prevFloat = 0.0;
    m_prev2Float = 0.0;
    m_prevTrailZeros = 0;
    m_reset = false;
}

void Reader::parse(const std::function<void(const Value& value)>& handler) {
    Value value(this);
    while (nextTag(value)) {
        if (value.is_array()) {
            if (handler) {
                const auto prevNibbleIdx = m_nibbleIdx;
                handler(value);
                if (m_nibbleIdx == prevNibbleIdx) {
                    value.array(nullptr);
                }
            }
            else {
                value.array(nullptr);
            }
        }
        else if (value.is_map()) {
            if (handler) {
                const auto prevNibbleIdx = m_nibbleIdx;
                handler(value);
                if (m_nibbleIdx == prevNibbleIdx) {
                    value.map(nullptr);
                }
            }
            else {
                value.map(nullptr);
            }
        }
        // else assert(false)
        else if (handler) {
            handler(value);
        }
    }
}

uint8_t Reader::nibble() {
    const uint8_t byte = m_data[m_nibbleIdx / 2];
    if (m_nibbleIdx++ & 1) {
        return byte & 0x0F;
    }
    else {
        return byte >> 4;
    }
}

bool Reader::nextTag(Value& value) {
    value.m_variant = Value::Variant::Undefined;
    Int65& prevInt = m_sizeChain ? m_prevSize : m_prevInt;
    Int65& prev2Int = m_sizeChain ? m_prev2Size : m_prev2Int;
    if (m_size_B <= m_nibbleIdx / 2) {
        return false;
    }
    switch (Tag(nibble())) {
    case Tag::Null:
        value.m_variant = Value::Variant::Null;
        return true;
    case Tag::Array:
        value.m_variant = Value::Variant::Array;
        value.m_union.reader = this;
        return true;
    case Tag::Map:
        value.m_variant = Value::Variant::Map;
        value.m_union.reader = this;
        return true;
    case Tag::String: {
        if ((m_nibbleIdx & 1) != 0
                and (m_size_B <= m_nibbleIdx / 2 or nibble() != 0)) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const size_t offset_B = m_nibbleIdx / 2;
        if (m_size_B <= offset_B) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const char* begin = reinterpret_cast<const char*>(m_data + offset_B);
        const void* end = std::memchr(begin, 0, m_size_B - offset_B);
        if (end == nullptr) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const size_t size_B = size_t(static_cast<const char*>(end) - begin);
        m_nibbleIdx = (offset_B + size_B + 1) * 2;
        value.m_variant = Value::Variant::String;
        value.m_union.string = { begin, size_B };
        return true;
    }
    case Tag::Bytes: {
        Value sizeValue(this);
        m_sizeChain = true;
        const bool sizeOk = nextTagV(sizeValue);
        m_sizeChain = false;
        if (not sizeOk or not sizeValue.is_uint()) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const uint64_t size_B = sizeValue.as_uint();
        if ((m_nibbleIdx & 1) != 0
                and (m_size_B <= m_nibbleIdx / 2 or nibble() != 0)) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const size_t offset_B = m_nibbleIdx / 2;
        if (size_B > uint64_t(m_size_B - offset_B)) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const uint8_t* data = m_data + offset_B;
        m_nibbleIdx = (offset_B + size_B) * 2;
        value.m_variant = Value::Variant::Bytes;
        value.m_union.bytes = { data, size_t(size_B) };
        return true;
    }
    case Tag::False:
        value.m_variant = Value::Variant::Bool;
        value.m_union.boolean = false;
        return true;
    case Tag::True:
        value.m_variant = Value::Variant::Bool;
        value.m_union.boolean = true;
        return true;
    case Tag::ResetToFloatZero:
        m_prev2Float = m_prevFloat;
        m_prevFloat = 0.0;
        value.m_variant = Value::Variant::Float;
        value.m_union.f64 = 0.0;
        return true;
    case Tag::ResetToIntZero:
        prev2Int = prevInt;
        prevInt = {};
        value.m_variant = Value::Variant::UInt;
        value.m_union.u64 = 0;
        return true;
    case Tag::ResetTo: {
        if (m_nibbleIdx / 2 >= m_size_B) {
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        const TagR tagr = TagR(nibble());
        Int65 v;
        switch (tagr) {
        case TagR::ResetToInt32Min:
            v = Int65{uint64_t(1) << 31, true};
            break;
        case TagR::ResetToInt32Max:
            v = Int65{(uint64_t(1) << 31) - 1, false};
            break;
        case TagR::ResetToUInt32Max:
            v = Int65{(uint64_t(1) << 32) - 1, false};
            break;
        case TagR::ResetToInt64Min:
            v = Int65{uint64_t(1) << 63, true};
            break;
        case TagR::ResetToInt64Max:
            v = Int65{(uint64_t(1) << 63) - 1, false};
            break;
        case TagR::ResetToUInt64Max:
            v = Int65{UINT64_MAX, false};
            break;
        default:
            assert(false);
            m_nibbleIdx = m_size_B * 2;
            return false;
        }
        prev2Int = v;
        prevInt = v;
        if (v.neg) {
            value.m_variant = Value::Variant::Int;
            value.m_union.i64 = int64_t(uint64_t(0) - v.mag);
        }
        else {
            value.m_variant = Value::Variant::UInt;
            value.m_union.u64 = v.mag;
        }
        return true;
    }
    case Tag::NaN:
        value.m_variant = Value::Variant::Float;
        value.m_union.f64 = std::numeric_limits<double>::quiet_NaN();
        return true;
    case Tag::pInf:
        value.m_variant = Value::Variant::Float;
        value.m_union.f64 = std::numeric_limits<double>::infinity();
        return true;
    case Tag::mInf:
        value.m_variant = Value::Variant::Float;
        value.m_union.f64 = -std::numeric_limits<double>::infinity();
        return true;
    case Tag::ResetToValue:
        m_reset = true;
        return nextTagV(value);
    case Tag::Reserved_0:
        assert(false);
        m_nibbleIdx = m_size_B * 2;
        return false;
    case Tag::End:
        return false;
    default:
        assert(false);
        m_nibbleIdx = m_size_B * 2;
        return false;
    }
}

bool Reader::nextTagV(Value& value) {
    value.m_variant = Value::Variant::Undefined;
    Int65& prevInt = m_sizeChain ? m_prevSize : m_prevInt;
    Int65& prev2Int = m_sizeChain ? m_prev2Size : m_prev2Int;
    // Tag::ResetToValue resets the chain; the next token sets both chain values,
    // so the second-order prediction degenerates to the first difference.
    bool chainCold = false;

    const auto add = [&](uint64_t delta) {
        // value = (2*p1 - p2) + delta over sign-magnitude pairs;
        Int65 base;
        Int65 y;
        const Int65 p1x2{prevInt.mag * 2, prevInt.neg};
        const bool ok = p1x2.sub(prev2Int, base)
            and base.add(Int65{delta, false}, y);
        assert(ok);
        prev2Int = chainCold ? y : prevInt;
        prevInt = y;
        chainCold = false;
    };
    const auto sub = [&](uint64_t delta) {
        Int65 base;
        Int65 y;
        const Int65 p1x2{prevInt.mag * 2, prevInt.neg};
        const bool ok = p1x2.sub(prev2Int, base)
            and base.sub(Int65{delta, false}, y);
        assert(ok);
        prev2Int = chainCold ? y : prevInt;
        prevInt = y;
        chainCold = false;
    };
    const auto set = [&]() {
        if (prevInt.neg) {
            value.m_variant = Value::Variant::Int;
            value.m_union.i64 = int64_t(uint64_t(0) - prevInt.mag);
        }
        else {
            value.m_variant = Value::Variant::UInt;
            value.m_union.u64 = prevInt.mag;
        }
    };

    while (m_nibbleIdx / 2 < m_size_B) {
        const TagV tagv = TagV(nibble());
        if (m_reset) {
            m_reset = false;
            switch (tagv) {
            case TagV::Int_p0: case TagV::Int_p1: case TagV::Int_p2: case TagV::Int_p3:
            case TagV::Int_m4: case TagV::Int_m3: case TagV::Int_m2: case TagV::Int_m1:
            case TagV::VarIntP0: case TagV::VarIntP1: case TagV::VarIntM0: case TagV::VarIntM1:
                prevInt = {};
                prev2Int = {};
                chainCold = true;
                break;
            case TagV::VarFloat0: case TagV::VarFloat1:
                m_prevFloat = 0.0;
                m_prev2Float = 0.0;
                chainCold = true;
                break;
            default:
                assert(false);
                m_nibbleIdx = m_size_B * 2;
                return false;
            }
        }
        uint8_t count3b = 0;
        uint64_t bits = 0;
        switch (tagv) {
        case TagV::Int_p0:  add(0); set(); return true;
        case TagV::Int_p1:  add(1); set(); return true;
        case TagV::Int_p2:  add(2); set(); return true;
        case TagV::Int_p3:  add(3); set(); return true;
        case TagV::Int_m4:  sub(4); set(); return true;
        case TagV::Int_m3:  sub(3); set(); return true;
        case TagV::Int_m2:  sub(2); set(); return true;
        case TagV::Int_m1:  sub(1); set(); return true;
        case TagV::VarIntP0:
        case TagV::VarIntP1:
            bits = uint8_t(tagv) & 0x1;
            while (m_nibbleIdx / 2 < m_size_B) {
                const uint8_t nbl = nibble();
                if ((nbl & 0x8) != 0) {
                    ++count3b;
                    bits <<= 3;
                    bits |= nbl & 0x7;
                }
                else {
                    ++count3b;
                    bits <<= 3;
                    bits |= nbl & 0x7;
                    uint8_t count4b = 0;
                    switch (count3b) {
                    case 1:
                        bits |= uint64_t(uint8_t(tagv) & 0x2) << 3;
                        if (bits < uint64_t(c::int5nop)) {
                            add(bits + uint64_t(c::int5fop));
                        }
                        else {
                            sub(uint64_t(int64_t(bits) - c::int5nop - c::int5fom));
                        }
                        set();
                        return true;
                    case 2: count4b = 0; break;
                    case 3: count4b = 0; break;
                    case 4: count4b = 1; break;
                    case 5: count4b = 12; break;
                    case 6: count4b = 2; break;
                    case 7: count4b = 4; break;
                    case 8: count4b = 6; break;
                    default:
                        assert(false);
                        m_nibbleIdx = m_size_B * 2;
                        return false;
                    }
                    for (uint8_t i = 0; i < count4b; ++i) {
                        if (m_size_B <= m_nibbleIdx / 2) {
                            m_nibbleIdx = m_size_B * 2;
                            return false;
                        }
                        bits <<= 4;
                        bits |= nibble();
                    }
                    add(bits);
                    set();
                    return true;
                }
            }
            break;
        case TagV::VarIntM0:
        case TagV::VarIntM1:
            bits = uint8_t(tagv) & 0x1;
            while (m_nibbleIdx / 2 < m_size_B) {
                const uint8_t nbl = nibble();
                if ((nbl & 0x8) != 0) {
                    ++count3b;
                    bits <<= 3;
                    bits |= nbl & 0x7;
                }
                else {
                    ++count3b;
                    bits <<= 3;
                    bits |= nbl & 0x7;
                    uint8_t count4b = 0;
                    switch (count3b) {
                    case 1:
                        bits |= uint64_t(uint8_t(tagv) & 0x2) << 3;
                        if (bits < uint64_t(c::int5nop)) {
                            add(bits + uint64_t(c::int5fop));
                        }
                        else {
                            sub(uint64_t(int64_t(bits) - c::int5nop - c::int5fom));
                        }
                        set();
                        return true;
                    case 2: count4b = 0; break;
                    case 3: count4b = 0; break;
                    case 4: count4b = 1; break;
                    case 5: count4b = 12; break;
                    case 6: count4b = 2; break;
                    case 7: count4b = 4; break;
                    case 8: count4b = 6; break;
                    default:
                        assert(false);
                        m_nibbleIdx = m_size_B * 2;
                        return false;
                    }
                    for (uint8_t i = 0; i < count4b; ++i) {
                        if (m_size_B <= m_nibbleIdx / 2) {
                            m_nibbleIdx = m_size_B * 2;
                            return false;
                        }
                        bits <<= 4;
                        bits |= nibble();
                    }
                    sub(bits);
                    set();
                    return true;
                }
            }
            break;
        case TagV::VarFloat0: {
            const double P = 2.0 * m_prevFloat - m_prev2Float;
            m_prev2Float = m_prevFloat;
            m_prevFloat = P;
            value.m_variant = Value::Variant::Float;
            value.m_union.f64 = m_prevFloat;
            return true;
        }
        case TagV::VarFloat1: {
            uint64_t bits = 0;
            uint8_t count3b = 0;
            while (m_nibbleIdx / 2 < m_size_B) {
                const uint8_t nbl = nibble();
                if ((nbl & 0x8) != 0) {
                    ++count3b;
                    bits <<= 3;
                    bits |= nbl & 0x7;
                }
                else {
                    ++count3b;
                    bits <<= 3;
                    bits |= nbl & 0x7;
                    uint8_t count4b = 0;
                    switch (count3b) {
                    case 1: count4b = 1; break;
                    case 2: count4b = 1; break;
                    case 3: count4b = 2; break;
                    case 4: count4b = 2; break;
                    case 5: count4b = 3; break;
                    case 6: count4b = 4; break;
                    case 7: count4b = 5; break;
                    case 8: count4b = 7; break;
                    case 9: count4b = 9; break;
                    case 10: count4b = 9; break;
                    default:
                        assert(false);
                        m_nibbleIdx = m_size_B * 2;
                        return false;
                    }
                    for (uint8_t i = 0; i < count4b; ++i) {
                        if (m_size_B <= m_nibbleIdx / 2) {
                            m_nibbleIdx = m_size_B * 2;
                            return false;
                        }
                        bits <<= 4;
                        bits |= nibble();
                    }
                    uint32_t trail = 0; {
                        if (m_size_B <= m_nibbleIdx / 2) {
                            m_nibbleIdx = m_size_B * 2;
                            return false;
                        }
                        const uint8_t t = nibble();
                        if (t == 0xF) {
                            if (m_size_B <= m_nibbleIdx / 2) {
                                m_nibbleIdx = m_size_B * 2;
                                return false;
                            }
                            const uint8_t hi = nibble();
                            if (m_size_B <= m_nibbleIdx / 2) {
                                m_nibbleIdx = m_size_B * 2;
                                return false;
                            }
                            const uint8_t lo = nibble();
                            trail = uint32_t(hi) * 16 + uint32_t(lo);
                        }
                        else {
                            trail = m_prevTrailZeros + uint32_t(t) - 7;
                        }
                    }
                    const uint64_t x = bits << trail;
                    const double P = 2.0 * m_prevFloat - m_prev2Float;
                    const double y = bit_cast<double>(bit_cast<uint64_t>(P) ^ x);
                    m_prev2Float = m_prevFloat;
                    m_prevFloat = y;
                    m_prevTrailZeros = trail;
                    value.m_variant = Value::Variant::Float;
                    value.m_union.f64 = m_prevFloat;
                    return true;
                }
            }
            break;
        }
        case TagV::Tag:
            return nextTag(value);
        case TagV::End:
            return false;
        default:
            assert(false);
            return false;
        }
    }
    return false;
}

Array::Array(Writer* writer) : m_writer(writer) {}

Array& Array::value(int32_t number) {
    m_writer->value(int64_t(number));
    return *this;
}
Array& Array::value(uint32_t number) {
    m_writer->value(uint64_t(number));
    return *this;
}
Array& Array::value(int64_t number) {
    m_writer->value(number);
    return *this;
}
Array& Array::value(uint64_t number) {
    m_writer->value(number);
    return *this;
}
Array& Array::value(float number) {
    m_writer->value(number);
    return *this;
}
Array& Array::value(double number) {
    m_writer->value(number);
    return *this;
}

Array& Array::value(std::nullptr_t) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Null));
    return *this;
}

Array& Array::value(std::string_view string) {
    m_writer->value(string);
    return *this;
}

Array& Array::bytes(const void* data, uint32_t size_B) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Bytes));
    m_writer->valueInt(size_B, false, m_writer->m_prevSize, m_writer->m_prev2Size);
    m_writer->allign();
    m_writer->write_raw(data, size_B);
    return *this;
}

Array& Array::array(const std::function<void(Array)>& handler) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Array));
    if (handler) {
        handler(Array(m_writer));
    }
    m_writer->nibble(uint8_t(TagV::End));
    return *this;
}

Array& Array::map(const std::function<void(Map)>& handler) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Map));
    if (handler) {
        handler(Map(m_writer));
    }
    m_writer->nibble(uint8_t(TagV::End));
    return *this;
}

MapValue::MapValue(Writer* writer, Map* map) : m_writer(writer), m_map(map) {}

Map& MapValue::value(int32_t number) {
    m_writer->value(int64_t(number));
    return *m_map;
}

Map& MapValue::value(uint32_t number) {
    m_writer->value(uint64_t(number));
    return *m_map;
}

Map& MapValue::value(int64_t number) {
    m_writer->value(number);
    return *m_map;
}

Map& MapValue::value(uint64_t number) {
    m_writer->value(number);
    return *m_map;
}

Map& MapValue::value(float number) {
    m_writer->value(number);
    return *m_map;
}

Map& MapValue::value(double number) {
    m_writer->value(number);
    return *m_map;
}

Map& MapValue::value(std::nullptr_t) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Null));
    return *m_map;
}

Map& MapValue::value(std::string_view string) {
    m_writer->value(string);
    return *m_map;
}

Map& MapValue::bytes(const void* data, uint32_t size_B) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Bytes));
    m_writer->valueInt(size_B, false, m_writer->m_prevSize, m_writer->m_prev2Size);
    m_writer->allign();
    m_writer->write_raw(data, size_B);
    return *m_map;
}

Map& MapValue::array(const std::function<void(Array)>& handler) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Array));
    if (handler) {
        handler(Array(m_writer));
    }
    m_writer->nibble(uint8_t(TagV::End));
    return *m_map;
}

Map& MapValue::map(const std::function<void(Map)>& handler) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Map));
    if (handler) {
        handler(Map(m_writer));
    }
    m_writer->nibble(uint8_t(TagV::End));
    return *m_map;
}

Map::Map(Writer* writer) : m_writer(writer), m_mapValue(writer, this) {}

MapValue& Map::key(int32_t number) {
    m_writer->value(int64_t(number));
    return m_mapValue;
}

MapValue& Map::key(uint32_t number) {
    m_writer->value(uint64_t(number));
    return m_mapValue;
}

MapValue& Map::key(int64_t number) {
    m_writer->value(number);
    return m_mapValue;
}

MapValue& Map::key(uint64_t number) {
    m_writer->value(number);
    return m_mapValue;
}

MapValue& Map::key(std::string_view string) {
    m_writer->value(string);
    return m_mapValue;
}

MapValue& Map::key(const void* data, uint32_t size_B) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Bytes));
    m_writer->value(uint64_t(size_B));
    m_writer->allign();
    m_writer->write_raw(data, size_B);
    return m_mapValue;
}

MapValue& Map::key(const std::function<void(Array)>& handler) {
    m_writer->nibble(uint8_t(TagV::Tag));
    m_writer->nibble(uint8_t(Tag::Array));
    if (handler) {
        handler(Array(m_writer));
    }
    m_writer->nibble(uint8_t(TagV::End));
    return m_mapValue;
}

void Writer::reset() {
    buffer.clear();
    m_isHighNibbleEmpty = true;
    m_prevInt = {};
    m_prev2Int = {};
    m_prevSize = {};
    m_prev2Size = {};
    m_prevFloat = 0.0;
    m_prev2Float = 0.0;
    m_prevTrailZeros = 0;
# if defined(NIBBLE_PACK_STATISTICS)
    idist.fill(0);
    fdist.fill(0);
# endif
}

void Writer::tolerance_prc(float prc) {
    tolerance = double(prc) * 0.01;
}
float Writer::tolerance_prc() const {
    return float(tolerance * 100.0);
}

void Writer::array(const std::function<void(Array)>& handler) {
    nibble(uint8_t(Tag::Array));
    if (handler) {
        handler(Array(this));
    }
    nibble(uint8_t(Tag::End));
    if (not m_isHighNibbleEmpty) {
        nibble(uint8_t(Tag::End));
    }
}

void Writer::map(const std::function<void(Map)>& handler) {
    nibble(uint8_t(Tag::Map));
    if (handler) {
        handler(Map(this));
    }
    nibble(uint8_t(Tag::End));
    if (not m_isHighNibbleEmpty) {
        nibble(uint8_t(Tag::End));
    }
}

void Writer::nibble(uint8_t nibble) {
    //nibble &= 0x0F;
    if (m_isHighNibbleEmpty) {
        buffer.push_back(uint8_t(nibble << 4)); // | uint8_t(Tag::End));
        m_isHighNibbleEmpty = false;
    }
    else {
        buffer.back() = uint8_t(buffer.back() | nibble);
        m_isHighNibbleEmpty = true;
    }
}
//void Writer::allign() {
//    if (m_isHighNibbleEmpty) {
//        return;
//    }
//    buffer.back() = uint8_t(buffer.back() | nibble);
//    m_isHighNibbleEmpty = true;
//}
void Writer::allign() {
    if (not m_isHighNibbleEmpty) {
        nibble(0);
    }
}

void Writer::write_raw(const void* data, size_t size_B) {
    assert(m_isHighNibbleEmpty);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    buffer.insert(buffer.end(), bytes, bytes + size_B);
}

//void Writer::value(int32_t number) {
//}
//void Writer::value(uint32_t number) {
//}
void Writer::value(int64_t number) {
    if (0 <= number) {
        value(uint64_t(number));
        return;
    }
    valueInt(uint64_t(0) - uint64_t(number), true, m_prevInt, m_prev2Int);
}

void Writer::value(uint64_t number) {
    valueInt(number, false, m_prevInt, m_prev2Int);
}

void Writer::valueInt(uint64_t magnitude, bool negative, Int65& prev, Int65& prev2) {
    // Second-order predictor: base = 2*p1 - p2 over sign-magnitude pairs;
    // any overflow on the way forces the absolute encoding.
    bool deltaPossible = prev.mag <= UINT64_MAX / 2;
    Int65 base;
    if (deltaPossible) {
        const Int65 p1x2{prev.mag * 2, prev.neg};
        deltaPossible = p1x2.sub(prev2, base);
    }
    Int65 delta;
    if (deltaPossible) {
        deltaPossible = Int65{magnitude, negative}.sub(base, delta);
    }

    const auto numberVariant = matchInt(magnitude, negative);
    const uint32_t numberNibbles = nibblesCount(numberVariant) + 2;

    VarInt variant = VarInt::pInt64;
    uint32_t deltaNibbles = UINT32_MAX;
    if (deltaPossible) {
        variant = matchInt(delta.mag, delta.neg);
        deltaNibbles = nibblesCount(variant);
    }
    const bool absolute = not deltaPossible or numberNibbles < deltaNibbles;
    // Constant reset tokens: 2 nibbles for zero, 3 for the extremes. They
    // are used only when cheaper than the chosen delta/absolute encoding.
    const uint32_t chosenNibbles = absolute ? numberNibbles : deltaNibbles;
    if (magnitude == 0) {
        if (2 < chosenNibbles) {
            nibble(uint8_t(TagV::Tag));
            nibble(uint8_t(Tag::ResetToIntZero));
            prev2 = prev;
            prev = {};
            return;
        }
    }
    else {
        int8_t tagr = -1;
        if (negative) {
            if (magnitude == uint64_t(1) << 31) {
                tagr = int8_t(TagR::ResetToInt32Min);
            }
            else if (magnitude == uint64_t(1) << 63) {
                tagr = int8_t(TagR::ResetToInt64Min);
            }
        }
        else {
            if (magnitude == (uint64_t(1) << 31) - 1) {
                tagr = int8_t(TagR::ResetToInt32Max);
            }
            else if (magnitude == (uint64_t(1) << 32) - 1) {
                tagr = int8_t(TagR::ResetToUInt32Max);
            }
            else if (magnitude == (uint64_t(1) << 63) - 1) {
                tagr = int8_t(TagR::ResetToInt64Max);
            }
            else if (magnitude == UINT64_MAX) {
                tagr = int8_t(TagR::ResetToUInt64Max);
            }
        }
        if (tagr >= 0 and 3 < chosenNibbles) {
            nibble(uint8_t(TagV::Tag));
            nibble(uint8_t(Tag::ResetTo));
            nibble(uint8_t(tagr));
            prev2 = Int65{magnitude, negative};
            prev = Int65{magnitude, negative};
            return;
        }
    }
    if (absolute) {
        variant = numberVariant;
        nibble(uint8_t(TagV::Tag));
        nibble(uint8_t(Tag::ResetToValue));
    }
# if defined(NIBBLE_PACK_STATISTICS)
    const size_t raw = size_t(variant);
    ++idist[raw < size_t(VarInt::p4_19) ? 0
        : raw <= size_t(VarInt::m20_5) ? 1
        : (raw - size_t(VarInt::pInt7)) / 2 + 2];
# endif
    // After a delta, the chain shifts by one (p2 becomes the old p1).
    // After an absolute, both chain values become the value itself,
    // so the next delta degenerates to the first difference.
    const Int65 val{magnitude, negative};
    prev2 = absolute ? val : prev;
    prev = val;
    const Int65 payload = absolute ? val : delta;
    writeInt(variant, payload.neg, payload.mag);
}
void Writer::writeInt(VarInt variant, bool negative, uint64_t bits) {
    uint8_t count3b = 0;
    uint8_t count4b = 0;
    switch (variant) {
    case VarInt::p0:      nibble(uint8_t(TagV::Int_p0)); return;
    case VarInt::p1:      nibble(uint8_t(TagV::Int_p1)); return;
    case VarInt::p2:      nibble(uint8_t(TagV::Int_p2)); return;
    case VarInt::p3:      nibble(uint8_t(TagV::Int_p3)); return;
    case VarInt::m4:      nibble(uint8_t(TagV::Int_m4)); return;
    case VarInt::m3:      nibble(uint8_t(TagV::Int_m3)); return;
    case VarInt::m2:      nibble(uint8_t(TagV::Int_m2)); return;
    case VarInt::m1:      nibble(uint8_t(TagV::Int_m1)); return;
    case VarInt::p4_19: {
        assert(c::int5fop <= bits and bits <= c::int5max);
        const uint8_t b = uint8_t(bits - c::int5fop);
        nibble(uint8_t(TagV::VarIntP0) | ((b >> 3) & 0x3));
        nibble(b & 0x7);
        return;
    }
    case VarInt::m20_5: {
        assert(uint64_t(-c::int5fom) <= bits and bits <= uint64_t(-c::int5min));
        const uint8_t b = uint8_t(c::int5nop + c::int5fom + bits);
        nibble(uint8_t(TagV::VarIntP0) | ((b >> 3) & 0x3));
        nibble(b & 0x7);
        return;
    }
    case VarInt::pInt7:
    case VarInt::mInt7:   count3b = 2; break;
    case VarInt::pInt10:
    case VarInt::mInt10:  count3b = 3; break;
    case VarInt::pInt17:
    case VarInt::mInt17:  count3b = 4; count4b = 1; break;
    case VarInt::pInt27:
    case VarInt::mInt27:  count3b = 6; count4b = 2; break;
    case VarInt::pInt38:
    case VarInt::mInt38:  count3b = 7; count4b = 4; break;
    case VarInt::pInt49:
    case VarInt::mInt49:  count3b = 8; count4b = 6; break;
    case VarInt::pInt64:
    case VarInt::mInt64:  count3b = 5; count4b = 12; break;
    default:              assert(false); return;
    }
    const uint8_t highest = uint8_t((bits >> (count3b * 3 + count4b * 4)) & 0x1);
    nibble(uint8_t(negative ? TagV::VarIntM0 : TagV::VarIntP0) | highest);
    for (uint8_t i = 0; i < count3b; ++i) {
        const uint8_t shift = (count3b - i - 1) * 3 + count4b * 4;
        const uint8_t var = i < (count3b - 1) ? 0x8 : 0x0;
        nibble(var | uint8_t((bits >> shift) & 0x7));
    }
    for (uint8_t i = 0; i < count4b; ++i) {
        const uint8_t shift = (count4b - i - 1) * 4;
        nibble(uint8_t((bits >> shift) & 0xF));
    }
}
//void Writer::value(float number) {
//}
void Writer::value(double number) {
    if (std::isnan(number)) {
        nibble(uint8_t(TagV::Tag));
        nibble(uint8_t(Tag::NaN));
        return;
    }
    if (std::isinf(number)) {
        nibble(uint8_t(TagV::Tag));
        nibble(uint8_t(number > 0.0 ? Tag::pInf : Tag::mInf));
        return;
    }
    // Optional rounding to the nearest value of the k-significant-bit
    // grid, with k chosen so the grid guarantees |q - y| <= tolerance*|y|.
    double q = number;
    if (tolerance > 0.0) {
        // k = ceil(-log2(tolerance)): the k-significant-bit grid rounds
        // with a relative error of at most 2^-k <= tolerance; falls out
        // of the exponent field as k = 1023 - exp (exp = 1023 + ilogb).
        const uint64_t tb = bit_cast<uint64_t>(tolerance);
        const int32_t exp = int32_t((tb >> 52) & 0x7FF);
        int32_t k;
        if (exp == 0x7FF) {
            k = (tb >> 63) ? 53 : 1;
        }
        else if (tb >> 63) {
            k = 53;
        }
        else {
            const int32_t n = 1023 - exp; // -ilogb(tolerance)
            k = n < 1 ? 1 : (n > 52 ? 53 : n);
        }
        const uint32_t shift = uint32_t(53 - k);
        if (shift != 0) {
            const uint64_t half = uint64_t(1) << (shift - 1);
            q = bit_cast<double>(((bit_cast<uint64_t>(number) + half) >> shift) << shift);
        }
    }
    const double P = 2.0 * m_prevFloat - m_prev2Float;
    const uint64_t x = bit_cast<uint64_t>(q) ^ bit_cast<uint64_t>(P);
# if defined(NIBBLE_PACK_STATISTICS)
    ++fdist[x == 0 ? 0 : 1];
# endif
    if (x == 0) {
        nibble(uint8_t(TagV::VarFloat0));
        m_prev2Float = m_prevFloat;
        m_prevFloat = P;
        return;
    }
    if (bit_cast<uint64_t>(q) == 0) {
        nibble(uint8_t(TagV::Tag));
        nibble(uint8_t(Tag::ResetToFloatZero));
        m_prev2Float = m_prevFloat;
        m_prevFloat = 0.0;
        return;
    }
    const uint32_t lead = uint32_t(countl_zero(x));
    const uint32_t trail = uint32_t(countr_zero(x));
    const uint32_t mlen = 64 - lead - trail;
    uint8_t count3b = 0;
    uint8_t count4b = 0;
    if (mlen <= 27) {
        if (mlen <= 10) {
            if (mlen <= 7)   { count3b = 1;  count4b = 1; }
            else             { count3b = 2;  count4b = 1; }
        }
        else if (mlen <= 20) {
            if (mlen <= 17)  { count3b = 3;  count4b = 2; }
            else             { count3b = 4;  count4b = 2; }
        }
        else                 { count3b = 5;  count4b = 3; }
    }
    else {
        if (mlen <= 41) {
            if (mlen <= 34)  { count3b = 6;  count4b = 4; }
            else             { count3b = 7;  count4b = 5; }
        }
        else if (mlen <= 52) { count3b = 8;  count4b = 7; }
        else if (mlen <= 63) { count3b = 9;  count4b = 9; }
        else                 { count3b = 10; count4b = 9; }
    }
    const uint64_t v = x >> trail;
    nibble(uint8_t(TagV::VarFloat1));
    for (uint8_t i = 0; i < count3b; ++i) {
        const uint8_t shift = uint8_t((count3b - i - 1) * 3 + count4b * 4);
        const uint8_t var = i < (count3b - 1) ? 0x8 : 0x0;
        nibble(var | uint8_t((v >> shift) & 0x7));
    }
    for (uint8_t i = 0; i < count4b; ++i) {
        const uint8_t shift = uint8_t((count4b - i - 1) * 4);
        nibble(uint8_t((v >> shift) & 0xF));
    }
    const int32_t d = int32_t(trail) - int32_t(m_prevTrailZeros);
    if (-7 <= d and d <= 7) {
        nibble(uint8_t(d + 7));
    }
    else {
        nibble(0xF);
        nibble(uint8_t((trail >> 4) & 0xF));
        nibble(uint8_t(trail & 0xF));
    }
    m_prev2Float = m_prevFloat;
    m_prevFloat = q;
    m_prevTrailZeros = trail;
}

void Writer::value(std::string_view string) {
    nibble(uint8_t(TagV::Tag));
    nibble(uint8_t(Tag::String));
    allign();
    const char nul = '\0';
    const size_t found = string.find(nul);
    const size_t size_B = found == std::string_view::npos ? string.size() : found;
    if (size_B != 0) {
        write_raw(string.data(), size_B);
    }
    write_raw(&nul, 1);
}

void Writer::value_bool(const bool boolean) {
    nibble(uint8_t(TagV::Tag));
    nibble(uint8_t(boolean ? Tag::True : Tag::False));
}

Writer::VarInt Writer::matchInt(uint64_t magnitude, bool negative) {
    if (negative) {
        assert(magnitude != 0);
        // -4 .. -1
        if (magnitude <= -c::int3min) {
            static_assert(int32_t(VarInt::m1) - int32_t(VarInt::m4) == 4 - 1, "");
            return VarInt(uint8_t(VarInt::m1) - (uint8_t(magnitude) - 1));
        }
        // -20 .. -5
        if (magnitude <= -c::int5min) {
            return VarInt::m20_5;
        }
    }
    else {
        // 0 .. 3
        if (magnitude <= c::int3max) {
            static_assert(int32_t(VarInt::p3) - int32_t(VarInt::p0) == 3 - 0, "");
            return VarInt(uint8_t(VarInt::p0) + uint8_t(magnitude));
        }
        // 4 .. 19
        if (magnitude <= c::int5max) {
            return VarInt::p4_19;
        }
    }

    const uint32_t count = 64 - countl_zero(magnitude);
    if (count <= 27) {
        if (count <= 10) {
            if (count <= 7) {
                return negative ? VarInt::mInt7 : VarInt::pInt7;
            }
            else {
                return negative ? VarInt::mInt10 : VarInt::pInt10;
            }
        }
        else {
            if (count <= 17) {
                return negative ? VarInt::mInt17 : VarInt::pInt17;
            }
            else {
                return negative ? VarInt::mInt27 : VarInt::pInt27;
            }
        }
    }
    else {
        if (count <= 38) {
            return negative ? VarInt::mInt38 : VarInt::pInt38;
        }
        else if (count <= 49) {
            return negative ? VarInt::mInt49 : VarInt::pInt49;
        }
        else {
            return negative ? VarInt::mInt64 : VarInt::pInt64;
        }
    }
}
uint32_t Writer::nibblesCount(VarInt variant) {
    switch (variant) {
    case VarInt::p0: case VarInt::p1: case VarInt::p2: case VarInt::p3:
    case VarInt::m4: case VarInt::m3: case VarInt::m2: case VarInt::m1:
        return 1;
    case VarInt::m20_5: case VarInt::p4_19:
        return 2;
    case VarInt::pInt7: case VarInt::mInt7:
        return 3;
    case VarInt::pInt10: case VarInt::mInt10:
        return 4;
    case VarInt::pInt17: case VarInt::mInt17:
        return 6;
    case VarInt::pInt27: case VarInt::mInt27:
        return 9;
    case VarInt::pInt38: case VarInt::mInt38:
        return 12;
    case VarInt::pInt49: case VarInt::mInt49:
        return 15;
    case VarInt::pInt64: case VarInt::mInt64:
        return 18;
    default:
        assert(false);
        return 0;
    }
}

} // namespace Niblp
