// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// http_client.cc：基于 libcurl 的 HTTPS 传输。单元测试只覆盖不依赖
// 服务器的路径：参数校验、协议白名单，以及把「连接被拒」映射成
// network 错误。真实 TLS/重定向行为由真机验证（AGENTS.md）覆盖。

#include "fsd/http_client.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "fsd/status.h"
#include "fsd_test_util.h"

namespace fsd {
namespace {

using fsd::testing::TempPathGuard;
using fsd::testing::WriteTempFile;

// HttpClient 要求调用方传入已打开的 FILE*，且不负责关闭。
class DestinationFile {
 public:
  DestinationFile() {
    path_ = WriteTempFile("");
    if (!path_.empty()) {
      file_ = std::fopen(path_.string().c_str(), "wb");
    }
  }
  ~DestinationFile() {
    if (file_ != nullptr) {
      std::fclose(file_);
    }
  }

  DestinationFile(const DestinationFile&) = delete;
  DestinationFile& operator=(const DestinationFile&) = delete;

  [[nodiscard]] FILE* file() const { return file_; }

 private:
  FILE* file_ = nullptr;
  std::filesystem::path path_;
};

HttpRequest RefusedRequest(FILE* destination) {
  HttpRequest request;
  // 本机无人监听的端口：连接立即被拒，不访问外网。
  request.url = "https://127.0.0.1:1/firmware.bin";
  request.destination_file = destination;
  request.connect_timeout_seconds = 2;
  request.transfer_timeout_seconds = 5;
  return request;
}

TEST(HttpClient, RejectsNullResultAndClosedFile) {
  DestinationFile destination;
  ASSERT_TRUE(destination.file() != nullptr);

  HttpRequest request = RefusedRequest(destination.file());
  HttpResult result;
  EXPECT_EQ(HttpClient().Download(request, nullptr).code(),
            ErrorCode::kInvalidArgument);

  request.destination_file = nullptr;
  EXPECT_EQ(HttpClient().Download(request, &result).code(),
            ErrorCode::kInvalidArgument);
}

TEST(HttpClient, RefusesPlainHttpUrl) {
  // 协议白名单锁死 https：HttpClient 自己也要拦下明文地址。
  DestinationFile destination;
  ASSERT_TRUE(destination.file() != nullptr);

  HttpRequest request = RefusedRequest(destination.file());
  request.url = "http://127.0.0.1:1/firmware.bin";
  HttpResult result;
  const Status status = HttpClient().Download(request, &result);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kProtocol) << status.message();
}

TEST(HttpClient, MapsConnectionRefusedToNetworkError) {
  DestinationFile destination;
  ASSERT_TRUE(destination.file() != nullptr);

  HttpRequest request = RefusedRequest(destination.file());
  HttpResult result;
  const Status status = HttpClient().Download(request, &result);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kNetwork) << status.message();
  EXPECT_TRUE(result.effective_url.empty());
  EXPECT_EQ(result.bytes_on_disk, 0U);
}

}  // namespace
}  // namespace fsd
