// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// status.h：统一的错误传递方式。退出码与枚举值的对应关系写在 README
// 「退出码」表里，这里把契约锁住，防止枚举重排后退出码悄悄变化。

#include "fsd/status.h"

#include <gtest/gtest.h>

namespace fsd {
namespace {

TEST(Status, OkIsTheDefaultValue) {
  const Status status = Status::Ok();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kOk);
  EXPECT_TRUE(status.message().empty());
}

TEST(Status, FailureCarriesCodeAndMessage) {
  const Status status =
      Status::Failure(ErrorCode::kChecksumMismatch, "digest mismatch");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kChecksumMismatch);
  EXPECT_EQ(status.message(), "digest mismatch");
}

TEST(Status, FailureWithEmptyMessageStaysInvalid) {
  const Status status = Status::Failure(ErrorCode::kIo, "");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), ErrorCode::kIo);
  EXPECT_TRUE(status.message().empty());
}

// 进程退出码与枚举值一致（README 退出码表），脚本靠它判断失败原因。
TEST(ErrorCode, ExitCodeContractMatchesDocumentation) {
  EXPECT_EQ(static_cast<int>(ErrorCode::kOk), 0);
  EXPECT_EQ(static_cast<int>(ErrorCode::kInvalidArgument), 2);
  EXPECT_EQ(static_cast<int>(ErrorCode::kIo), 3);
  EXPECT_EQ(static_cast<int>(ErrorCode::kNetwork), 4);
  EXPECT_EQ(static_cast<int>(ErrorCode::kTls), 5);
  EXPECT_EQ(static_cast<int>(ErrorCode::kHttp), 6);
  EXPECT_EQ(static_cast<int>(ErrorCode::kProtocol), 7);
  EXPECT_EQ(static_cast<int>(ErrorCode::kSizeLimit), 8);
  EXPECT_EQ(static_cast<int>(ErrorCode::kChecksumMismatch), 9);
  EXPECT_EQ(static_cast<int>(ErrorCode::kRangeNotSupported), 10);
}

TEST(ErrorCodeName, MapsEveryCodeToItsDocumentedName) {
  EXPECT_EQ(ErrorCodeName(ErrorCode::kOk), "ok");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kInvalidArgument), "invalid-argument");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kIo), "io");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kNetwork), "network");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kTls), "tls");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kHttp), "http");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kProtocol), "protocol");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kSizeLimit), "size-limit");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kChecksumMismatch), "checksum-mismatch");
  EXPECT_EQ(ErrorCodeName(ErrorCode::kRangeNotSupported),
            "range-not-supported");
}

}  // namespace
}  // namespace fsd
