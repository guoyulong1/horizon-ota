// ota-verify：verify 验签库的命令行入口（与 fsd 同构：库 + 可执行程序，
// --self-test 作为 ctest 底线 verify_self_test）。
//
// 用法:
//   ota-verify --self-test
//       内置自检（离线）：生成 Ed25519 密钥对 → 签名样例清单 → 验签往返
//       （篡改/坏长度/错钥拒绝）→ 清单解析与适用性判断用例。退出码 0 为通过。
//   ota-verify check --manifest <manifest.json> --sig <manifest.json.sig>
//         --pubkey <64hex>
//       用给定公钥验证一份发布清单的签名（签名必须恰好 64 字节）。
//
// 退出码: 0 成功；1 验证失败或自检失败；2 用法/IO 错误。
#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "crypto.h"
#include "manifest.h"

namespace {

using EvpPkeyCtx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using EvpPkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpMdCtx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

constexpr char kUsage[] =
    "Usage:\n"
    "  ota-verify --self-test\n"
    "  ota-verify check --manifest <json> --sig <file> --pubkey <64hex>\n";

bool ReadFileBytes(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(in),
              std::istreambuf_iterator<char>());
  return true;
}

std::string ToHex(const std::string& raw) {
  static const char kDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(raw.size() * 2);
  for (const char byte : raw) {
    const unsigned char value = static_cast<unsigned char>(byte);
    hex.push_back(kDigits[value >> 4]);
    hex.push_back(kDigits[value & 0x0F]);
  }
  return hex;
}

// 生成 Ed25519 密钥对，输出原始公钥/私钥（各 32 字节）；失败返回 false。
bool GenerateEd25519Key(std::string* public_key_out,
                        std::string* private_key_out) {
  EvpPkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr),
                 EVP_PKEY_CTX_free);
  if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1) {
    return false;
  }
  EVP_PKEY* raw = nullptr;
  if (EVP_PKEY_keygen(ctx.get(), &raw) != 1 || raw == nullptr) {
    return false;
  }
  EvpPkey key(raw, EVP_PKEY_free);

  std::array<unsigned char, 32> public_buffer = {};
  std::size_t public_len = public_buffer.size();
  std::array<unsigned char, 32> private_buffer = {};
  std::size_t private_len = private_buffer.size();
  if (EVP_PKEY_get_raw_public_key(key.get(), public_buffer.data(),
                                  &public_len) != 1 ||
      public_len != public_buffer.size()) {
    return false;
  }
  if (EVP_PKEY_get_raw_private_key(key.get(), private_buffer.data(),
                                   &private_len) != 1 ||
      private_len != private_buffer.size()) {
    return false;
  }
  public_key_out->assign(reinterpret_cast<const char*>(public_buffer.data()),
                         public_buffer.size());
  private_key_out->assign(reinterpret_cast<const char*>(private_buffer.data()),
                          private_buffer.size());
  return true;
}

// 用原始私钥对 data 一次性签名（Ed25519：摘要参数必须为空，一次喂完）。
bool SignEd25519(const std::string& raw_private_key, const std::string& data,
                 std::string* signature_out) {
  if (raw_private_key.size() != 32) {
    return false;
  }
  EvpPkey key(
      EVP_PKEY_new_raw_private_key(
          EVP_PKEY_ED25519, nullptr,
          reinterpret_cast<const unsigned char*>(raw_private_key.data()),
          raw_private_key.size()),
      EVP_PKEY_free);
  if (!key) {
    return false;
  }
  EvpMdCtx ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr,
                                 key.get()) != 1) {
    return false;
  }
  std::array<unsigned char, 64> signature = {};
  std::size_t signature_len = signature.size();
  if (EVP_DigestSign(ctx.get(), signature.data(), &signature_len,
                     reinterpret_cast<const unsigned char*>(data.data()),
                     data.size()) != 1 ||
      signature_len != signature.size()) {
    return false;
  }
  signature_out->assign(reinterpret_cast<const char*>(signature.data()),
                        signature.size());
  return true;
}

// 与 docs/OTA.md 清单契约一致的最小样例清单。
std::string SampleManifestJson() {
  return R"({
  "schema_version": 1,
  "release_id": "self-test-0001",
  "version": "1.2.3",
  "version_code": 10203,
  "security_version": 5,
  "model": "demo-board",
  "hardware_compat": ["example,demo-board"],
  "created_at": "2026-01-01T00:00:00Z",
  "install": {"mode": "reboot-required", "layouts": ["ab"]},
  "components": [
    {
      "name": "kernel",
      "path": "files/Image",
      "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "size": 1024,
      "target": "boot"
    }
  ]
}
)";
}

// 一条自检断言：通过打印 ok，失败打印 FAILED 并计数。
bool Expect(bool condition, const char* name, int* failures) {
  if (condition) {
    std::cout << "ok       " << name << "\n";
  } else {
    std::cout << "FAILED   " << name << "\n";
    ++*failures;
  }
  return condition;
}

DeviceContext SelfTestDevice() {
  DeviceContext device;
  device.model = "demo-board";
  device.version_code = 10202;
  device.security_version_floor = 4;
  device.hardware_compat = {"example,demo-board"};
  device.layout = "ab";
  return device;
}

