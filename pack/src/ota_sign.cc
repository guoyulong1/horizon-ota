// ota-sign：用离线保管的 Ed25519 私钥对发布清单的原始字节签名。
// 只依赖 OpenSSL；不依赖 fsd。
//
// 用法:
//   ota-sign --manifest <manifest.json> --key <private.pem> --output <manifest.json.sig>
//   私钥也可用 OTA_PRIVATE_KEY 环境变量提供。
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "crypto.h"

namespace {

std::string ValueOf(int argc, char** argv, int* index, const char* flag) {
  if (*index + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires a value");
  }
  ++(*index);
  return argv[*index];
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path);
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string manifest;
    std::string key;
    std::string output;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--manifest") {
        manifest = ValueOf(argc, argv, &i, "--manifest");
      } else if (arg == "--key") {
        key = ValueOf(argc, argv, &i, "--key");
      } else if (arg == "--output") {
        output = ValueOf(argc, argv, &i, "--output");
      } else if (arg == "--help" || arg == "-h") {
        std::cout
            << "Usage: ota-sign --manifest <json> --key <pem> --output <sig>\n";
        return 0;
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }
    if (key.empty()) {
      const char* env = std::getenv("OTA_PRIVATE_KEY");
      if (env != nullptr) {
        key = env;
      }
    }
    if (manifest.empty() || key.empty() || output.empty()) {
      throw std::runtime_error(
          "need --manifest, --key (or OTA_PRIVATE_KEY), and --output");
    }

    const std::string data = ReadFile(manifest);
    const std::string signature = ota::SignEd25519File(key, data);
    if (signature.size() != 64) {
      throw std::runtime_error("Ed25519 signature must be 64 bytes");
    }

    std::ofstream out(output, std::ios::binary);
    if (!out) {
      throw std::runtime_error("cannot write " + output);
    }
    out.write(signature.data(), static_cast<std::streamsize>(signature.size()));
    if (!out) {
      throw std::runtime_error("write failed: " + output);
    }
    std::cout << "signed: " << output << " (" << signature.size() << " bytes)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ota-sign: " << error.what() << "\n";
    return 1;
  }
}
