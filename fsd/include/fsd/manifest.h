// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 固件清单是下载前就应当拿到的可信元数据，而不是从同一个 URL
// “顺便”拉下来的文件。格式故意做成按行的 key=value，避免再引入
// 一个通用解析器。

#ifndef FSD_MANIFEST_H_
#define FSD_MANIFEST_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "fsd/status.h"

namespace fsd {

struct FirmwareManifest {
  std::string name;
  std::string url;
  std::string sha256_hex;
  std::string md5_hex;
  std::filesystem::path output_path;
  std::optional<std::uint64_t> size_bytes;
  std::optional<std::uint64_t> max_bytes;
};

// 必填字段：url、output，以及 sha256 或 md5 至少一个。
// 摘要会规范成小写十六进制。未知字段直接失败。
[[nodiscard]] Status LoadManifest(const std::filesystem::path& path,
                                  FirmwareManifest* manifest);

[[nodiscard]] Status RunManifestSelfTest();

}  // namespace fsd

#endif  // FSD_MANIFEST_H_
