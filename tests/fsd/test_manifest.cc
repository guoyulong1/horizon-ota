// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// manifest.cc：按行 key=value 的固件清单解析。覆盖完整解析、BOM/CRLF、
// 大小写摘要规范化、必填字段、未知/重复字段、非法数值，以及文件级
// 限制（64 KiB 上限、200 行上限、超长行）。

#include "fsd/manifest.h"

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

using fsd::testing::MakeTempDir;
using fsd::testing::TempPathGuard;
using fsd::testing::WriteTempFile;

constexpr std::string_view kSha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr std::string_view kUrl = "https://example.com/firmware.bin";

struct Loaded {
  Status status;
  FirmwareManifest manifest;
};

// 把清单内容写进临时文件再走 LoadManifest，结束自动清理。
Loaded LoadFromContent(std::string_view content) {
  Loaded loaded;
  const std::filesystem::path path = WriteTempFile(content);
  if (path.empty()) {
    loaded.status = Status::Failure(ErrorCode::kIo,
                                    "cannot write the manifest fixture");
    return loaded;
  }
  TempPathGuard guard(path);
  loaded.status = LoadManifest(path, &loaded.manifest);
  return loaded;
}

std::string ValidBody() {
  return "url=" + std::string(kUrl) + "\nsha256=" + std::string(kSha256) +
         "\noutput=/tmp/fsd-gtest-fw.bin\n";
}

TEST(LoadManifest, ParsesCompleteManifest) {
  const std::string content =
      "# firmware manifest\n"
      "\n"
      "name = fw-demo\n"
      "url=https://example.com/firmware.bin\n"
      "sha256=BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\n"
      "size=3\n"
      "max_bytes=1048576\n"
      "output=/tmp/fsd-gtest-fw.bin\n";
  const Loaded loaded = LoadFromContent(content);
  ASSERT_TRUE(loaded.status.ok()) << loaded.status.message();
  EXPECT_EQ(loaded.manifest.name, "fw-demo");
  EXPECT_EQ(loaded.manifest.url, kUrl);
  // 摘要读入后规范成小写。
  EXPECT_EQ(loaded.manifest.sha256_hex, kSha256);
  EXPECT_TRUE(loaded.manifest.md5_hex.empty());
  EXPECT_EQ(loaded.manifest.output_path, "/tmp/fsd-gtest-fw.bin");
  EXPECT_EQ(loaded.manifest.size_bytes, std::optional<std::uint64_t>(3));
  EXPECT_EQ(loaded.manifest.max_bytes,
            std::optional<std::uint64_t>(1048576));
}

TEST(LoadManifest, AcceptsUtf8BomAndCrlfLines) {
  const std::string content =
      "\xEF\xBB\xBFurl=" + std::string(kUrl) + "\r\nsha256=" +
      std::string(kSha256) + "\r\noutput=/tmp/fsd-gtest-fw.bin\r\n";
  const Loaded loaded = LoadFromContent(content);
  ASSERT_TRUE(loaded.status.ok()) << loaded.status.message();
  EXPECT_EQ(loaded.manifest.url, kUrl);
  EXPECT_EQ(loaded.manifest.sha256_hex, kSha256);
}

