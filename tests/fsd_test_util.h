// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 单元测试共用的小工具。测试只跑在 Linux 上，临时文件直接用 mkstemp /
// mkdtemp 创建，结束由 RAII 清理，不依赖用例通过与否。
// 与独立 fsd 仓库（firmware_secure_download/tests/fsd_test_util.h）保持
// 逐字一致，便于移植与同步。

#ifndef FSD_TEST_UTIL_H_
#define FSD_TEST_UTIL_H_

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fsd {
namespace testing {

// 把 string_view 的字节视图交给摘要/写入类接口用。
inline std::span<const std::uint8_t> AsBytes(std::string_view text) {
  return std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

// 把内容写进一个独占的临时文件，返回路径；失败返回空路径。
inline std::filesystem::path WriteTempFile(std::string_view content) {
  std::string pattern =
      (std::filesystem::temp_directory_path() / "fsd-gtest-XXXXXX").string();
  const int descriptor = ::mkstemp(pattern.data());
  if (descriptor < 0) {
    return {};
  }
  const ssize_t wrote = ::write(descriptor, content.data(), content.size());
  const int closed = ::close(descriptor);
  if (wrote < 0 || static_cast<std::size_t>(wrote) != content.size() ||
      closed != 0) {
    std::error_code error;
    std::filesystem::remove(pattern, error);
    return {};
  }
  return pattern;
}

// 每个用例独占一个临时目录；失败返回空路径。
inline std::filesystem::path MakeTempDir() {
  std::string pattern =
      (std::filesystem::temp_directory_path() / "fsd-gtest-XXXXXX").string();
  if (::mkdtemp(pattern.data()) == nullptr) {
    return {};
  }
  return pattern;
}

// RAII 包装：离开作用域时删除临时文件或目录，已不存在则忽略。
class TempPathGuard {
 public:
  explicit TempPathGuard(std::filesystem::path path)
      : path_(std::move(path)) {}
  ~TempPathGuard() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempPathGuard(const TempPathGuard&) = delete;
  TempPathGuard& operator=(const TempPathGuard&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace testing
}  // namespace fsd

#endif  // FSD_TEST_UTIL_H_
