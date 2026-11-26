// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// pack 加密库单元测试（GTest）：SHA-256 已知向量、文件哈希边界与
// Ed25519 签名往返。签名用 OpenSSL 现场生成密钥，验签在测试内独立
// 用 EVP 完成，不引用 verify 模块（保持 pack 的独立性）。

#include "crypto.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "fsd_test_util.h"

namespace ota {
namespace {

using fsd::testing::TempPathGuard;
using fsd::testing::WriteTempFile;

constexpr std::string_view kEmptyDigest =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr std::string_view kAbcDigest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
// FIPS 180-4 的两块消息（448 位）向量，sha256sum 实测核对。
constexpr std::string_view kTwoBlockMessage =
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
constexpr std::string_view kTwoBlockDigest =
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
// 100000 个 'x'：跨过 64 KiB 读缓冲边界，sha256sum 实测核对。
constexpr std::string_view kBufferBoundaryDigest =
    "d69e68988157833272305aaf21f453c800346e8a3640db6578e260215542e5d4";

struct KeyMaterial {
  std::string pem;
  std::string raw_public_key;
};

// 现场生成 Ed25519 密钥：给出 PKCS#8 PEM 与 32 字节原始公钥。
bool GenerateEd25519Key(KeyMaterial* out) {
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
  const bool got_public_key =
      EVP_PKEY_get_raw_public_key(
          key, reinterpret_cast<unsigned char*>(out->raw_public_key.data()),
          &key_len) == 1 &&
      key_len == 32;

  BIO* bio = BIO_new(BIO_s_mem());
  const bool wrote_pem =
      bio != nullptr &&
      PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr,
                               nullptr) == 1;
  if (wrote_pem) {
    char* data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    out->pem.assign(data, static_cast<std::size_t>(size));
  }
  BIO_free(bio);
  EVP_PKEY_free(key);
  return got_public_key && wrote_pem;
}

// 用原始公钥在测试内独立验签（OpenSSL EVP）。
bool VerifyWithRawKey(const std::string& raw_public_key,
                      const std::string& data, const std::string& signature) {
  EVP_PKEY* key = EVP_PKEY_new_raw_public_key(
      EVP_PKEY_ED25519, nullptr,
      reinterpret_cast<const unsigned char*>(raw_public_key.data()),
      raw_public_key.size());
  if (key == nullptr) {
    return false;
  }
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  const bool ok =
      ctx != nullptr &&
      EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
      EVP_DigestVerify(ctx,
                       reinterpret_cast<const unsigned char*>(
                           signature.data()),
                       signature.size(),
                       reinterpret_cast<const unsigned char*>(data.data()),
                       data.size()) == 1;
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok;
}

TEST(Sha256Hex, MatchesKnownVectors) {
  EXPECT_EQ(Sha256Hex(""), kEmptyDigest);
  EXPECT_EQ(Sha256Hex("abc"), kAbcDigest);
  EXPECT_EQ(Sha256Hex(std::string(kTwoBlockMessage)), kTwoBlockDigest);
}

TEST(Sha256Hex, HashesBinaryDataWithEmbeddedNul) {
  // 嵌入的 '\0' 不得截断：内存路径与文件流式路径互证。
  const std::string binary = std::string("a\0b", 3);
  const std::filesystem::path path = WriteTempFile(binary);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  EXPECT_EQ(Sha256Hex(binary), Sha256FileHex(path.string()));
  EXPECT_EQ(Sha256Hex(binary).size(), 64U);
}

TEST(Sha256FileHex, MatchesSha256HexOnSameContent) {
  const std::filesystem::path path = WriteTempFile("abc");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  EXPECT_EQ(Sha256FileHex(path.string()), kAbcDigest);
}

TEST(Sha256FileHex, HashesEmptyFileLikeEmptyString) {
  const std::filesystem::path path = WriteTempFile("");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  EXPECT_EQ(Sha256FileHex(path.string()), kEmptyDigest);
}

TEST(Sha256FileHex, HashesBeyondReadBufferBoundary) {
  const std::string payload(100000, 'x');  // 跨过 64 KiB 读缓冲
  const std::filesystem::path path = WriteTempFile(payload);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  EXPECT_EQ(Sha256FileHex(path.string()), kBufferBoundaryDigest);
}

TEST(Sha256FileHex, ThrowsOnMissingFile) {
  EXPECT_THROW(Sha256FileHex("/nonexistent/payload.bin"), std::runtime_error);
}

TEST(SignEd25519, ProducesVerifiableSignature) {
  KeyMaterial key;
  ASSERT_TRUE(GenerateEd25519Key(&key));

  const std::string signature = SignEd25519(key.pem, "hello");
  EXPECT_EQ(signature.size(), 64U);
  EXPECT_TRUE(VerifyWithRawKey(key.raw_public_key, "hello", signature));
  EXPECT_FALSE(VerifyWithRawKey(key.raw_public_key, "hellp", signature));
}

TEST(SignEd25519, ThrowsOnInvalidPem) {
  EXPECT_THROW(SignEd25519("this is not a pem", "hello"), std::runtime_error);
}

TEST(SignEd25519File, SignsFromKeyFile) {
  KeyMaterial key;
  ASSERT_TRUE(GenerateEd25519Key(&key));

  const std::filesystem::path path = WriteTempFile(key.pem);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);

  const std::string signature = SignEd25519File(path.string(), "hello");
  EXPECT_EQ(signature.size(), 64U);
  EXPECT_TRUE(VerifyWithRawKey(key.raw_public_key, "hello", signature));
}

TEST(SignEd25519File, ThrowsOnMissingKeyFile) {
  EXPECT_THROW(SignEd25519File("/nonexistent/signing-key.pem", "hello"),
               std::runtime_error);
}

}  // namespace
}  // namespace ota
