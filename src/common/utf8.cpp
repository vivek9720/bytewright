#include "common/utf8.hpp"

namespace bw {
namespace common {
namespace utf8 {

std::size_t decode(const std::uint8_t* data, std::size_t size,
                   std::uint32_t& cp) noexcept {
  if (size == 0) {
    return 0;
  }
  const std::uint8_t b0 = data[0];

  // 1-byte: 0xxxxxxx
  if (b0 < 0x80) {
    cp = b0;
    return 1;
  }

  // Determine the sequence length and the initial bits from the lead byte.
  std::size_t len;
  std::uint32_t value;
  std::uint32_t min_value;  // smallest value legal for this length (overlong gate)
  if ((b0 & 0xE0) == 0xC0) {
    len = 2;
    value = b0 & 0x1Fu;
    min_value = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    len = 3;
    value = b0 & 0x0Fu;
    min_value = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    len = 4;
    value = b0 & 0x07u;
    min_value = 0x10000;
  } else {
    return 0;  // 0x80..0xBF continuation as lead, or 0xF8+ - invalid
  }

  if (size < len) {
    return 0;  // truncated multi-byte sequence
  }

  for (std::size_t i = 1; i < len; ++i) {
    const std::uint8_t bi = data[i];
    if ((bi & 0xC0) != 0x80) {
      return 0;  // not a continuation byte
    }
    value = (value << 6) | (bi & 0x3Fu);
  }

  // Reject overlong encodings, UTF-16 surrogates, and out-of-range scalars.
  if (value < min_value) {
    return 0;
  }
  if (value >= 0xD800 && value <= 0xDFFF) {
    return 0;
  }
  if (value > kMaxCodepoint) {
    return 0;
  }

  cp = value;
  return len;
}

bool validate(const std::uint8_t* data, std::size_t size) noexcept {
  std::size_t i = 0;
  while (i < size) {
    std::uint32_t cp;
    const std::size_t n = decode(data + i, size - i, cp);
    if (n == 0) {
      return false;
    }
    i += n;
  }
  return true;
}

bool validate(const std::string& s) noexcept {
  return validate(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

bool count_codepoints(const std::uint8_t* data, std::size_t size,
                      std::size_t& out) noexcept {
  std::size_t i = 0;
  std::size_t n_cp = 0;
  while (i < size) {
    std::uint32_t cp;
    const std::size_t n = decode(data + i, size - i, cp);
    if (n == 0) {
      return false;
    }
    i += n;
    ++n_cp;
  }
  out = n_cp;
  return true;
}

bool encode(std::uint32_t cp, std::string& out) {
  if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > kMaxCodepoint) {
    return false;
  }
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return true;
}

bool to_codepoints(const std::string& s, std::vector<std::uint32_t>& out) {
  const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(s.data());
  std::size_t i = 0;
  while (i < s.size()) {
    std::uint32_t cp;
    const std::size_t n = decode(data + i, s.size() - i, cp);
    if (n == 0) {
      return false;
    }
    out.push_back(cp);
    i += n;
  }
  return true;
}

}  // namespace utf8
}  // namespace common
}  // namespace bw
