// pack 加密库单元测试：SHA-256 已知向量与文件哈希。
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "crypto.h"

namespace {
int g_failures = 0;

void Check(bool ok, const std::string& name) {
  if (ok) {
    std::cout << "PASS  " << name << "\n";
  } else {
    std::cout << "FAIL  " << name << "\n";
    ++g_failures;
  }
}
}  // namespace

int main() {
  Check(ota::Sha256Hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 empty");
  Check(ota::Sha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 abc");

  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "ota_pack_crypto_test.bin";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << "abc";
  }
  Check(ota::Sha256FileHex(tmp.string()) == ota::Sha256Hex("abc"),
        "sha256 file");
  std::error_code ignored;
  std::filesystem::remove(tmp, ignored);

  std::cout << (g_failures == 0 ? "RESULT: PASS" : "RESULT: FAIL") << "\n";
  return g_failures == 0 ? 0 : 1;
}
