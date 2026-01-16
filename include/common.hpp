/**
 * @file common.hpp
 * @brief Common utilities, macros, and types for high-performance market data processing
 * 
 * This header provides:
 * - Platform detection and compiler intrinsics
 * - Branch prediction hints (LIKELY/UNLIKELY)
 * - High-resolution TSC-based timing
 * - Endianness conversion utilities
 * - Cache-line alignment helpers
 */

#pragma once

// Standard library includes first to avoid conflicts
#include <cstdint>
#include <cstring>
#include <chrono>
#include <functional>

#if defined(_MSC_VER)
    #define ITCH_MSVC 1
    #include <intrin.h>
    #define ITCH_FORCE_INLINE __forceinline
    #define ITCH_NOINLINE __declspec(noinline)
    #define ITCH_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
    #define ITCH_GCC_COMPATIBLE 1
    #define ITCH_FORCE_INLINE inline __attribute__((always_inline))
    #define ITCH_NOINLINE __attribute__((noinline))
    #define ITCH_RESTRICT __restrict__
#else
    #define ITCH_FORCE_INLINE inline
    #define ITCH_NOINLINE
    #define ITCH_RESTRICT
#endif

namespace itch {

// =============================================================================
// Branch Prediction Hints
// =============================================================================

#if defined(ITCH_GCC_COMPATIBLE)
    #define ITCH_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define ITCH_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define ITCH_LIKELY(x)   (x)
    #define ITCH_UNLIKELY(x) (x)
#endif

// =============================================================================
// Cache Line Alignment
// =============================================================================

constexpr std::size_t CACHE_LINE_SIZE = 64;

#if defined(ITCH_MSVC)
    #define ITCH_CACHE_ALIGNED __declspec(align(64))
#elif defined(ITCH_GCC_COMPATIBLE)
    #define ITCH_CACHE_ALIGNED __attribute__((aligned(64)))
#else
    #define ITCH_CACHE_ALIGNED
#endif

// =============================================================================
// Fixed-Size Types
// =============================================================================

using Price = std::int64_t;      // Price in fixed-point (4 decimal places)
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using Timestamp = std::uint64_t; // Nanoseconds since midnight
using StockLocate = std::uint16_t;
using TrackingNumber = std::uint16_t;

// Stock symbol (8 bytes, space-padded ASCII)
struct Symbol {
    char data[8];
    
    bool operator==(const Symbol& other) const noexcept {
        return std::memcmp(data, other.data, 8) == 0;
    }
    
    bool operator!=(const Symbol& other) const noexcept {
        return !(*this == other);
    }
    
    bool operator<(const Symbol& other) const noexcept {
        return std::memcmp(data, other.data, 8) < 0;
    }
};

// MPID (4 bytes, space-padded ASCII)
struct MPID {
    char data[4];
};

// =============================================================================
// Endianness Conversion (Big-Endian Network Byte Order to Host)
// =============================================================================

namespace endian {

/**
 * @brief Convert 16-bit big-endian to host byte order
 */
ITCH_FORCE_INLINE std::uint16_t be16_to_host(std::uint16_t value) noexcept {
#if defined(ITCH_MSVC)
    return _byteswap_ushort(value);
#elif defined(ITCH_GCC_COMPATIBLE)
    return __builtin_bswap16(value);
#else
    return static_cast<std::uint16_t>((value >> 8) | (value << 8));
#endif
}

/**
 * @brief Convert 32-bit big-endian to host byte order
 */
ITCH_FORCE_INLINE std::uint32_t be32_to_host(std::uint32_t value) noexcept {
#if defined(ITCH_MSVC)
    return _byteswap_ulong(value);
#elif defined(ITCH_GCC_COMPATIBLE)
    return __builtin_bswap32(value);
#else
    return ((value >> 24) & 0xFF) |
           ((value >> 8) & 0xFF00) |
           ((value << 8) & 0xFF0000) |
           ((value << 24) & 0xFF000000);
#endif
}

/**
 * @brief Convert 64-bit big-endian to host byte order
 */
ITCH_FORCE_INLINE std::uint64_t be64_to_host(std::uint64_t value) noexcept {
#if defined(ITCH_MSVC)
    return _byteswap_uint64(value);
#elif defined(ITCH_GCC_COMPATIBLE)
    return __builtin_bswap64(value);
#else
    return ((value >> 56) & 0xFF) |
           ((value >> 40) & 0xFF00) |
           ((value >> 24) & 0xFF0000) |
           ((value >> 8) & 0xFF000000ULL) |
           ((value << 8) & 0xFF00000000ULL) |
           ((value << 24) & 0xFF0000000000ULL) |
           ((value << 40) & 0xFF000000000000ULL) |
           ((value << 56) & 0xFF00000000000000ULL);
#endif
}

/**
 * @brief Convert 48-bit (6-byte) big-endian timestamp to host byte order
 * ITCH 5.0 uses 6-byte timestamps for nanoseconds since midnight
 */
ITCH_FORCE_INLINE std::uint64_t be48_to_host(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint64_t>(data[0]) << 40) |
           (static_cast<std::uint64_t>(data[1]) << 32) |
           (static_cast<std::uint64_t>(data[2]) << 24) |
           (static_cast<std::uint64_t>(data[3]) << 16) |
           (static_cast<std::uint64_t>(data[4]) << 8) |
           (static_cast<std::uint64_t>(data[5]));
}

} // namespace endian

