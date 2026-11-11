#include "NibblePack.hpp"
#include "fp_t.hpp"
using Niblp::fp_t;

#include <iostream>
#include <fstream>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#if defined(NIBBLE_PACK_CPP_LIB_STRING_VIEW)
#   include <string_view>
#endif
#include <vector>

#if defined(_MSC_VER)
#   include <crtdbg.h>
#endif

#if not defined(NIBLP_TEST_NO_MSGPACK)
#   include "cpp_msgpack_without_dom.hpp" // C++17+
#endif


namespace {

// input: 0..1
// output: _/`\_ 0..1
inline float getSin(const float x) {
    constexpr float k = 360.0f * (3.1415927f / 180.0f);
    return std::sin(x * k) * 0.5f + 0.5f;
}
inline double getSin(const double x) {
    constexpr double k = 360.0 * (3.1415926535897932 / 180.0);
    return std::sin(x * k) * 0.5 + 0.5;
}
// input: 0..1
// output: /\ 0..1
inline float getTri(const float x) {
    if (x < 0.5f) {
        return x * 2.0f;
    }
    else {
        return (1.0f - x) * 2.0f;
    }
}
inline double getTri(const double x) {
    //return 2.0 * (0.5 - std::abs(x - 0.5));
    if (x < 0.5) {
        return x * 2.0;
    }
    else {
        return (1.0 - x) * 2.0;
    }
}
// output: noise 0..1
inline double getRand(uint32_t x) {
    x ^= x >> 16;
    x *= UINT32_C(0x7FEB352D);
    x ^= x >> 15;
    x *= UINT32_C(0x846CA68B);
    x ^= x >> 16;
    constexpr double k = 1.0 / double(UINT32_MAX);
    return double(x) * k;
}

} // namespace

//void test_varint() {
//    constexpr uint64_t bits = static_cast<uint64_t>(1);
//    constexpr uint64_t sign = 0ULL - (bits >> 63);
//    constexpr int64_t n3 = static_cast<int64_t>((bits << 1) ^ sign);
//
//    constexpr int64_t n1 = 1;
//    constexpr uint64_t z1 = ((uint64_t(n1) << 1) ^ (uint64_t(n1) >> 63));
//
//    constexpr uint64_t z2 = 0b111'1110;
//    constexpr int64_t n2 = (int64_t((z2 >> 1) ^ -int64_t(z2 & 1)));
//}

