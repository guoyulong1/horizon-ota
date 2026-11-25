// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 固件 URL 只接受绝对 HTTPS。初始地址和每一次重定向都走同一套检查。

#ifndef FSD_URL_H_
#define FSD_URL_H_

#include <string>
#include <string_view>

#include "fsd/status.h"

namespace fsd {

// 拒绝用户信息、控制字符、非 ASCII 字节、非法百分号编码、坏主机和坏端口。
[[nodiscard]] Status ValidateUrl(std::string_view url);

// 把重定向的 Location 解析成绝对 URL。调用方仍须对结果调用 ValidateUrl。
[[nodiscard]] Status ResolveRedirect(std::string_view base,
                                     std::string_view location,
                                     std::string* resolved);

}  // namespace fsd

#endif  // FSD_URL_H_
