// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 固件包安全下载的编排层，威胁模型限定为：
// 1. 传输被降级、重定向到明文，或证书主机名不匹配。
// 2. 镜像在途中被替换、截断，或临时文件在校验前就被当成正式包。
//
// 对应措施是只允许 HTTPS（证书、主机名校验不可关闭，最低 TLS 1.2），
// 以及先写入同目录临时文件，调用方给出的 SHA-256 或 MD5 一致后才原子改名。
// 摘要必须来自带外可信清单。它只能证明下载到的字节与清单一致，不能证明
// 清单本身可信，也不能阻止旧版本固件被重放。本演示不验证发布者签名，也没有
// 防回滚计数。生产环境应在此之后校验签名，并要求版本号单调递增。

#ifndef FSD_SECURE_DOWNLOADER_H_
#define FSD_SECURE_DOWNLOADER_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "fsd/status.h"
#include "fsd/version.h"

namespace fsd {

struct DownloadOptions {
  std::string package_name;
  std::string url;
  std::filesystem::path output_path;
  std::string expected_sha256;
  std::string expected_md5;
  std::optional<std::uint64_t> expected_size;
  std::uint64_t max_bytes = kDefaultMaxBytes;
  long connect_timeout_seconds = kDefaultConnectTimeoutSeconds;
  long transfer_timeout_seconds = kDefaultTransferTimeoutSeconds;
  long max_redirects = kDefaultMaxRedirects;
  bool resume = false;
  bool force = false;
  std::string ca_file;
  std::string pinned_public_key;
  std::string user_agent{kDefaultUserAgent};
};

class SecureDownloader {
 public:
  [[nodiscard]] Status Download(const DownloadOptions& options) const;
};

}  // namespace fsd

#endif  // FSD_SECURE_DOWNLOADER_H_
