// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// secure_downloader.cc：下载编排。这里覆盖参数校验矩阵与发布前的负路径
// ——全部用本机被拒端口（127.0.0.1:1），不访问外网。校验不通过的用例
// 在进入网络阶段之前就会被拒绝；走到传输阶段的用例断言失败码是 network，
// 且失败后不残留 .part、不破坏已有输出文件。

#include "fsd/secure_downloader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "fsd/status.h"
#include "fsd_test_util.h"
#include "fsd/version.h"

namespace fsd {
namespace {

using fsd::testing::MakeTempDir;
using fsd::testing::TempPathGuard;

constexpr std::string_view kSha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr std::string_view kMd5 = "d41d8cd98f00b204e9800998ecf8427e";

// 一组合法参数；指向本机无人监听的端口，确保不会真的联网。
DownloadOptions BaseOptions(const std::filesystem::path& dir) {
  DownloadOptions options;
  options.package_name = "gtest";
  options.url = "https://127.0.0.1:1/firmware.bin";
  options.output_path = dir / "firmware.bin";
  options.expected_sha256 = std::string(kSha256);
  options.connect_timeout_seconds = 2;
  options.transfer_timeout_seconds = 5;
  return options;
}

struct TempWorkspace {
  std::filesystem::path dir;
  TempPathGuard guard;

  TempWorkspace() : dir(MakeTempDir()), guard(dir) {
    EXPECT_FALSE(dir.empty());
  }
};

void ExpectRejection(const DownloadOptions& options, ErrorCode code) {
  const Status status = SecureDownloader().Download(options);
  EXPECT_FALSE(status.ok()) << "unexpectedly accepted: " << status.message();
  EXPECT_EQ(status.code(), code) << status.message();
}

void ExpectInvalidArgument(const DownloadOptions& options) {
  ExpectRejection(options, ErrorCode::kInvalidArgument);
}

TEST(ValidateOptions, RejectsNonHttpsUrl) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.url = "http://example.com/firmware.bin";
  ExpectRejection(options, ErrorCode::kProtocol);
}

TEST(ValidateOptions, RejectsEmptyUrl) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.url.clear();
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsMalformedSha256) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.expected_sha256 = "xyz";
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsMalformedMd5) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.expected_sha256.clear();
  options.expected_md5 = "xyz";
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsMalformedSha256EvenWhenMd5IsValid) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.expected_md5 = std::string(kMd5);
  options.expected_sha256 = std::string(64, 'g');
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsZeroMaxBytes) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.max_bytes = 0;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsZeroDeclaredSize) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.expected_size = 0;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsDeclaredSizeAboveLimit) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.max_bytes = 100;
  options.expected_size = 101;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsOutOfRangeConnectTimeout) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.connect_timeout_seconds = 0;
  ExpectInvalidArgument(options);

  options.connect_timeout_seconds = 601;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsOutOfRangeTransferTimeout) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.transfer_timeout_seconds = 0;
  ExpectInvalidArgument(options);

  options.transfer_timeout_seconds = 86401;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsConnectTimeoutAboveTransferTimeout) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.connect_timeout_seconds = 60;
  options.transfer_timeout_seconds = 59;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsOutOfRangeRedirectCount) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.max_redirects = -1;
  ExpectInvalidArgument(options);

  options.max_redirects = 21;
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsBadUserAgent) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.user_agent = "";
  ExpectInvalidArgument(options);

  options.user_agent = std::string(257, 'a');
  ExpectInvalidArgument(options);

  options.user_agent = "Fsd/1.0\n";
  ExpectInvalidArgument(options);
}

TEST(ValidateOptions, RejectsMissingCaFile) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.ca_file = (workspace.dir / "missing.pem").string();
  ExpectRejection(options, ErrorCode::kIo);
}

TEST(ValidateOptions, RejectsBadPublicKeyPin) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.pinned_public_key = "sha256//has space";
  ExpectInvalidArgument(options);

  options.pinned_public_key = std::string(513, 'a');
  ExpectInvalidArgument(options);
}

TEST(PrepareDestination, RejectsEmptyOutputPath) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.output_path.clear();
  ExpectInvalidArgument(options);
}

TEST(PrepareDestination, RejectsOutputDirectory) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.output_path = workspace.dir;  // 已存在的目录
  ExpectInvalidArgument(options);
}

TEST(PrepareDestination, RejectsExistingOutputWithoutForce) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  {
    std::ofstream existing(options.output_path, std::ios::binary);
    existing << "OLD";
  }
  ExpectRejection(options, ErrorCode::kIo);
}

TEST(SecureDownloader, NetworkFailureLeavesNoPartFile) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  const Status status = SecureDownloader().Download(options);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kNetwork) << status.message();
  // 失败清理：临时目录里不残留 .part。
  std::error_code error;
  EXPECT_TRUE(std::filesystem::is_empty(workspace.dir, error)) << error.message();
}

TEST(SecureDownloader, AcceptsBothDigestsBeforeTransfer) {
  // SHA-256 与 MD5 同时给出属于合法参数：应通过校验进入传输阶段，
  // 失败码是 network 而不是 invalid-argument。
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  options.expected_md5 = std::string(kMd5);
  ExpectRejection(options, ErrorCode::kNetwork);
}

TEST(SecureDownloader, KeepsExistingOutputWhenTransferFailsWithForce) {
  TempWorkspace workspace;
  DownloadOptions options = BaseOptions(workspace.dir);
  const std::filesystem::path output = options.output_path;
  {
    std::ofstream existing(output, std::ios::binary);
    existing << "OLD";
  }
  options.force = true;
  ExpectRejection(options, ErrorCode::kNetwork);

  // 传输失败不得动已有的输出文件。
  std::ifstream kept(output, std::ios::binary);
  std::string content;
  std::getline(kept, content);
  EXPECT_EQ(content, "OLD");
}

}  // namespace
}  // namespace fsd
