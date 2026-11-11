#pragma once
#ifndef NIBBLE_PACK_FP_T_HPP
#define NIBBLE_PACK_FP_T_HPP

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Niblp {

//   name                 format                precision for 0..1    standard
// float32   [1:sign][8:exponent][23:fraction]      6-7 digits      IEEE 754-2008 (single-precision 32-bit binary float)
//
// bfloat16  [1:sign][8:exponent][7:fraction]       2 digits        OCP Microscaling Formats (MX) / Google Brain
//     0, 5.877'47e-39, 1.175'49e-38, 1.763'26e-38, ..., 3.382'15e+38, 3.384'81e+38, 3.389'53e+38
//     ..., 1, 1.007'812, 1.015'625, 1.023'437, 1.031'25, 1.039'062, 1.046'875, 1.054'687, ...
//
// float16   [1:sign][5:exponent][10:fraction]      3-4 digits      IEEE 754-2008 (half-precision 16-bit binary float)
//     0, 0.000'000'059, 0.000'000'119, 0.000'000'179, ..., 65'408, 65'440, 65'504
//     ..., 1, 1.000'976, 1.001'953, 1.002'929, 1.003'906, 1.004'883, 1.005'859, 1.006'834, ...
//
// float8    [1:sign][3:exponent][4:fraction]       1-2 digits      E3M4 / E3M4FN
//     0, 0.015'625, 0.031'25, 0.046'875, 0.062'5, 0.078'125, ..., 13.5, 14, 14.5, 15, 15.5
//     ..., 1, 1.062'5, 1.125, 1.187'5, 1.25, 1.312'5, 1.375, 1.437'5, 1.5, ...
//
// float8    [1:sign][4:exponent][3:fraction]       1-2 digits      OCP FP8 / IEEE P3109 (E4M3 / E4M3FN)
//     0, 0.015'625, 0.031'25, 0.046'875, 0.062'5, 0.078'125, ..., 1'024, 1'152, 1'280, 1'408, 1'536
//     ..., 1, 1.125, 1.25, 1.375, 1.5, 1.625, 1.75, 1.875, 2, ...
//
// float8    [1:sign][5:exponent][2:fraction]       1 digit         OCP FP8 / IEEE P3109 (E5M2)
//     0, 0.000'015'2, 0.000'030'5, 0.000'457'7, 0.000'061, ..., 20'480, 24'576, 28'672, 57'344
//     ..., 1, 1.25, 1.5, 1.75, 2, ...
//
// float4    [1:sign][2:exponent][1:fraction]       0-1 digit       OCP MXFP4 / IEEE P3109 (E2M1)
//     0, 0.5, 1, 1.5, 2, 3, 4, 6(MXFP4)
//
// float4    [1:sign][3:exponent][0:fraction]       0-1 digit       OCP Microscaling Formats / IEEE P3109 (E3M0)
//     0, 0.062'5, 0.125, 0.25, 0.5, 1, 2, 4, 8

// finite == true:
//   * the all-zero payload is +0.0 (delta 0.0); the sign-only payload is -0.0;
//   * exponent field 0 with a non-zero mantissa is a subnormal;
//   * normal values use exponent fields up to max_finite_exponent; values
//     that would decode to Inf are the only invalid payloads (for E11M52
//     that is just the max exponent field);
//   * there are no NaNs and no Infs;
//   * try_from() accepts finite values, including subnormals; a
//     non-zero value that rounds to zero is rejected.
//
// finite == false makes the type IEEE-754-like and universally usable:
//   * exponent field 0 is +/-0.0 (mantissa 0) or a subnormal;
//   * exponent field max is +/-Inf (mantissa 0) or NaN (mantissa != 0);
//   * every bit pattern is valid and try_from() accepts NaN and Inf.
template <uint8_t exponent_b, uint8_t mantissa_b, bool finite = false>
struct fp_t {
    static_assert(exponent_b >= 1, "");
    static_assert(exponent_b <= 30, "");
    static_assert(mantissa_b >= 1, "");
    static_assert(1 + exponent_b + mantissa_b <= 64, "");

    static constexpr uint8_t total_b = 1 + exponent_b + mantissa_b;
    static constexpr uint8_t payload_bits = total_b;

    using storage_t = typename std::conditional<total_b <= 8,
        uint8_t,
        typename std::conditional<total_b <= 16,
            uint16_t,
            typename std::conditional<total_b <= 32,
                uint32_t, uint64_t
            >::type
        >::type
    >::type;

    storage_t bits = 0;

    static constexpr storage_t sign_mask =
        storage_t(1) << (exponent_b + mantissa_b);
    static constexpr storage_t exp_mask =
        ((storage_t(1) << exponent_b) - 1) << mantissa_b;
    static constexpr storage_t man_mask =
        (storage_t(1) << mantissa_b) - 1;
    static constexpr int32_t bias = (1 << (exponent_b - 1)) - 1;
    static constexpr storage_t max_biased_exponent =
        static_cast<storage_t>((uint32_t(1) << exponent_b) - 1);
    // The largest exponent field whose normal values still fit in a double.
    static constexpr int32_t max_finite_exponent =
        (bias + 1023 < static_cast<int32_t>(max_biased_exponent))
            ? bias + 1023
            : static_cast<int32_t>(max_biased_exponent);
    static constexpr storage_t quiet_nan_bit =
        storage_t(1) << (mantissa_b - 1);

    constexpr fp_t() noexcept = default;

    // Convenience constructor. Invalid values are stored as 0; use
    // try_from() when representability must be checked. Floating-point inputs
    // keep their own arithmetic; integer inputs are converted through double.
    // In non-finite mode NaN and +/-Inf are stored as their IEEE-like
    // payloads.
    template <typename number_t,
        typename std::enable_if<
            std::is_arithmetic<number_t>::value, int>::type = 0>
    fp_t(number_t value) noexcept {
        if (not try_from(value)) {
            bits = 0;
        }
    }

    // Encodes a value.
    // finite mode: returns false for NaN, Inf, a non-zero value that rounds
    // to zero, and values beyond the largest finite exponent.
    // non-finite mode: every value can be encoded (with rounding to nearest
    // even), including NaN and +/-Inf.
    // Floating-point inputs are converted in their own arithmetic (float
    // stays float); integer inputs are converted through double.
    template <typename number_t,
        typename std::enable_if<
            std::is_arithmetic<number_t>::value, int>::type = 0>
    bool try_from(number_t value) noexcept {
        using real_t = typename std::conditional<
            std::is_floating_point<number_t>::value,
            number_t, double>::type;
        const real_t real_value = static_cast<real_t>(value);

        if (std::isnan(real_value)) {
            if (finite) {
                return false;
            }
            bits = make_nan();
            return true;
        }
        if (std::isinf(real_value)) {
            if (finite) {
                return false;
            }
            bits = make_inf(std::signbit(real_value));
            return true;
        }

        const bool negative = std::signbit(real_value);
        const real_t magnitude = std::fabs(real_value);
        if (magnitude == real_t(0)) {
            bits = negative ? sign_mask : storage_t(0);
            return true;
        }

        int exp2 = 0;
        const real_t significand = std::frexp(magnitude, &exp2);
        int exponent = exp2 - 1 + bias;

        const int max_exponent = finite
            ? max_finite_exponent
            : static_cast<int>(max_biased_exponent) - 1;
        if (exponent > max_exponent) {
            if (finite) {
                return false;
            }
            bits = make_inf(negative);
            return true;
        }

        if (exponent <= 0) {
            const real_t scaled = std::ldexp(
                magnitude, bias - 1 + mantissa_b);
            uint64_t mantissa = round_half_even(scaled);
            exponent = 0;
            if (mantissa >= (uint64_t(1) << mantissa_b)) {
                // Rounded up to the smallest normal value.
                exponent = 1;
                mantissa = 0;
            }
            else if (finite and mantissa == 0) {
                // A non-zero value rounded to zero: not representable.
                return false;
            }
            if (exponent > max_exponent) {
                if (finite) {
                    return false;
                }
                bits = make_inf(negative);
                return true;
            }
            bits = pack(negative, exponent, mantissa);
            return true;
        }

        const real_t fraction = significand * real_t(2) - real_t(1);
        uint64_t mantissa = round_half_even(
            fraction * static_cast<real_t>(uint64_t(1) << mantissa_b));
        if (mantissa >= (uint64_t(1) << mantissa_b)) {
            mantissa = 0;
            ++exponent;
            if (exponent > max_exponent) {
                if (finite) {
                    return false;
                }
                bits = make_inf(negative);
                return true;
            }
        }

        bits = pack(negative, exponent, mantissa);
        return true;
    }

    operator double() const noexcept {
        return to_real<double>();
    }

    // Same as operator double(), but the conversion is performed in float
    // arithmetic (frexp/ldexp/floor are not promoted to double).
    operator float() const noexcept {
        return to_real<float>();
    }

    static bool is_valid_bits(storage_t bits) noexcept {
        if (not finite) {
            return true;
        }
        if (bits == 0) {
            return true;
        }
        const storage_t e = (bits & exp_mask) >> mantissa_b;
        if (e == 0) {
            // +/-0.0 or a subnormal.
            return true;
        }
        return static_cast<int32_t>(e) <= max_finite_exponent;
    }

    bool is_valid() const noexcept {
        return is_valid_bits(bits);
    }

    bool isZero() const noexcept {
        return (bits & static_cast<storage_t>(~sign_mask)) == 0;
    }

    bool isNaN() const noexcept {
        if (finite) {
            return false;
        }
        return (bits & exp_mask) == exp_mask and (bits & man_mask) != 0;
    }

    bool isInf() const noexcept {
        if (finite) {
            return false;
        }
        return (bits & exp_mask) == exp_mask and (bits & man_mask) == 0;
    }

    bool isFinite() const noexcept {
        if (finite) {
            // Reserved payloads decode as NaN, so they are not finite.
            return is_valid();
        }
        return !isNaN() and !isInf();
    }

    int8_t sign() const noexcept {
        if ((bits & sign_mask) != 0) {
            return -1;
        }
        return isZero() ? 0 : 1;
    }

private:
    template <typename real_t>
    real_t to_real() const noexcept {
        const bool negative = (bits & sign_mask) != 0;
        const storage_t e = (bits & exp_mask) >> mantissa_b;
        const storage_t m = bits & man_mask;

        if (e == 0) {
            if (m == 0) {
                return negative ? -real_t(0) : real_t(0);
            }
            const real_t subnormal = std::ldexp(
                static_cast<real_t>(m), 1 - bias - mantissa_b);
            return negative ? -subnormal : subnormal;
        }

        if (finite) {
            if (static_cast<int32_t>(e) > max_finite_exponent) {
                // The value would be out of the result range.
                return std::numeric_limits<real_t>::quiet_NaN();
            }
        }
        else if (e == max_biased_exponent) {
            if (m != 0) {
                return std::numeric_limits<real_t>::quiet_NaN();
            }
            return negative
                ? -std::numeric_limits<real_t>::infinity()
                : std::numeric_limits<real_t>::infinity();
        }

        const real_t fraction = static_cast<real_t>(m)
            / static_cast<real_t>(storage_t(1) << mantissa_b);
        const real_t result = std::ldexp(real_t(1) + fraction,
            static_cast<int>(e) - bias);
        return negative ? -result : result;
    }

    static storage_t pack(bool negative, int exponent, uint64_t mantissa) noexcept {
        return static_cast<storage_t>(
            (negative ? sign_mask : storage_t(0))
            | (static_cast<storage_t>(exponent) << mantissa_b)
            | static_cast<storage_t>(mantissa));
    }

    static storage_t make_inf(bool negative) noexcept {
        return static_cast<storage_t>(
            (negative ? sign_mask : storage_t(0))
            | (max_biased_exponent << mantissa_b));
    }

    static storage_t make_nan() noexcept {
        return static_cast<storage_t>(
            (max_biased_exponent << mantissa_b) | quiet_nan_bit);
    }

    // Rounds a non-negative value to the nearest integer, ties to even.
    template <typename real_t>
    static uint64_t round_half_even(real_t value) noexcept {
        const real_t lower = std::floor(value);
        uint64_t result = static_cast<uint64_t>(lower);
        const real_t fraction = value - lower;
        if (fraction > real_t(0.5)
            or (fraction == real_t(0.5) and (result & 1) != 0)) {
            ++result;
        }
        return result;
    }
};

static_assert(sizeof(fp_t<3, 4>) == sizeof(uint8_t), "");
static_assert(sizeof(fp_t<5, 10>) == sizeof(uint16_t), "");
static_assert(sizeof(fp_t<8, 23>) == sizeof(uint32_t), "");
static_assert(sizeof(fp_t<11, 52>) == sizeof(uint64_t), "");

} // namespace Niblp

#endif // NIBBLE_PACK_FP_T_HPP
