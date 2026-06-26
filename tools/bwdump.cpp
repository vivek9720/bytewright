// bwdump - decode a bytewright container and print a human-readable summary.
//
// Usage:
//   bwdump <format> <file>
//   bwdump <format> -        (read from stdin)
//
// <format> is one of: binpack | minidb | cfgscript | streamcodec
//
// The tool is a thin shell over each subproject's public facade. It performs no
// network access, takes no interactive input, and uses only the path provided
// on the command line.
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "binpack/binpack.hpp"
#include "cfgscript/cfgscript.hpp"
#include "common/status.hpp"
#include "minidb/minidb.hpp"
#include "streamcodec/streamcodec.hpp"

namespace {

std::vector<std::uint8_t> read_all(std::istream& in) {
  std::vector<std::uint8_t> data;
  char buf[4096];
  while (in) {
    in.read(buf, sizeof(buf));
    std::streamsize got = in.gcount();
    if (got > 0) {
      data.insert(data.end(), buf, buf + got);
    }
  }
  return data;
}

int usage() {
  std::cerr << "usage: bwdump <binpack|minidb|cfgscript|streamcodec> <file|->\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return usage();
  }
  std::string format = argv[1];
  std::string path = argv[2];

  std::vector<std::uint8_t> data;
  if (path == "-") {
    data = read_all(std::cin);
  } else {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
      std::cerr << "error: cannot open " << path << "\n";
      return 1;
    }
    std::uint8_t chunk[4096];
    std::size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), fp)) > 0) {
      data.insert(data.end(), chunk, chunk + got);
    }
    std::fclose(fp);
  }

  try {
    if (format == "binpack") {
      auto container = bw::binpack::parse(data.data(), data.size());
      std::cout << bw::binpack::summarize(container);
    } else if (format == "minidb") {
      auto db = bw::minidb::open(data.data(), data.size());
      std::cout << bw::minidb::summarize(db);
    } else if (format == "cfgscript") {
      auto doc = bw::cfgscript::parse(data.data(), data.size());
      std::cout << bw::cfgscript::summarize(doc);
    } else if (format == "streamcodec") {
      auto result = bw::streamcodec::decode(data.data(), data.size());
      std::cout << bw::streamcodec::summarize(result);
    } else {
      return usage();
    }
  } catch (const bw::common::ParseError& e) {
    std::cerr << "decode error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
