#include "spinneret/util/random.h"

#include <array>
#include <cstdint>
#include <random>

namespace spinneret::util {

namespace {

constexpr char kHex[] = "0123456789abcdef";

std::mt19937_64& thread_rng() {
    thread_local std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return rng;
}

}  // namespace

std::string random_uuid() {
    std::array<std::uint8_t, 16> bytes{};
    auto& rng = thread_rng();
    for (std::size_t i = 0; i < bytes.size(); i += 8) {
        std::uint64_t v = rng();
        for (std::size_t j = 0; j < 8; ++j) bytes[i + j] = static_cast<std::uint8_t>(v >> (j * 8));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);  // version 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);  // RFC 4122 variant

    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

std::string random_hex(std::size_t bytes) {
    std::random_device rd;
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        auto b = static_cast<std::uint8_t>(rd() & 0xFF);
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

}  // namespace spinneret::util
