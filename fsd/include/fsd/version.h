// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 版本号与下载器的默认安全限额。默认值偏保守，避免演示程序
// 在错误 URL 上无界地写盘。

#ifndef FSD_VERSION_H_
#define FSD_VERSION_H_

#include <cstdint>
#include <string_view>

namespace fsd {

inline constexpr std::string_view kProjectVersion = "1.0.0";
inline constexpr std::string_view kProjectAuthor = "GuoYuLong";

// 单个固件包的默认体积上限：256 MiB。
inline constexpr std::uint64_t kDefaultMaxBytes = 256ull * 1024ull * 1024ull;

inline constexpr long kDefaultConnectTimeoutSeconds = 15;
inline constexpr long kDefaultTransferTimeoutSeconds = 300;
inline constexpr long kDefaultMaxRedirects = 5;

inline constexpr std::string_view kDefaultUserAgent =
    "FsdFirmwareDownloader/1.0";

}  // namespace fsd

#endif  // FSD_VERSION_H_
