// verify 加密库单元测试：公钥解析与 Ed25519 验签（签名用 OpenSSL 现场生成）。
#include <openssl/evp.h>

#include <cstddef>
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

bool GenerateAndSign(const std::string& message, std::string* raw_public_key,
                     std::string* signature) {
  EVP_PKEY_CTX* keygen = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (keygen == nullptr) {
    return false;
  }
  EVP_PKEY* key = nullptr;
  bool ok = EVP_PKEY_keygen_init(keygen) == 1 &&
            EVP_PKEY_keygen(keygen, &key) == 1;
  EVP_PKEY_CTX_free(keygen);
  if (!ok) {
    return false;
  }

  raw_public_key->resize(32);
  std::size_t key_len = 32;
  ok = EVP_PKEY_get_raw_public_key(
           key, reinterpret_cast<unsigned char*>(raw_public_key->data()),
           &key_len) == 1 &&
       key_len == 32;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  signature->resize(64);
  std::size_t sig_len = 64;
  ok = ok && ctx != nullptr &&
       EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
       EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(signature->data()),
                      &sig_len,
                      reinterpret_cast<const unsigned char*>(message.data()),
                      message.size()) == 1 &&
       sig_len == 64;
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok;
}
}  // namespace

int main() {
  std::string raw;
  Check(ota::ParseHexPublicKey(std::string(64, '0'), &raw) && raw.size() == 32,
        "parse valid hex");
  Check(!ota::ParseHexPublicKey("zz", &raw), "parse invalid hex");
  Check(!ota::ParseHexPublicKey(std::string(63, '0'), &raw),
        "parse wrong length");

  std::string public_key;
  std::string signature;
  Check(GenerateAndSign("hello", &public_key, &signature), "generate+sign");
  Check(ota::VerifyEd25519(public_key, "hello", signature), "verify valid");
  Check(!ota::VerifyEd25519(public_key, "hellp", signature),
        "verify tampered message");
  std::string bad_signature = signature;
  bad_signature[0] = static_cast<char>(bad_signature[0] ^ 0xff);
  Check(!ota::VerifyEd25519(public_key, "hello", bad_signature),
        "verify tampered signature");

  std::cout << (g_failures == 0 ? "RESULT: PASS" : "RESULT: FAIL") << "\n";
  return g_failures == 0 ? 0 : 1;
}