void test_varfloat() {
    // Finite (NibblePack varfloat) mode: no NaNs or Infs, but subnormals,
    // signed zero and every exponent field that fits in a double are valid.
    {
        fp_t<3, 3, true> f;
        assert(f.try_from(1.5));
        assert(static_cast<double>(f) == 1.5);
        assert(f.sign() == 1);
        assert(f.isFinite());
        assert(f.try_from(1.5f));
        assert(static_cast<float>(f) == 1.5f);
        assert(!f.try_from(0.01f));

        assert(static_cast<float>(fp_t<3, 3>(1.5f)) == 1.5f);
        assert(static_cast<double>(fp_t<3, 3>(2)) == 2.0);

        assert(f.try_from(0.0));
        assert(f.bits == 0);
        assert(f.isZero());
        assert(f.sign() == 0);

        assert(f.try_from(-0.0));
        assert((f.bits == fp_t<3, 3>::sign_mask));
        assert(f.isZero());
        assert(f.sign() == -1);
        assert(std::signbit(static_cast<double>(f)));

        // Exponent field 0 is not fully reserved: non-zero mantissas are
        // subnormals (the smallest step is 2^-5).
        assert(f.try_from(0.03125));
        assert(f.bits == 1);
        assert(static_cast<double>(f) == 0.03125);
        assert(f.try_from(0.21875));
        assert(f.bits == 7);
        assert(static_cast<double>(f) == 0.21875);
        // Underflow to zero is rejected for a non-zero value.
        assert(!f.try_from(0.01));

        // The max exponent field is used too: E3M3 covers 16.0 .. 30.0.
        assert(f.try_from(16.0));
        assert(static_cast<double>(f) == 16.0);
        assert(f.try_from(30.0));
        assert(f.bits == 0x3F);
        assert(static_cast<double>(f) == 30.0);
        assert(!f.try_from(31.0));
        // 15.5 is exactly between 15 and 16: ties-to-even gives 16.
        assert(f.try_from(15.5));
        assert(static_cast<double>(f) == 16.0);

        assert(!f.try_from(
            std::numeric_limits<double>::quiet_NaN()));
        assert(!f.try_from(
            std::numeric_limits<double>::infinity()));
        assert(!f.isNaN());
        assert(!f.isInf());
    }

    // E7M20 and E11M52 accuracy.
    {
        fp_t<7, 20, true> f20;
        assert(f20.try_from(0.01));
        assert(std::abs(static_cast<double>(f20) - 0.01) <= 0.0001 * 0.01);

        fp_t<11, 52, true> f64;
        assert(f64.try_from(1.1));
        assert(static_cast<double>(f64) == 1.1);
        assert(f64.try_from(
            std::numeric_limits<double>::max()));
        assert(static_cast<double>(f64) == std::numeric_limits<double>::max());

        // E8M30 has enough range and precision to hold every float exactly,
        // including float subnormals.
        fp_t<8, 30, true> f30;
        assert(f30.try_from(
            std::numeric_limits<float>::denorm_min()));
        assert(static_cast<float>(f30) ==
            std::numeric_limits<float>::denorm_min());

        // Values above FLT_MAX convert to float infinity.
        assert(f64.try_from(1e300));
        assert(std::isinf(static_cast<float>(f64)));

        // E11M52 is the only NibblePack class whose max exponent field would
        // be out of the double range (2^1024), so only that field is invalid.
        assert((!fp_t<11, 52, true>::is_valid_bits(fp_t<11, 52, true>::exp_mask)));
        fp_t<11, 52, true> invalid;
        invalid.bits = fp_t<11, 52, true>::exp_mask;
        assert(!invalid.is_valid());
        assert(std::isnan(static_cast<double>(invalid)));
    }

    // IEEE-754-like mode.
    using f16 = fp_t<5, 10, false>;
    using f32 = fp_t<8, 23, false>;
    using f64 = fp_t<11, 52, false>;
    static_assert(sizeof(f16) == sizeof(uint16_t), "");
    static_assert(sizeof(f32) == sizeof(uint32_t), "");
    static_assert(sizeof(f64) == sizeof(uint64_t), "");

    {
        f16 f;
        assert(f.try_from(1.5));
        assert(static_cast<double>(f) == 1.5);
        assert(f.bits == 0x3E00);
        assert(f.try_from(1.5f));
        assert(static_cast<float>(f) == 1.5f);

        // -0.0 keeps its sign in this mode.
        assert(f.try_from(-0.0));
        assert(f.bits == f16::sign_mask);
        assert(f.isZero());
        assert(f.sign() == -1);
        assert(std::signbit(static_cast<double>(f)));

        // Smallest subnormal and smallest normal.
        assert(f.try_from(std::ldexp(1.0, -24)));
        assert(f.bits == 1);
        assert(static_cast<double>(f) == std::ldexp(1.0, -24));
        assert(f.try_from(std::ldexp(1.0, -14)));
        assert(f.bits == (uint16_t(1) << 10));
        assert(static_cast<double>(f) == std::ldexp(1.0, -14));

        // Max finite is 65504; 65520 rounds to infinity.
        assert(f.try_from(65504.0));
        assert(f.bits == 0x7BFF);
        assert(static_cast<double>(f) == 65504.0);
        assert(f.try_from(65520.0));
        assert(f.isInf());
        assert(!f.isNaN());
        assert(!f.isFinite());
        assert(static_cast<double>(f) ==
            std::numeric_limits<double>::infinity());

        // NaN and +/-Inf are first-class values.
        assert(f.try_from(
            std::numeric_limits<double>::quiet_NaN()));
        assert(f.isNaN());
        assert(!f.isInf());
        assert(!f.isFinite());
        assert(std::isnan(static_cast<double>(f)));

        assert(f.try_from(
            -std::numeric_limits<double>::infinity()));
        assert(f.isInf());
        assert(f.sign() == -1);
        assert(static_cast<double>(f) ==
            -std::numeric_limits<double>::infinity());

        assert(f.try_from(
            std::numeric_limits<float>::quiet_NaN()));
        assert(f.isNaN());
        assert(std::isnan(static_cast<float>(f)));

        assert(f.try_from(
            -std::numeric_limits<float>::infinity()));
        assert(f.isInf());
        assert(f.sign() == -1);
        assert(static_cast<float>(f) ==
            -std::numeric_limits<float>::infinity());
    }

    // binary32 compatibility: every float round-trips exactly.
    {
        const float values[] = {
            0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 1.1f,
            std::numeric_limits<float>::min(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::denorm_min(),
            std::numeric_limits<float>::epsilon(),
        };
        for (const float value : values) {
            f32 f;
            assert(f.try_from(value));
            assert(static_cast<float>(f) == value);
            assert(std::signbit(static_cast<float>(f)) == std::signbit(value));
        }
    }

    // binary64 compatibility: every finite double round-trips exactly.
    {
        const double values[] = {
            0.0, -0.0, 1.0, -1.0, 0.1, 1.1,
            std::numeric_limits<double>::min(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::denorm_min(),
            std::numeric_limits<double>::epsilon(),
        };
        for (const double value : values) {
            f64 f;
            assert(f.try_from(value));
            assert(static_cast<double>(f) == value);
            assert(std::signbit(static_cast<double>(f)) == std::signbit(value));
        }

        f64 f;
        assert(f.try_from(1.1f));
        assert(static_cast<float>(f) == 1.1f);
    }
}

void test_NibblePack() {
    Niblp::Writer writer;
    Niblp::Reader reader;

    // []
    writer.array(nullptr);
    assert(writer.buffer.size() == 1);
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        bool any = false;
        value.array([&](uint32_t, const Niblp::Value&) {
            any = true;
        });
        assert(not any);
    });

    // {}
    writer.reset();
    writer.map(nullptr);
    assert(writer.buffer.size() == 1);
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_map());
        bool any = false;
        value.map([&](const Niblp::Value&, const Niblp::Value&) {
            any = true;
        });
        assert(not any);
    });

    // [ 1, -1, 0 ]
    writer.reset();
    writer.array([&](Niblp::Array writer) {
        writer.value(1).value(-1).value(0);
    });
    assert(writer.buffer.size() == 3);
    assert(writer.buffer == std::vector<uint8_t>({ 0x11, 0x53, 0xFF }));
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        value.array([&](uint32_t idx, const Niblp::Value& value) {
            switch (idx) {
            case 0: assert(value.is_int()); assert(value.as_int() == 1); break;
            case 1: assert(value.is_int()); assert(value.as_int() == -1); break;
            case 2: assert(value.is_int()); assert(value.as_int() == 0); break;
            default: assert(false); break;
            }
        });
    });

    // Constant reset tokens: zero and the six extremes round-trip exactly.
    // A cold chain makes the token cheaper than the absolute encoding; the
    // mixed tail exercises the delta-cheap and generic-reset paths after them.
    {
        const int64_t vals[] = {
            INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX,
            12345, -98765, 0, INT64_MIN, 7, 0,
        };
        writer.reset();
        writer.array([&](Niblp::Array array) {
            for (int64_t v : vals) {
                array.value(v);
            }
        });
        reader.assign(writer.buffer.data(), writer.buffer.size());
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t idx, const Niblp::Value& item) {
                assert(item.is_int());
                assert(item.as_int() == vals[idx]);
            });
        });
    }
    {
        const uint64_t vals[] = {
            UINT32_MAX, UINT64_MAX, uint64_t(INT64_MAX) + 1,
            42, 0, UINT64_MAX, UINT32_MAX,
        };
        writer.reset();
        writer.array([&](Niblp::Array array) {
            for (uint64_t v : vals) {
                array.value(v);
            }
        });
        reader.assign(writer.buffer.data(), writer.buffer.size());
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t idx, const Niblp::Value& item) {
                assert(item.is_uint());
                assert(item.as_uint() == vals[idx]);
            });
        });
    }

    // [ 1.0, -1.0, 0.0 ]
    writer.reset();
    writer.array([&](Niblp::Array writer) {
        writer.value(1.0f).value(-1.0).value(0.0f);
    });
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        value.array([&](uint32_t idx, const Niblp::Value& value) {
            switch (idx) {
            case 0: assert(value.is_float()); assert(value.as_float64() == 1.0f); break;
            case 1: assert(value.is_float()); assert(value.as_float32() == -1.0f); break;
            case 2: assert(value.is_float()); assert(value.as_float64() == 0.0); break;
            default: assert(false); break;
            }
        });
    });

    // [ 1.0, +Inf, 2.0, NaN, 3.0, -Inf, 0.0, 4.0 ]
    writer.reset();
    writer.array([&](Niblp::Array writer) {
        writer
        .value(1.0)
        .value(std::numeric_limits<double>::infinity())
        .value(2.0)
        .value(std::numeric_limits<double>::quiet_NaN())
        .value(3.0)
        .value(-std::numeric_limits<double>::infinity())
        .value(0.0)
        .value(4.0);
    });
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        value.array([&](uint32_t idx, const Niblp::Value& value) {
            assert(value.is_float());
            switch (idx) {
            case 0: assert(value.as_float64() == 1.0); break;
            case 1:
                assert(std::isinf(value.as_float64()));
                assert(value.as_float64() > 0.0);
                break;
            case 2: assert(value.as_float64() == 2.0); break;
            case 3: assert(std::isnan(value.as_float64())); break;
            case 4: assert(value.as_float64() == 3.0); break;
            case 5:
                assert(std::isinf(value.as_float64()));
                assert(value.as_float64() < 0.0);
                break;
            case 6: assert(value.as_float64() == 0.0); break;
            case 7: assert(value.as_float64() == 4.0); break;
            default: assert(false); break;
            }
        });
    });

    // tolerance is a percentage of the value, not a fraction of it.
    {
        Niblp::Writer exactWriter;
        exactWriter.tolerance = 0.0;
        exactWriter.array([&](Niblp::Array array) {
            array.value(1.1);
        });

        Niblp::Writer halfPercentWriter;
        halfPercentWriter.tolerance = 0.005; // 0.5%
        halfPercentWriter.array([&](Niblp::Array array) {
            array.value(1.1);
        });

        Niblp::Writer onePercentWriter;
        onePercentWriter.tolerance = 0.01; // 1%
        onePercentWriter.array([&](Niblp::Array array) {
            array.value(1.1);
        });

        // A looser tolerance guarantees a not-larger stream. It cannot
        // promise a strictly smaller one: coarser classes only merge grid
        // points, and a single value can land on a point both classes share
        // (1.1 rounds to 1.1015625 on both the 0.5% and the 1% grid).
        assert(onePercentWriter.buffer.size() <= halfPercentWriter.buffer.size());
        assert(halfPercentWriter.buffer.size() <= exactWriter.buffer.size());

        // The percent accessors agree with the fraction member.
        assert(exactWriter.tolerance_prc() == 0.0f);
        assert(halfPercentWriter.tolerance_prc() == 0.5f);
        assert(onePercentWriter.tolerance_prc() == 1.0f);

        // The decoded value stays within 1% of the original.
        Niblp::Reader percentReader;
        percentReader.assign(onePercentWriter.buffer.data(),
            onePercentWriter.buffer.size());
        percentReader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t, const Niblp::Value& item) {
                assert(item.is_float());
                assert(std::abs(item.as_float64() - 1.1) <= 1.0 * 1.1 / 100.0);
            });
        });
    }


    // Every float ladder class round-trips on the wire: the deltas of the
    // sequence are dyadic, so they are exact on every class grid and the
    // decoded values must match bit for bit. The scales walk the ladder:
    // E3M4, E3M11, E6M11, E7M13, E8M26, E9M32, E10M42, E11M52.
    {
        static const int ks[] = { 0, 4, 10, 32, 40, 60, 70, 130, 260, 520, 1000 };
        for (const int k : ks) {
            const double A = std::ldexp(1.0, k);
            writer.reset();
            writer.array([&](Niblp::Array array) {
                array.value(A).value(-A).value(A * 0.5)
                    .value(-A * 0.5).value(A).value(0.0);
            });
            reader.assign(writer.buffer.data(), writer.buffer.size());
            reader.parse([&](const Niblp::Value& value) {
                assert(value.is_array());
                value.array([&](uint32_t idx, const Niblp::Value& item) {
                    assert(item.is_float());
                    const double original = idx == 5 ? 0.0
                        : (idx % 2 == 0 ? 1.0 : -1.0)
                            * (idx == 2 || idx == 3 ? 0.5 : 1.0) * A;
                    assert(item.as_float64() == original);
                });
            });
        }
    }

    // int5 absolute tags: +6..+19 and -20..-3, plus int7/int10 deltas.
    writer.reset();
    writer.array([&](Niblp::Array array) {
        array.value(0).value(7).value(-7);
        array.value(6).value(19).value(20);
        array.value(-3).value(-20).value(-21);
        array.value(1000).value(1007).value(1014);
    });
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        static const int64_t expected[] = {
            0, 7, -7, 6, 19, 20, -3, -20, -21, 1000, 1007, 1014,
        };
        value.array([&](uint32_t idx, const Niblp::Value& item) {
            assert(idx < sizeof(expected) / sizeof(expected[0]));
            if (expected[idx] < 0) {
                assert(item.is_int() and not item.is_uint());
                assert(item.as_int() == expected[idx]);
            }
            else {
                assert(item.is_uint());
                assert(item.as_uint() == uint64_t(expected[idx]));
            }
        });
    });

    // The drift-budget contract: the reconstructed waveform stays within
    // tolerance of the full scale at every sample.
    {
        writer.reset();
        const double A = 1e4;
        double peak = 0.0;
        std::vector<double> ys;
        writer.array([&](Niblp::Array array) {
            for (int32_t x_ms = 0; x_ms < 10000; ++x_ms) {
                const double x = double(x_ms % 100) / 100.0;
                const double y = getSin(x) * A * 2.0 - A;
                if (std::abs(y) > peak) {
                    peak = std::abs(y);
                }
                ys.push_back(y);
                array.value(y);
            }
        });
        reader.assign(writer.buffer.data(), writer.buffer.size());
        double maxDrift = 0.0;
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t idx, const Niblp::Value& item) {
                assert(item.is_float());
                const double drift = std::abs(item.as_float64() - ys[idx]);
                if (drift > maxDrift) {
                    maxDrift = drift;
                }
            });
        });
        assert(maxDrift <= writer.tolerance * peak * (1.0 + 1e-9));
    }

    // Extreme integers: a downward jump that makes the absolute cheaper
    // than the delta (the payload must keep the value's own sign), unsigned
    // results above INT64_MAX, and the INT64_MIN base.
    writer.reset();
    writer.array([&](Niblp::Array array) {
        array.value(uint64_t(200000)).value(uint64_t(20));
        array.value(uint64_t(9223372036854775807ULL));
        array.value(uint64_t(18446744073709551615ULL));
        array.value(int64_t(-1)).value(uint64_t(9223372036854775813ULL));
        array.value(std::numeric_limits<int64_t>::min())
            .value(uint64_t(0))
            .value(std::numeric_limits<int64_t>::min());
    });
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        value.array([&](uint32_t idx, const Niblp::Value& value) {
            switch (idx) {
            case 0: assert(value.is_uint()); assert(value.as_uint() == 200000); break;
            case 1: assert(value.is_uint()); assert(value.as_uint() == 20); break;
            case 2: assert(value.is_uint()); assert(value.as_uint() == 9223372036854775807ULL); break;
            case 3: assert(value.is_uint()); assert(value.as_uint() == 18446744073709551615ULL); break;
            case 4: assert(value.is_int() and not value.is_uint()); assert(value.as_int() == -1); break;
            case 5: assert(value.is_uint()); assert(value.as_uint() == 9223372036854775813ULL); break;
            case 6: assert(value.is_int() and not value.is_uint()); assert(value.as_int() == std::numeric_limits<int64_t>::min()); break;
            case 7: assert(value.is_uint()); assert(value.as_uint() == 0); break;
            case 8: assert(value.is_int() and not value.is_uint()); assert(value.as_int() == std::numeric_limits<int64_t>::min()); break;
            default: assert(false); break;
            }
        });
    });

    // Every ladder class spends its x tag bit: the last value of a class
    // and the first value of the next one encode at the class's own cost.
    {
        static const uint64_t bounds[] = {
            511ULL, 512ULL, 1023ULL, 1024ULL,         // 2^9 - 1 .. 2^10
            134217727ULL, 134217728ULL,               // 2^27 - 1, 2^27
            274877906943ULL, 274877906944ULL,         // 2^38 - 1, 2^38
            562949953421311ULL, 562949953421312ULL,   // 2^49 - 1, 2^49
            1125899906842623ULL, 1125899906842624ULL, // 2^50 - 1, 2^50
        };
        // pInt10 covers up to 10 bits (nibble field 9 + the x tag bit);
        // 512..1023 must stay at 4 nibbles, not fall into pInt17.
        static const size_t sizes[] = { 3, 3, 3, 4, 6, 7, 7, 9, 9, 10, 10, 10 };
        for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i) {
            writer.reset();
            writer.array([&](Niblp::Array array) {
                array.value(bounds[i]);
            });
            assert(writer.buffer.size() == sizes[i]);
            reader.assign(writer.buffer.data(), writer.buffer.size());
            reader.parse([&](const Niblp::Value& value) {
                assert(value.is_array());
                value.array([&](uint32_t index, const Niblp::Value& value) {
                    assert(index == 0);
                    assert(value.is_uint());
                    assert(value.as_uint() == bounds[i]);
                });
            });
        }
    }

    // Null, booleans, string, bytes and nested containers.
    writer.reset();
    writer.array([&](Niblp::Array array) {
        array.value(nullptr);
        array.value(true).value(false);
        array.value(std::string_view("hi"));
        const uint8_t payload[3] = { 0x12, 0x34, 0x56 };
        array.bytes(payload, sizeof(payload));
        array.map([&](Niblp::Map map) {
            map.key(std::string_view("a")).value(1);
            map.key(2).value(std::string_view("b"));
        });
        array.array([&](Niblp::Array nested) {
            nested.value(7);
        });
    });
    reader.assign(writer.buffer.data(), writer.buffer.size());
    reader.parse([&](const Niblp::Value& value) {
        assert(value.is_array());
        value.array([&](uint32_t idx, const Niblp::Value& item) {
            switch (idx) {
            case 0: assert(item.is_null()); break;
            case 1: assert(item.is_bool()); assert(item.as_bool()); break;
            case 2: assert(item.is_bool()); assert(not item.as_bool()); break;
            case 3:
                assert(item.is_string());
                assert(item.as_string() == "hi");
                break;
            case 4:
                assert(item.is_bytes());
                assert(item.size_bytes_B() == 3);
                assert(item.as_bytes()[0] == 0x12);
                assert(item.as_bytes()[1] == 0x34);
                assert(item.as_bytes()[2] == 0x56);
                break;
            case 5:
                assert(item.is_map());
                item.map([&](const Niblp::Value& key,
                        const Niblp::Value& mapValue) {
                    if (key.is_string()) {
                        assert(key.as_string() == "a");
                        assert(mapValue.is_int());
                        assert(mapValue.as_int() == 1);
                    }
                    else {
                        assert(key.is_int());
                        assert(key.as_int() == 2);
                        assert(mapValue.is_string());
                        assert(mapValue.as_string() == "b");
                    }
                });
                break;
            case 6:
                assert(item.is_array());
                item.array([&](uint32_t, const Niblp::Value& nested) {
                    assert(nested.is_int());
                    assert(nested.as_int() == 7);
                });
                break;
            default: assert(false); break;
            }
        });
    });

    // Deep nesting: map -> array -> map, string and number keys.
    {
        writer.reset();
        writer.map([&](Niblp::Map map) {
            map.key(std::string_view("m")).array([&](Niblp::Array array) {
                array.value(1);
                array.map([&](Niblp::Map inner) {
                    inner.key(2).value(std::string_view("deep"));
                });
            });
            map.key(3).value(4);
        });
        reader.assign(writer.buffer.data(), writer.buffer.size());
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_map());
            value.map([&](const Niblp::Value& key, const Niblp::Value& item) {
                if (key.is_string()) {
                    assert(key.as_string() == "m");
                    assert(item.is_array());
                    item.array([&](uint32_t idx, const Niblp::Value& element) {
                        switch (idx) {
                        case 0: assert(element.is_uint()); assert(element.as_uint() == 1); break;
                        case 1:
                            assert(element.is_map());
                            element.map([&](const Niblp::Value& innerKey,
                                    const Niblp::Value& innerItem) {
                                assert(innerKey.is_uint());
                                assert(innerKey.as_uint() == 2);
                                assert(innerItem.is_string());
                                assert(innerItem.as_string() == "deep");
                            });
                            break;
                        default: assert(false); break;
                        }
                    });
                }
                else {
                    assert(key.is_uint());
                    assert(key.as_uint() == 3);
                    assert(item.is_uint());
                    assert(item.as_uint() == 4);
                }
            });
        });
    }

    // Truncated streams: parsing every byte prefix of a well-formed stream
    // must stop cleanly -- no reads past the buffer (the sanitizer build
    // checks that), no asserts, just fewer decoded values.
    {
        std::vector<std::vector<uint8_t>> streams;

        writer.reset();
        writer.array([&](Niblp::Array array) {
            for (int32_t i = 0; i < 100; ++i) {
                array.value(int64_t(i) * 7 - 350);
            }
        });
        streams.push_back(writer.buffer);

        writer.reset();
        writer.array([&](Niblp::Array array) {
            for (int32_t i = 0; i < 100; ++i) {
                array.value(getSin(double(i % 100) / 100.0) - 0.5);
            }
        });
        streams.push_back(writer.buffer);

        writer.reset();
        writer.array([&](Niblp::Array array) {
            array.value(std::string_view("truncation probe"));
            const uint8_t payload[5] = { 1, 2, 3, 4, 5 };
            array.bytes(payload, sizeof(payload));
            array.map([&](Niblp::Map map) {
                map.key(std::string_view("k")).value(42);
            });
            array.value(true).value(nullptr).value(-3);
        });
        streams.push_back(writer.buffer);

        for (size_t s = 0; s < streams.size(); ++s) {
            for (size_t size_B = 0; size_B < streams[s].size(); ++size_B) {
                reader.assign(streams[s].data(), size_B);
                uint32_t parsed = 0;
                reader.parse([&](const Niblp::Value&) {
                    ++parsed;
                });
                assert(parsed <= size_B * 2);
            }
        }
    }
}


