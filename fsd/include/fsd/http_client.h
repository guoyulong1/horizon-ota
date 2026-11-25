// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 基于 libcurl 的 HTTPS 传输。这个类只负责把字节写到调用方指定的
// 临时文件，不解释固件格式，也不做摘要比对。

#ifndef FSD_HTTP_CLIENT_H_
#define FSD_HTTP_CLIENT_H_

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "fsd/status.h"
#include "fsd/version.h"

namespace fsd {

struct HttpRequest {
  std::string url;
  // 调用方已经打开并锁住的临时文件。传输过程中不关闭它。
  FILE* destination_file = nullptr;
  std::uint64_t resume_from = 0;
  std::uint64_t max_bytes = kDefaultMaxBytes;
  std::optional<std::uint64_t> expected_size;
  long connect_timeout_seconds = kDefaultConnectTimeoutSeconds;
  long transfer_timeout_seconds = kDefaultTransferTimeoutSeconds;
  long max_redirects = kDefaultMaxRedirects;
  std::string ca_file;
  // libcurl 公钥锁定，例如 "sha256//<base64(SPKI SHA-256)>"。
  std::string pinned_public_key;
  std::string user_agent{kDefaultUserAgent};
};

struct HttpResult {
  long http_status = 0;
  std::string effective_url;
  std::uint64_t bytes_on_disk = 0;
};

class HttpClient {
 public:
  HttpClient() = default;

  // 成功返回前，目标文件已 fflush 且 fsync。不关闭 destination_file。
  // 失败时保留已写入的前缀，由上层决定删除还是续传。
  [[nodiscard]] Status Download(const HttpRequest& request,
                                HttpResult* result) const;
};

}  // namespace fsd

#endif  // FSD_HTTP_CLIENT_H_
