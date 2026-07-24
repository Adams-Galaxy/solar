#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace solar::remote::detail
{

[[nodiscard]] consteval std::array<std::byte, 32> sha256(std::span<const std::byte> input) noexcept
{
    constexpr std::array<std::uint32_t, 64> round{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    };
    std::array<std::uint32_t, 8> hash{
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    const auto blocks = (input.size() + 9U + 63U) / 64U;
    for (std::size_t block{}; block < blocks; ++block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index{}; index < 64; ++index) {
            std::uint8_t value{};
            const auto absolute = block * 64U + index;
            if (absolute < input.size()) {
                value = std::to_integer<std::uint8_t>(input[absolute]);
            } else if (absolute == input.size()) {
                value = 0x80;
            } else if (absolute >= blocks * 64U - 8U) {
                const auto shift = (blocks * 64U - 1U - absolute) * 8U;
                value = static_cast<std::uint8_t>((static_cast<std::uint64_t>(input.size()) * 8U) >>
                                                  shift);
            }
            words[index / 4U] |= static_cast<std::uint32_t>(value) << ((3U - index % 4U) * 8U);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^
                            (words[index - 15] >> 3U);
            const auto s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^
                            (words[index - 2] >> 10U);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = hash;
        for (std::size_t index{}; index < words.size(); ++index) {
            const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary1 = h + sum1 + choice + round[index] + words[index];
            const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }
    std::array<std::byte, 32> output{};
    for (std::size_t index{}; index < hash.size(); ++index) {
        for (std::size_t byte{}; byte < 4; ++byte) {
            output[index * 4U + byte] = static_cast<std::byte>(hash[index] >> ((3U - byte) * 8U));
        }
    }
    return output;
}

} // namespace solar::remote::detail