#if defined(NIBLP_TEST_NO_MSGPACK)
void compare() {} // no msgpack in the C++11 build
#else
void compare() {
    constexpr uint32_t steps = 19;
    constexpr int32_t duration_ms = 10000;
    constexpr int32_t period_ms = 100;
    std::ofstream csv("statistics.csv");
    // Uppercase 'E' in scientific notation, to simplify parsing for LibreOffice
    csv << std::uppercase;
    std::array<std::array<uint32_t, steps>, 6> stat;
    // The max |decoded - original| per amplitude step, one series per
    // shape (sin, tri, rand): the float Niblp3 runs below fill it in.
    std::array<std::array<float, steps>, 3> deviation;

    Niblp::Writer wnblp;
    Niblp::Reader reader;
    msgpack_writer wmsgp;

    double amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wnblp.reset();
        wnblp.array([&](Niblp::Array writer) {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const int64_t y = int64_t(getSin(x) * amplitude * 2.0 - amplitude);
                writer.value(y);
            }
        });
        stat[0][p] = uint32_t(wnblp.buffer.size());
        std::cout << "NibblePack: " << duration_ms << " points of int sin with amplitude "
            << amplitude << " => " << wnblp.buffer.size() << " bytes" << std::endl;
//#     if not defined (_NDEBUG)
//        for (uint32_t count : wnblp.idist) {
//            std::cout << "  " << count;
//        }
//        std::cout << std::endl;
//#     endif
        amplitude *= 10.0f;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wnblp.reset();
        wnblp.array([&](Niblp::Array writer) {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const int64_t y = int64_t(getTri(x) * amplitude * 2.0 - amplitude);
                writer.value(y);
            }
        });
        stat[1][p] = uint32_t(wnblp.buffer.size());
        std::cout << "NibblePack: " << duration_ms << " points of int tri with amplitude "
            << amplitude << " => " << wnblp.buffer.size() << " bytes" << std::endl;
