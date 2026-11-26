// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// sha256.cc：固件包完整性摘要。向量覆盖 FIPS 180-4 标准用例、跨读缓冲
// 边界的分块写入、规范化与常量时间比较，以及文件哈希的 IO 错误路径。
// 全部期望摘要在目标板上用 sha256sum 独立实测核对过。

#include "fsd/sha256.h"

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

constexpr std::string_view kEmptyDigest =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr std::string_view kAbcDigest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
// FIPS 180-4 的两块消息（448 位）向量。
constexpr std::string_view kTwoBlockMessage =
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
constexpr std::string_view kTwoBlockDigest =
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
// 百万个 'a' 的 NIST 向量，覆盖多块级联。
constexpr std::string_view kMillionADigest =
    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
// 100000 个 'x'，跨过 64 KiB 读缓冲边界。sha256sum 实测。
constexpr std::string_view kBufferBoundaryDigest =
    "d69e68988157833272305aaf21f453c800346e8a3640db6578e260215542e5d4";

TEST(Sha256Hasher, MatchesFips180_4Vectors) {
  Sha256Hasher empty_hasher;
  ASSERT_TRUE(empty_hasher.ok());
  empty_hasher.Update({});
  EXPECT_EQ(empty_hasher.FinalizeHex(), kEmptyDigest);

  Sha256Hasher abc_hasher;
  ASSERT_TRUE(abc_hasher.ok());
  abc_hasher.Update(AsBytes("abc"));
  EXPECT_EQ(abc_hasher.FinalizeHex(), kAbcDigest);

  Sha256Hasher two_block_hasher;
  ASSERT_TRUE(two_block_hasher.ok());
  two_block_hasher.Update(AsBytes(kTwoBlockMessage));
  EXPECT_EQ(two_block_hasher.FinalizeHex(), kTwoBlockDigest);
}

TEST(Sha256Hasher, MatchesMillionAVector) {
  const std::string payload(1000000, 'a');
  Sha256Hasher hasher;
  ASSERT_TRUE(hasher.ok());
  hasher.Update(AsBytes(payload));
  EXPECT_EQ(hasher.FinalizeHex(), kMillionADigest);
}

TEST(Sha256Hasher, ChunkedUpdatesMatchSingleUpdate) {
  // 70000 字节、997 字节一组：覆盖 Update 的多次调用与缓冲拼接。
  const std::string payload(70000, 'x');
  Sha256Hasher whole;
  ASSERT_TRUE(whole.ok());
  whole.Update(AsBytes(payload));
  const std::string whole_hex = whole.FinalizeHex();
  EXPECT_EQ(whole_hex.size(), kSha256HexLength);

  Sha256Hasher chunked;
  ASSERT_TRUE(chunked.ok());
  for (std::size_t offset = 0; offset < payload.size(); offset += 997) {
    chunked.Update(AsBytes(std::string_view(payload).substr(offset, 997)));
  }
  EXPECT_EQ(chunked.FinalizeHex(), whole_hex);
}

TEST(Sha256Hasher, IgnoresEmptyUpdates) {
  Sha256Hasher hasher;
  ASSERT_TRUE(hasher.ok());
  hasher.Update({});
  hasher.Update(AsBytes(""));
  hasher.Update(AsBytes("abc"));
  EXPECT_EQ(hasher.FinalizeHex(), kAbcDigest);
}

TEST(Sha256Hasher, FinalizeIsOneShot) {
  Sha256Hasher hasher;
  ASSERT_TRUE(hasher.ok());
  hasher.Update(AsBytes("abc"));
  EXPECT_EQ(hasher.FinalizeHex(), kAbcDigest);
  // 每个实例只能收尾一次：收尾后实例失效，再次收尾返回空串。
  EXPECT_FALSE(hasher.ok());
  EXPECT_EQ(hasher.FinalizeHex(), "");
}

