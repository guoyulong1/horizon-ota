// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 项目内统一的错误传递方式。按 Google C++ 风格，不用异常表达
// 可预期失败（网络、校验、参数），调用方必须处理 Status。

#ifndef FSD_STATUS_H_
#define FSD_STATUS_H_

#include <string>
#include <string_view>
#include <utility>

namespace fsd {

// 进程退出码与枚举值一致，便于脚本判断失败原因。
enum class ErrorCode {
  kOk = 0,
  kInvalidArgument = 2,
  kIo = 3,
  kNetwork = 4,
  kTls = 5,
  kHttp = 6,
  kProtocol = 7,
  kSizeLimit = 8,
  kChecksumMismatch = 9,
  kRangeNotSupported = 10,
};

[[nodiscard]] inline std::string_view ErrorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::kOk:
      return "ok";
    case ErrorCode::kInvalidArgument:
      return "invalid-argument";
    case ErrorCode::kIo:
      return "io";
    case ErrorCode::kNetwork:
      return "network";
    case ErrorCode::kTls:
      return "tls";
    case ErrorCode::kHttp:
      return "http";
    case ErrorCode::kProtocol:
      return "protocol";
    case ErrorCode::kSizeLimit:
      return "size-limit";
    case ErrorCode::kChecksumMismatch:
      return "checksum-mismatch";
    case ErrorCode::kRangeNotSupported:
      return "range-not-supported";
  }
  return "unknown";
}

class Status {
 public:
  Status() = default;

  static Status Ok() { return Status(); }

  static Status Failure(ErrorCode code, std::string message) {
    return Status(code, std::move(message));
  }

  [[nodiscard]] bool ok() const { return code_ == ErrorCode::kOk; }
  [[nodiscard]] ErrorCode code() const { return code_; }
  [[nodiscard]] std::string_view message() const { return message_; }

 private:
  Status(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  ErrorCode code_ = ErrorCode::kOk;
  std::string message_;
};

}  // namespace fsd

#endif  // FSD_STATUS_H_
