#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace market_engine {
inline std::uint64_t checkedAdd(std::uint64_t a, std::uint64_t b) {
    if (b > UINT64_MAX - a) throw std::overflow_error("Integer addition overflow.");
    return a + b;
}
inline std::uint64_t checkedMultiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > UINT64_MAX / b) throw std::overflow_error("Integer multiplication overflow.");
    return a * b;
}
inline std::int64_t signedTicks(std::uint64_t value) {
    if (value > INT64_MAX) throw std::overflow_error("Value exceeds signed reporting range.");
    return static_cast<std::int64_t>(value);
}
inline std::int64_t difference(std::uint64_t a, std::uint64_t b) {
    return a >= b ? signedTicks(a - b) : -signedTicks(b - a);
}
inline std::int64_t checkedSignedAdd(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        throw std::overflow_error("Signed addition overflow.");
    return a + b;
}
}