//#     if not defined (_NDEBUG)
//        for (uint32_t count : wnblp.idist) {
//            std::cout << "  " << count;
//        }
//        std::cout << std::endl;
//#     endif
        amplitude *= 10.0f;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wnblp.reset();
        wnblp.array([&](Niblp::Array writer) {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const int64_t y = int64_t(getRand(x_ms) * amplitude * 2.0 - amplitude);
                writer.value(y);
            }
        });
        stat[2][p] = uint32_t(wnblp.buffer.size());
        std::cout << "NibblePack: " << duration_ms << " points of int rand with amplitude "
            << amplitude << " => " << wnblp.buffer.size() << " bytes" << std::endl;
//#     if not defined (_NDEBUG)
//        for (uint32_t count : wnblp.idist) {
//            std::cout << "  " << count;
//        }
//        std::cout << std::endl;
//#     endif
        amplitude *= 10.0f;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wmsgp.clear();
        wmsgp.array(duration_ms, [&]() {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const int64_t y = int64_t(getSin(x) * amplitude * 2.0 - amplitude);
                wmsgp.value(y);
            }
        });
        stat[3][p] = uint32_t(wmsgp.buffer.size());
        std::cout << "MessagePack: " << duration_ms << " points of int sin with amplitude "
            << amplitude << " => " << wmsgp.buffer.size() << " bytes" << std::endl;
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wmsgp.clear();
        wmsgp.array(duration_ms, [&]() {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const int64_t y = int64_t(getTri(x) * amplitude * 2.0 - amplitude);
                wmsgp.value(y);
            }
        });
        stat[4][p] = uint32_t(wmsgp.buffer.size());
        std::cout << "MessagePack: " << duration_ms << " points of int tri with amplitude "
            << amplitude << " => " << wmsgp.buffer.size() << " bytes" << std::endl;
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wmsgp.clear();
        wmsgp.array(duration_ms, [&]() {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const int64_t y = int64_t(getRand(x_ms) * amplitude * 2.0 - amplitude);
                wmsgp.value(y);
            }
        });
        stat[5][p] = uint32_t(wmsgp.buffer.size());
        std::cout << "MessagePack: " << duration_ms << " points of int rand with amplitude "
            << amplitude << " => " << wmsgp.buffer.size() << " bytes" << std::endl;
        amplitude *= 10.0;
    }

    csv << "int 10 Hz at 1000 Hz 10 s\n";
    csv << "amplitude, NibblePack sin, NibblePack tri, NibblePack rand, MessagePack sin, MessagePack tri, MessagePack rand\n";
    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        csv << amplitude;
        for (const auto& s : stat) {
            csv << ", " << s[p];
        }
        csv << "\n";
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wnblp.reset();
        wnblp.array([&](Niblp::Array writer) {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const double y = getSin(x) * amplitude * 2.0 - amplitude;
                writer.value(y);
            }
        });
        stat[0][p] = uint32_t(wnblp.buffer.size());
        double dev = 0.0;
        reader.assign(wnblp.buffer.data(), wnblp.buffer.size());
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t idx, const Niblp::Value& item) {
                assert(item.is_float());
                const double x = double(idx % period_ms) / double(period_ms);
                const double y = getSin(x) * amplitude * 2.0 - amplitude;
                const double d = std::abs(item.as_float64() - y);
                if (d > dev) {
                    dev = d;
                }
            });
        });
        dev /= amplitude; // relative to the full scale, comparable to tolerance
        deviation[0][p] = float(dev);
        std::cout << "NibblePack: " << duration_ms << " points of float sin with amplitude "
            << amplitude << " => " << wnblp.buffer.size() << " bytes, max dev " << dev << std::endl;
