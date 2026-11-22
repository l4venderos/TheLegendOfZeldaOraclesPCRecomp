#include "oracles/rom.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace oracles {
namespace {
std::uint32_t rotate_right(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
constexpr std::array<std::uint32_t, 64> K = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
std::uint32_t load_be(const std::uint8_t* p) { return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3]; }
}

RomImage RomImage::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open ROM: " + path.string());
    RomImage image;
    image.bytes.assign(std::istreambuf_iterator<char>(file), {});
    if (image.bytes.size() < 0x150) throw std::runtime_error("File is too small to contain a Game Boy cartridge header.");
    if (image.bytes.size() % 0x4000 != 0) throw std::runtime_error("ROM size is not a whole number of 16 KiB banks.");
    for (std::size_t i = 0x134; i <= 0x143; ++i) {
        const auto ch = image.bytes[i];
        if (ch == 0) break;
        if (ch >= 32 && ch <= 126) image.title.push_back(static_cast<char>(ch));
    }
    image.cartridge_type = image.bytes[0x147];
    image.rom_size_code = image.bytes[0x148];
    image.ram_size_code = image.bytes[0x149];
    image.cgb_flag = image.bytes[0x143];
    image.bank_count = image.bytes.size() / 0x4000;
    return image;
}

std::size_t RomImage::physical_offset(std::uint16_t address, std::uint16_t bank) const {
    if (address < 0x4000) return address;
    const std::size_t selected = bank % bank_count;
    return selected * 0x4000 + (address - 0x4000);
}
std::uint8_t RomImage::read(std::uint16_t address, std::uint16_t bank) const {
    const auto offset = physical_offset(address, bank);
    return offset < bytes.size() ? bytes[offset] : 0xFF;
}

std::string sanitize_identifier(const std::string& input) {
    std::string out;
    for (unsigned char ch : input) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) out.push_back(static_cast<char>(ch));
        else if (!out.empty() && out.back() != '_') out.push_back('_');
    }
    if (out.empty() || (out.front() >= '0' && out.front() <= '9')) out.insert(out.begin(), '_');
    return out;
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot hash file: " + path.string());
    std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(f), {});
    const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<std::uint8_t>(bit_len >> (i * 8)));
    std::array<std::uint32_t, 8> h = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (std::size_t off = 0; off < data.size(); off += 64) {
        std::uint32_t w[64]{};
        for (unsigned i = 0; i < 16; ++i) w[i] = load_be(data.data() + off + i * 4);
        for (unsigned i = 16; i < 64; ++i) {
            const auto s0 = rotate_right(w[i-15],7) ^ rotate_right(w[i-15],18) ^ (w[i-15] >> 3);
            const auto s1 = rotate_right(w[i-2],17) ^ rotate_right(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], ff=h[5], g=h[6], hh=h[7];
        for (unsigned i = 0; i < 64; ++i) {
            const auto s1 = rotate_right(e,6) ^ rotate_right(e,11) ^ rotate_right(e,25);
            const auto ch = (e & ff) ^ (~e & g);
            const auto t1 = hh + s1 + ch + K[i] + w[i];
            const auto s0 = rotate_right(a,2) ^ rotate_right(a,13) ^ rotate_right(a,22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + maj;
            hh=g; g=ff; ff=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=ff; h[6]+=g; h[7]+=hh;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto v : h) out << std::setw(8) << v;
    return out.str();
}
}