TEST(LoadManifest, AcceptsMd5Only) {
  const Loaded loaded = LoadFromContent(
      "url=https://example.com/firmware.bin\n"
      "md5=D41D8CD98F00B204E9800998ECF8427E\n"
      "output=/tmp/fsd-gtest-md5.bin\n");
  ASSERT_TRUE(loaded.status.ok()) << loaded.status.message();
  EXPECT_EQ(loaded.manifest.md5_hex, "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_TRUE(loaded.manifest.sha256_hex.empty());
}

TEST(LoadManifest, RequiresUrlOutputAndOneDigest) {
  const Loaded missing_url =
      LoadFromContent("sha256=" + std::string(kSha256) +
                      "\noutput=/tmp/fw.bin\n");
  EXPECT_EQ(missing_url.status.code(), ErrorCode::kInvalidArgument);

  const Loaded missing_output =
      LoadFromContent("url=" + std::string(kUrl) + "\nsha256=" +
                      std::string(kSha256) + "\n");
  EXPECT_EQ(missing_output.status.code(), ErrorCode::kInvalidArgument);

  const Loaded missing_digest =
      LoadFromContent("url=" + std::string(kUrl) + "\noutput=/tmp/fw.bin\n");
  EXPECT_EQ(missing_digest.status.code(), ErrorCode::kInvalidArgument);
}

TEST(LoadManifest, RejectsUnknownAndDuplicateFields) {
  const Loaded unknown = LoadFromContent(
      ValidBody() + "typo=1\n");
  EXPECT_EQ(unknown.status.code(), ErrorCode::kInvalidArgument);

  const Loaded duplicate = LoadFromContent(
      "url=https://example.com/a\nurl=https://example.com/b\nsha256=" +
      std::string(kSha256) + "\noutput=/tmp/fw.bin\n");
  EXPECT_EQ(duplicate.status.code(), ErrorCode::kInvalidArgument);

  const Loaded duplicate_size = LoadFromContent(
      ValidBody() + "size=1\nsize=2\n");
  EXPECT_EQ(duplicate_size.status.code(), ErrorCode::kInvalidArgument);
}

TEST(LoadManifest, RejectsEmptyValueAndMissingEquals) {
  const Loaded empty_value = LoadFromContent(ValidBody() + "name=\n");
  EXPECT_EQ(empty_value.status.code(), ErrorCode::kInvalidArgument);

  const Loaded no_equals =
      LoadFromContent("url=" + std::string(kUrl) +
                      "\njust-a-line\nsha256=" + std::string(kSha256) +
                      "\noutput=/tmp/fw.bin\n");
  EXPECT_EQ(no_equals.status.code(), ErrorCode::kInvalidArgument);
}

TEST(LoadManifest, RejectsBadDigestHex) {
  const Loaded bad_sha =
      LoadFromContent("url=" + std::string(kUrl) +
                      "\nsha256=xyz\noutput=/tmp/fw.bin\n");
  EXPECT_EQ(bad_sha.status.code(), ErrorCode::kInvalidArgument);

  // 31 位十六进制：长度不对。
  const Loaded bad_md5 =
      LoadFromContent("url=" + std::string(kUrl) +
                      "\nmd5=0cc175b9c0f1b6a831c399e2697726\n"
                      "output=/tmp/fw.bin\n");
  EXPECT_EQ(bad_md5.status.code(), ErrorCode::kInvalidArgument);
}

TEST(LoadManifest, RejectsNonDecimalSize) {
  const Loaded not_number = LoadFromContent(ValidBody() + "size=abc\n");
  EXPECT_EQ(not_number.status.code(), ErrorCode::kInvalidArgument);

  const Loaded trailing = LoadFromContent(ValidBody() + "size=12x\n");
  EXPECT_EQ(trailing.status.code(), ErrorCode::kInvalidArgument);

  const Loaded plus = LoadFromContent(ValidBody() + "size=+3\n");
  EXPECT_EQ(plus.status.code(), ErrorCode::kInvalidArgument);
}

TEST(LoadManifest, ReportsMissingFileAsIoError) {
  FirmwareManifest manifest;
  const Status status = LoadManifest(
      std::filesystem::path("/nonexistent") / "manifest.txt", &manifest);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kIo);
}

TEST(LoadManifest, RejectsDirectoryPath) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);
  FirmwareManifest manifest;
  const Status status = LoadManifest(dir, &manifest);
  EXPECT_EQ(status.code(), ErrorCode::kIo);
}

TEST(LoadManifest, RejectsNullOutputPointer) {
  const std::filesystem::path path = WriteTempFile(ValidBody());
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  const Status status = LoadManifest(path, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

TEST(LoadManifest, EnforcesFileSizeLimit) {
  // 体积检查发生在逐行解析之前：64 KiB + 1 字节直接拒绝。
  const std::string huge(64 * 1024 + 1, '#');
  const std::filesystem::path path = WriteTempFile(huge);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  FirmwareManifest manifest;
  const Status status = LoadManifest(path, &manifest);
  EXPECT_EQ(status.code(), ErrorCode::kSizeLimit);
}

TEST(LoadManifest, EnforcesLineCountLimit) {
  // 200 行注释（都合法）+ 1 行内容：第 201 行触发上限。
  std::string many_lines;
  for (int i = 0; i < 200; ++i) {
    many_lines += "# filler comment\n";
  }
  many_lines += "url=" + std::string(kUrl) + "\n";
  const std::filesystem::path path = WriteTempFile(many_lines);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  FirmwareManifest manifest;
  const Status status = LoadManifest(path, &manifest);
  EXPECT_EQ(status.code(), ErrorCode::kSizeLimit);
}

TEST(LoadManifest, RejectsOverlongLine) {
  std::string long_line = "name=";
  long_line += std::string(5000, 'x');
  const std::filesystem::path path = WriteTempFile(long_line);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  FirmwareManifest manifest;
  const Status status = LoadManifest(path, &manifest);
  EXPECT_EQ(status.code(), ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace fsd