//#     if not defined (_NDEBUG)
//        for (uint32_t count : wnblp.fdist) {
//            std::cout << "  " << count;
//        }
//        std::cout << std::endl;
//#     endif
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wnblp.reset();
        wnblp.array([&](Niblp::Array writer) {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const double y = getTri(x) * amplitude * 2.0 - amplitude;
                writer.value(y);
            }
        });
        stat[1][p] = uint32_t(wnblp.buffer.size());
        double dev = 0.0;
        reader.assign(wnblp.buffer.data(), wnblp.buffer.size());
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t idx, const Niblp::Value& item) {
                assert(item.is_float());
                const double x = double(idx % period_ms) / double(period_ms);
                const double y = getTri(x) * amplitude * 2.0 - amplitude;
                const double d = std::abs(item.as_float64() - y);
                if (d > dev) {
                    dev = d;
                }
            });
        });
        dev /= amplitude; // relative to the full scale, comparable to tolerance
        deviation[1][p] = float(dev);
        std::cout << "NibblePack: " << duration_ms << " points of float tri with amplitude "
            << amplitude << " => " << wnblp.buffer.size() << " bytes, max dev " << dev << std::endl;
//#     if not defined (_NDEBUG)
//        for (uint32_t count : wnblp.fdist) {
//            std::cout << "  " << count;
//        }
//        std::cout << std::endl;
//#     endif
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wnblp.reset();
        wnblp.array([&](Niblp::Array writer) {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double y = getRand(x_ms) * amplitude * 2.0 - amplitude;
                writer.value(y);
            }
        });
        stat[2][p] = uint32_t(wnblp.buffer.size());
        double dev = 0.0;
        reader.assign(wnblp.buffer.data(), wnblp.buffer.size());
        reader.parse([&](const Niblp::Value& value) {
            assert(value.is_array());
            value.array([&](uint32_t idx, const Niblp::Value& item) {
                assert(item.is_float());
                const double y = getRand(idx) * amplitude * 2.0 - amplitude;
                const double d = std::abs(item.as_float64() - y);
                if (d > dev) {
                    dev = d;
                }
            });
        });
        dev /= amplitude; // relative to the full scale, comparable to tolerance
        deviation[2][p] = float(dev);
        std::cout << "NibblePack: " << duration_ms << " points of float rand with amplitude "
            << amplitude << " => " << wnblp.buffer.size() << " bytes, max dev " << dev << std::endl;
