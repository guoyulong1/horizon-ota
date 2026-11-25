// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 清单与命令行共用的小型文本工具。只接受完整十进制数字，
// 拒绝前导加号和尾部垃圾，避免“123abc”这类体积字段被悄悄截断。

#ifndef FSD_TEXT_UTIL_H_
#define FSD_TEXT_UTIL_H_

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace fsd {

[[nodiscard]] inline std::string_view Trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] inline bool EqualsIgnoreCase(std::string_view lhs,
                                           std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const int left = std::tolower(static_cast<unsigned char>(lhs[i]));
    const int right = std::tolower(static_cast<unsigned char>(rhs[i]));
    if (left != right) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool StartsWithIgnoreCase(std::string_view text,
                                               std::string_view prefix) {
  if (text.size() < prefix.size()) {
    return false;
  }
  return EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

[[nodiscard]] inline std::optional<std::uint64_t> ParseUint64(
    std::string_view text) {
  if (text.empty() || text.front() == '+' || text.front() == '-') {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] inline std::optional<long> ParseLong(std::string_view text) {
  if (text.empty() || text.front() == '+') {
    return std::nullopt;
  }
  long value = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] inline std::string ErrnoMessage(std::string_view action) {
  const std::error_code error(errno, std::generic_category());
  std::string message;
  message.reserve(action.size() + error.message().size() + 2);
  message.append(action);
  message.append(": ");
  message.append(error.message());
  return message;
}

}  // namespace fsd

#endif  // FSD_TEXT_UTIL_H_