int RunSelfTest() {
  int failures = 0;

  std::string raw_public;
  std::string raw_private;
  Expect(GenerateEd25519Key(&raw_public, &raw_private),
         "generate ed25519 keypair", &failures);

  const std::string manifest = SampleManifestJson();
  std::string signature;
  if (Expect(SignEd25519(raw_private, manifest, &signature),
             "sign sample manifest", &failures)) {
    Expect(signature.size() == 64, "signature is 64 bytes", &failures);
    Expect(VerifyEd25519(raw_public, manifest, signature),
           "verify manifest signature", &failures);

    std::string tampered = manifest;
    tampered[0] = static_cast<char>(tampered[0] ^ 0x20);
    Expect(!VerifyEd25519(raw_public, tampered, signature),
           "reject tampered manifest", &failures);
    Expect(!VerifyEd25519(raw_public, manifest,
                          signature.substr(0, signature.size() - 1)),
           "reject short signature", &failures);
    Expect(!VerifyEd25519(std::string(32, '\0'), manifest, signature),
           "reject wrong public key", &failures);
  }

  const std::string public_hex = ToHex(raw_public);
  std::string parsed_public;
  Expect(ParseHexPublicKey(public_hex, &parsed_public) &&
             parsed_public == raw_public,
         "hex public key round trip", &failures);
  Expect(!ParseHexPublicKey(public_hex.substr(0, public_hex.size() - 2),
                            &parsed_public),
         "reject short hex public key", &failures);

  Manifest parsed;
  std::string error;
  if (Expect(ParseManifest(manifest, &parsed, &error),
             "parse sample manifest", &failures)) {
    Expect(parsed.release_id == "self-test-0001" &&
               parsed.version_code == 10203 &&
               parsed.security_version == 5 &&
               parsed.install_mode == "reboot-required" &&
               parsed.components.size() == 1U &&
               parsed.components[0].name == "kernel",
           "parsed manifest fields", &failures);
    Expect(CheckApplicability(parsed, SelfTestDevice()).accepted,
           "applicability accepted", &failures);

    DeviceContext wrong_model = SelfTestDevice();
    wrong_model.model = "other-board";
    Expect(CheckApplicability(parsed, wrong_model).reason ==
               RejectReason::kModelMismatch,
           "reject model mismatch", &failures);

    DeviceContext rollback = SelfTestDevice();
    rollback.security_version_floor = 6;
    Expect(CheckApplicability(parsed, rollback).reason ==
               RejectReason::kRollback,
           "reject rollback", &failures);

    DeviceContext not_newer = SelfTestDevice();
    not_newer.version_code = 10203;
    Expect(CheckApplicability(parsed, not_newer).reason ==
               RejectReason::kNotNewer,
           "reject not newer", &failures);

    DeviceContext wrong_layout = SelfTestDevice();
    wrong_layout.layout = "recovery";
    Expect(CheckApplicability(parsed, wrong_layout).reason ==
               RejectReason::kLayoutUnsupported,
           "reject unsupported layout", &failures);
  }

  Expect(!ParseManifest("{ not json", &parsed, &error),
         "reject broken manifest json", &failures);

  if (failures == 0) {
    std::cout << "ota-verify self-test: OK\n";
    return 0;
  }
  std::cout << "ota-verify self-test: " << failures << " check(s) FAILED\n";
  return 1;
}

int RunCheck(int argc, char** argv) {
  std::string manifest_path;
  std::string signature_path;
  std::string pubkey_hex;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--manifest" && i + 1 < argc) {
      manifest_path = argv[++i];
    } else if (arg == "--sig" && i + 1 < argc) {
      signature_path = argv[++i];
    } else if (arg == "--pubkey" && i + 1 < argc) {
      pubkey_hex = argv[++i];
    } else {
      std::cerr << kUsage;
      return 2;
    }
  }
  if (manifest_path.empty() || signature_path.empty() || pubkey_hex.empty()) {
    std::cerr << kUsage;
    return 2;
  }

  std::string manifest;
  std::string signature;
  if (!ReadFileBytes(manifest_path, &manifest) ||
      !ReadFileBytes(signature_path, &signature)) {
    std::cerr << "ota-verify: cannot read manifest or signature\n";
    return 2;
  }
  std::string raw_public;
  if (!ParseHexPublicKey(pubkey_hex, &raw_public)) {
    std::cerr << "ota-verify: --pubkey must be 64 hex digits\n";
    return 2;
  }
  if (signature.size() != 64) {
    std::cerr << "ota-verify: signature must be exactly 64 bytes, got "
              << signature.size() << "\n";
    return 1;
  }
  if (!VerifyEd25519(raw_public, manifest, signature)) {
    std::cerr << "ota-verify: manifest signature verification FAILED\n";
    return 1;
  }
  std::cout << "ota-verify: manifest signature OK\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--self-test") {
    return RunSelfTest();
  }
  if (argc >= 2 && std::string(argv[1]) == "check") {
    return RunCheck(argc - 1, argv + 1);
  }
  std::cerr << kUsage;
  return 2;
}
