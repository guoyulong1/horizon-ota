// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// verify 加密库单元测试（GTest）：公钥十六进制解析与 Ed25519 验签
// （签名与密钥用 OpenSSL 现场生成）。

#include "crypto.h"

#include <openssl/evp.h>

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

namespace ota {
namespace {

struct KeyMaterial {
  std::string raw_public_key;
  std::string signature;
};

bool GenerateAndSign(const std::string& message, KeyMaterial* out) {
  EVP_PKEY_CTX* keygen = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (keygen == nullptr) {
    return false;
  }
  EVP_PKEY* key = nullptr;
  const bool generated =
      EVP_PKEY_keygen_init(keygen) == 1 && EVP_PKEY_keygen(keygen, &key) == 1;
  EVP_PKEY_CTX_free(keygen);
  if (!generated) {
    return false;
  }

  out->raw_public_key.resize(32);
  std::size_t key_len = 32;
  bool ok =
      EVP_PKEY_get_raw_public_key(
          key, reinterpret_cast<unsigned char*>(out->raw_public_key.data()),
          &key_len) == 1 &&
      key_len == 32;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  out->signature.resize(64);
  std::size_t sig_len = 64;
  ok = ok && ctx != nullptr &&
       EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
       EVP_DigestSign(ctx,
                      reinterpret_cast<unsigned char*>(out->signature.data()),
                      &sig_len,
                      reinterpret_cast<const unsigned char*>(message.data()),
                      message.size()) == 1 &&
       sig_len == 64;
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok;
}

TEST(ParseHexPublicKey, Accepts64HexDigitsCaseInsensitively) {
  // 0x00..0x1f 递增序列：小写与大写都应解析出相同原始字节。
  std::string hex_lower;
  for (int value = 0; value < 32; ++value) {
    static const char kDigits[] = "0123456789abcdef";
    hex_lower.push_back(kDigits[(value >> 4) & 0x0f]);
    hex_lower.push_back(kDigits[value & 0x0f]);
  }
  std::string hex_upper = hex_lower;
  for (char& ch : hex_upper) {
    if (ch >= 'a' && ch <= 'f') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }

  std::string raw;
  ASSERT_TRUE(ParseHexPublicKey(hex_lower, &raw));
  ASSERT_EQ(raw.size(), 32U);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    EXPECT_EQ(static_cast<unsigned char>(raw[i]), i) << "byte " << i;
  }

  std::string raw_upper;
  ASSERT_TRUE(ParseHexPublicKey(hex_upper, &raw_upper));
  EXPECT_EQ(raw_upper, raw);
}

TEST(ParseHexPublicKey, RejectsBadInput) {
  std::string raw;
  EXPECT_FALSE(ParseHexPublicKey("", &raw));
  EXPECT_FALSE(ParseHexPublicKey("zz", &raw));
  EXPECT_FALSE(ParseHexPublicKey(std::string(63, '0'), &raw));
  EXPECT_FALSE(ParseHexPublicKey(std::string(65, '0'), &raw));
  EXPECT_FALSE(ParseHexPublicKey(std::string(64, 'g'), &raw));
}

TEST(VerifyEd25519, VerifiesSignatureGeneratedInProcess) {
  KeyMaterial key;
  ASSERT_TRUE(GenerateAndSign("hello", &key));
  EXPECT_TRUE(VerifyEd25519(key.raw_public_key, "hello", key.signature));
}

TEST(VerifyEd25519, RejectsTamperedMessageAndSignature) {
  KeyMaterial key;
  ASSERT_TRUE(GenerateAndSign("hello", &key));
  EXPECT_FALSE(VerifyEd25519(key.raw_public_key, "hellp", key.signature));

  std::string bad_signature = key.signature;
  bad_signature[0] = static_cast<char>(bad_signature[0] ^ 0xff);
  EXPECT_FALSE(VerifyEd25519(key.raw_public_key, "hello", bad_signature));
}

TEST(VerifyEd25519, RejectsSignatureFromAnotherKey) {
  KeyMaterial first;
  KeyMaterial second;
  ASSERT_TRUE(GenerateAndSign("hello", &first));
  ASSERT_TRUE(GenerateAndSign("hello", &second));
  EXPECT_FALSE(
      VerifyEd25519(second.raw_public_key, "hello", first.signature));
}

TEST(VerifyEd25519, RejectsWrongSizes) {
  KeyMaterial key;
  ASSERT_TRUE(GenerateAndSign("hello", &key));

  std::string short_key = key.raw_public_key;
  short_key.resize(31);
  EXPECT_FALSE(VerifyEd25519(short_key, "hello", key.signature));

  std::string short_signature = key.signature;
  short_signature.resize(63);
  EXPECT_FALSE(VerifyEd25519(key.raw_public_key, "hello", short_signature));
}

}  // namespace
}  // namespace ota
