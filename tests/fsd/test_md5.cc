// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// md5.cc：可选的 MD5 完整性校验。向量取自 RFC 1321 §A.5 官方测试套件，
// 并覆盖规范化、比较与文件哈希的 IO 错误路径。

#include "fsd/md5.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "fsd/status.h"
#include "fsd_test_util.h"

namespace fsd {
namespace {

using fsd::testing::AsBytes;
using fsd::testing::TempPathGuard;
using fsd::testing::WriteTempFile;

constexpr std::string_view kEmptyDigest = "d41d8cd98f00b204e9800998ecf8427e";

struct Rfc1321Vector {
  std::string_view message;
  std::string_view digest;
};

// RFC 1321 A.5 测试套件的七条向量。
constexpr Rfc1321Vector kRfc1321Vectors[] = {
    {"", "d41d8cd98f00b204e9800998ecf8427e"},
    {"a", "0cc175b9c0f1b6a831c399e269772661"},
    {"abc", "900150983cd24fb0d6963f7d28e17f72"},
    {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
    {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
    {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
     "d174ab98d277d9f5a5611c2c9f419d9f"},
    {"123456789012345678901234567890123456789012345678901234567890"
     "12345678901234567890",
     "57edf4a22be3c955ac49da2e2107b67a"},
};

TEST(Md5Hasher, MatchesRfc1321TestSuite) {
  for (const Rfc1321Vector& vector : kRfc1321Vectors) {
    Md5Hasher hasher;
    ASSERT_TRUE(hasher.ok());
    hasher.Update(AsBytes(vector.message));
    EXPECT_EQ(hasher.FinalizeHex(), vector.digest)
        << "message: " << vector.message;
  }
}

TEST(Md5Hasher, ChunkedUpdatesMatchSingleUpdate) {
  const std::string payload(70000, 'y');
  Md5Hasher whole;
  ASSERT_TRUE(whole.ok());
  whole.Update(AsBytes(payload));
  const std::string whole_hex = whole.FinalizeHex();
  EXPECT_EQ(whole_hex.size(), kMd5HexLength);

  Md5Hasher chunked;
  ASSERT_TRUE(chunked.ok());
  for (std::size_t offset = 0; offset < payload.size(); offset += 991) {
    chunked.Update(AsBytes(std::string_view(payload).substr(offset, 991)));
  }
  EXPECT_EQ(chunked.FinalizeHex(), whole_hex);
}

TEST(Md5Hasher, FinalizeIsOneShot) {
  Md5Hasher hasher;
  ASSERT_TRUE(hasher.ok());
  hasher.Update(AsBytes("abc"));
  EXPECT_EQ(hasher.FinalizeHex(), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_FALSE(hasher.ok());
  EXPECT_EQ(hasher.FinalizeHex(), "");
}

TEST(NormalizeMd5Hex, LowercasesUppercaseInput) {
  EXPECT_EQ(*NormalizeMd5Hex("D41D8CD98F00B204E9800998ECF8427E"), kEmptyDigest);
  EXPECT_EQ(*NormalizeMd5Hex(std::string(32, 'A')), std::string(32, 'a'));
  EXPECT_EQ(*NormalizeMd5Hex(kEmptyDigest), kEmptyDigest);
}

TEST(NormalizeMd5Hex, RejectsWrongLengthAndNonHex) {
  EXPECT_EQ(NormalizeMd5Hex(""), std::nullopt);
  EXPECT_EQ(NormalizeMd5Hex("abc"), std::nullopt);
  EXPECT_EQ(NormalizeMd5Hex(std::string(31, 'a')), std::nullopt);
  EXPECT_EQ(NormalizeMd5Hex(std::string(33, 'a')), std::nullopt);
  EXPECT_EQ(NormalizeMd5Hex(std::string(31, 'a') + "g"), std::nullopt);
}

TEST(IsMd5Hex, MirrorsNormalize) {
  EXPECT_TRUE(IsMd5Hex(kEmptyDigest));
  EXPECT_TRUE(IsMd5Hex(std::string(32, 'F')));
  EXPECT_FALSE(IsMd5Hex(std::string(32, 'g')));
  EXPECT_FALSE(IsMd5Hex("abc"));
  EXPECT_FALSE(IsMd5Hex(""));
}

TEST(Md5DigestsEqual, ComparesCaseInsensitively) {
  EXPECT_TRUE(Md5DigestsEqual(
      kEmptyDigest,
      "D41D8CD98F00B204E9800998ECF8427E"));
}

TEST(Md5DigestsEqual, DetectsAnyDifference) {
  std::string modified(kEmptyDigest);
  modified[0] = modified[0] == 'd' ? 'e' : 'd';
  EXPECT_FALSE(Md5DigestsEqual(modified, kEmptyDigest));
}

TEST(Md5DigestsEqual, RejectsInvalidInput) {
  EXPECT_FALSE(Md5DigestsEqual("abc", kEmptyDigest));
  EXPECT_FALSE(Md5DigestsEqual(kEmptyDigest, "abc"));
  EXPECT_FALSE(Md5DigestsEqual(std::string(32, 'g'), kEmptyDigest));
  EXPECT_FALSE(Md5DigestsEqual("", ""));
}

TEST(HashFileMd5, HashesFileContent) {
  const std::filesystem::path path = WriteTempFile("abc");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const Md5Digest digest = HashFileMd5(path);
  ASSERT_TRUE(digest.status.ok()) << digest.status.message();
  EXPECT_EQ(digest.hex, "900150983cd24fb0d6963f7d28e17f72");
}

TEST(HashFileMd5, HashesEmptyFileLikeEmptyString) {
  const std::filesystem::path path = WriteTempFile("");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const Md5Digest digest = HashFileMd5(path);
  ASSERT_TRUE(digest.status.ok()) << digest.status.message();
  EXPECT_EQ(digest.hex, kEmptyDigest);
}

TEST(HashFileMd5, HashesBeyondReadBufferBoundary) {
  const std::string payload(100000, 'z');  // 跨过 64 KiB 读缓冲
  const std::filesystem::path path = WriteTempFile(payload);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const std::string in_memory = [] {
    Md5Hasher hasher;
    hasher.Update(AsBytes(std::string(100000, 'z')));
    return hasher.FinalizeHex();
  }();
  const Md5Digest digest = HashFileMd5(path);
  ASSERT_TRUE(digest.status.ok()) << digest.status.message();
  // 文件路径与内存路径两条实现互相印证。
  EXPECT_EQ(digest.hex, in_memory);
  EXPECT_EQ(digest.hex.size(), kMd5HexLength);
}

TEST(HashFileMd5, ReportsMissingFileAsIoError) {
  const Md5Digest digest =
      HashFileMd5(std::filesystem::path("/nonexistent") / "fw.bin");
  EXPECT_FALSE(digest.status.ok());
  EXPECT_EQ(digest.status.code(), ErrorCode::kIo);
  EXPECT_TRUE(digest.hex.empty());
}

}  // namespace
}  // namespace fsd