//#     if not defined (_NDEBUG)
//        for (uint32_t count : wnblp.fdist) {
//            std::cout << "  " << count;
//        }
//        std::cout << std::endl;
//#     endif
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wmsgp.clear();
        wmsgp.array(duration_ms, [&]() {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const float x = float(x_ms % period_ms) / float(period_ms);
                const float y = getSin(x) * float(amplitude) * 2.0f - float(amplitude);
                wmsgp.value(y);
            }
        });
        stat[3][p] = uint32_t(wmsgp.buffer.size());
        std::cout << "MessagePack: " << duration_ms << " points of float sin with amplitude "
            << amplitude << " => " << wmsgp.buffer.size() << " bytes" << std::endl;
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wmsgp.clear();
        wmsgp.array(duration_ms, [&]() {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double x = double(x_ms % period_ms) / double(period_ms);
                const double y = getTri(x) * amplitude * 2.0 - amplitude;
                wmsgp.value(y);
            }
        });
        stat[4][p] = uint32_t(wmsgp.buffer.size());
        std::cout << "MessagePack: " << duration_ms << " points of float tri with amplitude "
            << amplitude << " => " << wmsgp.buffer.size() << " bytes" << std::endl;
        amplitude *= 10.0;
    }

    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        wmsgp.clear();
        wmsgp.array(duration_ms, [&]() {
            for (int32_t x_ms = 0; x_ms < duration_ms; ++x_ms) {
                const double y = getRand(x_ms) * amplitude * 2.0 - amplitude;
                wmsgp.value(y);
            }
        });
        stat[5][p] = uint32_t(wmsgp.buffer.size());
        std::cout << "MessagePack: " << duration_ms << " points of float rand with amplitude "
            << amplitude << " => " << wmsgp.buffer.size() << " bytes" << std::endl;
        amplitude *= 10.0;
    }

    csv << "\n";
    csv << "float 10 Hz at 1000 Hz 10 s\n";
    csv << "amplitude, NibblePack sin, NibblePack tri, NibblePack rand, MessagePack f32, MessagePack f64, MessagePack rand\n";
    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        csv << amplitude;
        for (const auto& s : stat) {
            csv << ", " << s[p];
        }
        csv << "\n";
        amplitude *= 10.0;
    }

    csv << "\n";
    csv << "float max deviation relative to amplitude (|decoded - original| / amplitude)\n";
    csv << "amplitude, NibblePack sin, NibblePack tri, NibblePack rand\n";
    amplitude = 1.0;
    for (uint32_t p = 0; p < steps; ++p) {
        csv << amplitude;
        for (const auto& d : deviation) {
            csv << ", " << d[p];
        }
        csv << "\n";
        amplitude *= 10.0;
    }
}
#endif // NIBLP_TEST_NO_MSGPACK

void benchmark() {

}

int32_t main(int32_t argc, char** argv) {
    bool noAbortPopup = false;
    for (int32_t i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-abort-popup") == 0) {
            noAbortPopup = true;
        }
    }
    if (noAbortPopup) {
#     ifdef _MSC_VER
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#     endif
    }
    //test_varint();
    test_varfloat();
    test_NibblePack();
    compare();
    benchmark();
    return 0;
}