TEST(NormalizeSha256Hex, LowercasesUppercaseInput) {
  EXPECT_EQ(*NormalizeSha256Hex(
                "BA7816BF8F01CFEA414140DE5DAE2223"
                "B00361A396177A9CB410FF61F20015AD"),
            kAbcDigest);
  EXPECT_EQ(*NormalizeSha256Hex(std::string(64, 'A')),
            std::string(64, 'a'));
  // 已经是小写时原样返回。
  EXPECT_EQ(*NormalizeSha256Hex(kAbcDigest), kAbcDigest);
}

TEST(NormalizeSha256Hex, RejectsWrongLengthAndNonHex) {
  EXPECT_EQ(NormalizeSha256Hex(""), std::nullopt);
  EXPECT_EQ(NormalizeSha256Hex("abc"), std::nullopt);
  EXPECT_EQ(NormalizeSha256Hex(std::string(63, 'a')), std::nullopt);
  EXPECT_EQ(NormalizeSha256Hex(std::string(65, 'a')), std::nullopt);
  EXPECT_EQ(NormalizeSha256Hex(std::string(63, 'a') + "g"), std::nullopt);
  EXPECT_EQ(NormalizeSha256Hex(std::string(64, ' ')), std::nullopt);
}

TEST(IsSha256Hex, MirrorsNormalize) {
  EXPECT_TRUE(IsSha256Hex(kAbcDigest));
  EXPECT_TRUE(IsSha256Hex(std::string(64, 'F')));
  EXPECT_FALSE(IsSha256Hex(std::string(64, 'g')));
  EXPECT_FALSE(IsSha256Hex("abc"));
  EXPECT_FALSE(IsSha256Hex(""));
}

TEST(DigestsEqual, ComparesCaseInsensitively) {
  EXPECT_TRUE(DigestsEqual(
      kAbcDigest,
      "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
}

TEST(DigestsEqual, DetectsAnyDifference) {
  std::string modified(kAbcDigest);
  modified[0] = modified[0] == 'b' ? 'c' : 'b';
  EXPECT_FALSE(DigestsEqual(modified, kAbcDigest));
}

TEST(DigestsEqual, RejectsInvalidInput) {
  EXPECT_FALSE(DigestsEqual("abc", kAbcDigest));
  EXPECT_FALSE(DigestsEqual(kAbcDigest, "abc"));
  EXPECT_FALSE(DigestsEqual(std::string(64, 'g'), kAbcDigest));
  EXPECT_FALSE(DigestsEqual("", ""));
}

TEST(HashFile, HashesFileContent) {
  const std::filesystem::path path = WriteTempFile("abc");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const Sha256Digest digest = HashFile(path);
  ASSERT_TRUE(digest.status.ok()) << digest.status.message();
  EXPECT_EQ(digest.hex, kAbcDigest);
}

TEST(HashFile, HashesEmptyFileLikeEmptyString) {
  const std::filesystem::path path = WriteTempFile("");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const Sha256Digest digest = HashFile(path);
  ASSERT_TRUE(digest.status.ok()) << digest.status.message();
  EXPECT_EQ(digest.hex, kEmptyDigest);
}

TEST(HashFile, HashesBeyondReadBufferBoundary) {
  const std::string payload(100000, 'x');  // 跨过 64 KiB 读缓冲
  const std::filesystem::path path = WriteTempFile(payload);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const Sha256Digest digest = HashFile(path);
  ASSERT_TRUE(digest.status.ok()) << digest.status.message();
  EXPECT_EQ(digest.hex, kBufferBoundaryDigest);
}

TEST(HashFile, ReportsMissingFileAsIoError) {
  const Sha256Digest digest =
      HashFile(std::filesystem::path("/nonexistent") / "fw.bin");
  EXPECT_FALSE(digest.status.ok());
  EXPECT_EQ(digest.status.code(), ErrorCode::kIo);
  EXPECT_TRUE(digest.hex.empty());
}

}  // namespace
}  // namespace fsd
