//
// MD5 (RFC 1321) and HMAC-MD5 (RFC 2104), written from the RFCs.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <bit>
#include <cmath>
#include <cstring>

#include "util/codec.h"

namespace util {
namespace {

constexpr std::array<int, 64> shifts{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

// Return the RFC's sine table.
std::array<std::uint32_t, 64> make_sines() {
    std::array<std::uint32_t, 64> table{};
    for (int i = 0; i < 64; ++i)
        table[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(
            std::floor(std::fabs(std::sin(i + 1.0)) * 4294967296.0));
    return table;
}

// Mix one 64 byte block into the state.
void transform(std::array<std::uint32_t, 4> &state,
               const unsigned char *block) {
    static const std::array<std::uint32_t, 64> sines = make_sines();
    std::array<std::uint32_t, 16> words{};
    for (std::size_t i = 0; i < 16; ++i)
        words[i] = static_cast<std::uint32_t>(block[i * 4]) |
                   static_cast<std::uint32_t>(block[i * 4 + 1]) << 8 |
                   static_cast<std::uint32_t>(block[i * 4 + 2]) << 16 |
                   static_cast<std::uint32_t>(block[i * 4 + 3]) << 24;
    auto [a, b, c, d] = state;
    for (std::size_t i = 0; i < 64; ++i) {
        std::uint32_t mix = 0;
        std::size_t word = 0;
        if (i < 16) {
            mix = (b & c) | (~b & d);
            word = i;
        } else if (i < 32) {
            mix = (d & b) | (~d & c);
            word = (5 * i + 1) % 16;
        } else if (i < 48) {
            mix = b ^ c ^ d;
            word = (3 * i + 5) % 16;
        } else {
            mix = c ^ (b | ~d);
            word = (7 * i) % 16;
        }
        const std::uint32_t rotated =
            a + mix + sines[i] + words[word];
        a = d;
        d = c;
        c = b;
        b += std::rotl(rotated, shifts[i]);
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

} // namespace

std::array<std::uint8_t, 16> md5(std::string_view data) {
    std::array<std::uint32_t, 4> state{0x67452301, 0xefcdab89, 0x98badcfe,
                                       0x10325476};
    std::string padded(data);
    padded += static_cast<char>(0x80);
    while (padded.size() % 64 != 56)
        padded += '\0';
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; ++i)
        padded += static_cast<char>(bits >> (i * 8) & 255);
    for (std::size_t i = 0; i < padded.size(); i += 64)
        transform(state, reinterpret_cast<const unsigned char *>(
                             padded.data() + i));
    std::array<std::uint8_t, 16> digest{};
    for (std::size_t i = 0; i < 16; ++i)
        digest[i] =
            static_cast<std::uint8_t>(state[i / 4] >> ((i % 4) * 8) & 255);
    return digest;
}

std::string md5_hex(std::string_view data) {
    const auto digest = md5(data);
    return hex_encode({reinterpret_cast<const char *>(digest.data()),
                       digest.size()});
}

std::string hmac_md5_hex(std::string_view key, std::string_view data) {
    std::string block(key);
    if (block.size() > 64) {
        const auto digest = md5(block);
        block.assign(reinterpret_cast<const char *>(digest.data()),
                     digest.size());
    }
    block.resize(64, '\0');
    std::string inner = block;
    std::string outer = block;
    for (std::size_t i = 0; i < 64; ++i) {
        inner[i] = static_cast<char>(inner[i] ^ 0x36);
        outer[i] = static_cast<char>(outer[i] ^ 0x5c);
    }
    inner += data;
    const auto inner_digest = md5(inner);
    outer.append(reinterpret_cast<const char *>(inner_digest.data()),
                 inner_digest.size());
    return md5_hex(outer);
}

} // namespace util
