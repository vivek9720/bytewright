#include "common/hexdump.hpp"

namespace bw {
namespace common {
namespace {

const char kHexDigits[] = "0123456789abcdef";

void append_hex_byte(std::string& out, std::uint8_t b) {
  out += kHexDigits[(b >> 4) & 0xF];
  out += kHexDigits[b & 0xF];
}

}  // namespace

std::string to_hex(std::uint64_t value, int min_digits) {
  char buf[16];
  int i = 0;
  if (value == 0) {
    buf[i++] = '0';
  } else {
    while (value > 0 && i < 16) {
      buf[i++] = kHexDigits[value & 0xF];
      value >>= 4;
    }
  }
  while (i < min_digits && i < 16) {
    buf[i++] = '0';
  }
  std::string out = "0x";
  while (i > 0) {
    out += buf[--i];
  }
  return out;
}

std::string escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c >= 0x20 && c < 0x7F) {
      out += static_cast<char>(c);
    } else {
      out += "\\x";
      append_hex_byte(out, c);
    }
  }
  return out;
}

std::string hexdump(const std::uint8_t* data, std::size_t size) {
  std::string out;
  for (std::size_t row = 0; row < size; row += 16) {
    out += to_hex(row, 8);
    out += "  ";
    std::string ascii;
    for (std::size_t col = 0; col < 16; ++col) {
      if (row + col < size) {
        std::uint8_t b = data[row + col];
        append_hex_byte(out, b);
        out += ' ';
        ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
      } else {
        out += "   ";
      }
    }
    out += ' ';
    out += ascii;
    out += '\n';
  }
  return out;
}

std::string hexdump(const std::vector<std::uint8_t>& data) {
  return hexdump(data.data(), data.size());
}

}  // namespace common
}  // namespace bw