// =============================================================================
// High-Resolution Timing (TSC-Based)
// =============================================================================

namespace timing {

/**
 * @brief Read Time Stamp Counter (TSC) for high-resolution timing
 * Returns CPU cycles since reset - extremely low overhead
 */
ITCH_FORCE_INLINE std::uint64_t rdtsc() noexcept {
#if defined(ITCH_MSVC)
    return __rdtsc();
#elif defined(ITCH_GCC_COMPATIBLE) && (defined(__x86_64__) || defined(__i386__))
    std::uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
#else
    // Fallback to chrono for non-x86 platforms
    return static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
#endif
}

/**
 * @brief Read TSC with serialization (more accurate but slightly higher overhead)
 */
ITCH_FORCE_INLINE std::uint64_t rdtscp() noexcept {
#if defined(ITCH_MSVC)
    unsigned int aux;
    return __rdtscp(&aux);
#elif defined(ITCH_GCC_COMPATIBLE) && (defined(__x86_64__) || defined(__i386__))
    std::uint32_t lo, hi, aux;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
#else
    return rdtsc();
#endif
}

/**
 * @brief Simple latency measurement helper
 */
class LatencyTimer {
public:
    ITCH_FORCE_INLINE void start() noexcept { start_ = rdtsc(); }
    ITCH_FORCE_INLINE void stop() noexcept { end_ = rdtscp(); }
    ITCH_FORCE_INLINE std::uint64_t cycles() const noexcept { return end_ - start_; }
    
    // Convert cycles to nanoseconds (requires calibrated cycles_per_ns)
    ITCH_FORCE_INLINE double nanoseconds(double cycles_per_ns) const noexcept {
        return static_cast<double>(cycles()) / cycles_per_ns;
    }

private:
    std::uint64_t start_ = 0;
    std::uint64_t end_ = 0;
};

/**
 * @brief Estimate TSC frequency (cycles per nanosecond)
 * Uses a busy-wait loop to avoid thread include dependency
 */
inline double calibrate_tsc() {
    auto start_time = std::chrono::high_resolution_clock::now();
    std::uint64_t start_tsc = rdtsc();
    
    // Busy-wait for ~50ms
    auto target = start_time + std::chrono::milliseconds(50);
    while (std::chrono::high_resolution_clock::now() < target) {
        // Busy wait
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::uint64_t end_tsc = rdtsc();
    
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    std::uint64_t elapsed_tsc = end_tsc - start_tsc;
    
    return static_cast<double>(elapsed_tsc) / static_cast<double>(elapsed_ns);
}

} // namespace timing

// =============================================================================
// Side Enum
// =============================================================================

enum class Side : char {
    Buy = 'B',
    Sell = 'S'
};

ITCH_FORCE_INLINE Side char_to_side(char c) noexcept {
    return static_cast<Side>(c);
}

ITCH_FORCE_INLINE bool is_buy(Side s) noexcept {
    return s == Side::Buy;
}

// =============================================================================
// Compile-Time Utilities
// =============================================================================

template<typename T>
ITCH_FORCE_INLINE constexpr bool is_power_of_two(T value) noexcept {
    return value && !(value & (value - 1));
}

template<typename T>
ITCH_FORCE_INLINE constexpr T align_up(T value, T alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

// =============================================================================
// Prefetch Hints
// =============================================================================

ITCH_FORCE_INLINE void prefetch_read(const void* ptr) noexcept {
    (void)ptr;
#if defined(ITCH_GCC_COMPATIBLE)
    __builtin_prefetch(ptr, 0, 3);
#elif defined(ITCH_MSVC)
    _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T0);
#endif
}

ITCH_FORCE_INLINE void prefetch_write(void* ptr) noexcept {
    (void)ptr;
#if defined(ITCH_GCC_COMPATIBLE)
    __builtin_prefetch(ptr, 1, 3);
#elif defined(ITCH_MSVC)
    _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T0);
#endif
}

// =============================================================================
// Symbol Hash Function
// =============================================================================

struct SymbolHash {
    std::size_t operator()(const Symbol& s) const noexcept {
        // Fast hash using 64-bit load
        std::uint64_t val;
        std::memcpy(&val, s.data, 8);
        // Mix bits (FNV-1a inspired)
        val ^= val >> 33;
        val *= 0xff51afd7ed558ccdULL;
        val ^= val >> 33;
        val *= 0xc4ceb9fe1a85ec53ULL;
        val ^= val >> 33;
        return static_cast<std::size_t>(val);
    }
};

} // namespace itch

// Hash specialization for Symbol (must be outside itch namespace)
namespace std {
template<>
struct hash<itch::Symbol> {
    std::size_t operator()(const itch::Symbol& s) const noexcept {
        return itch::SymbolHash{}(s);
    }
};
} // namespace std
