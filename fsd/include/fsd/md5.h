// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 可选的 MD5 完整性校验。摘要格式和文件核对都由下载器在发布前完成，
// 与 SHA-256 同一时机。MD5 不能代替 SHA-256，更不能代替发布者签名。

#ifndef FSD_MD5_H_
#define FSD_MD5_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "fsd/status.h"

namespace fsd {

inline constexpr std::size_t kMd5Bytes = 16;
inline constexpr std::size_t kMd5HexLength = 32;

// 接受大小写十六进制。非法输入返回空 optional，不抛异常。
[[nodiscard]] std::optional<std::string> NormalizeMd5Hex(std::string_view hex);

[[nodiscard]] bool IsMd5Hex(std::string_view hex);

// 比较两份摘要。长度或字符非法时返回 false。
[[nodiscard]] bool Md5DigestsEqual(std::string_view actual_hex,
                                   std::string_view expected_hex);

// OpenSSL 的 EVP_MD_CTX 只在 .cc 中出现，头文件不依赖 openssl。
class Md5Hasher {
 public:
  Md5Hasher();
  ~Md5Hasher();

  Md5Hasher(const Md5Hasher&) = delete;
  Md5Hasher& operator=(const Md5Hasher&) = delete;
  Md5Hasher(Md5Hasher&&) = delete;
  Md5Hasher& operator=(Md5Hasher&&) = delete;

  [[nodiscard]] bool ok() const;
  void Update(std::span<const std::uint8_t> data);
  // 每个实例只能收尾一次。失败时返回空字符串。
  [[nodiscard]] std::string FinalizeHex();

 private:
  struct Context;
  std::unique_ptr<Context> ctx_;
};

struct Md5Digest {
  Status status;
  std::string hex;
};

// 以固定大小的缓冲区流式读取，避免把整包固件读进内存。
[[nodiscard]] Md5Digest HashFileMd5(const std::filesystem::path& path);

// 用 RFC 1321 的空串向量做自检，不访问网络。
[[nodiscard]] Status RunMd5SelfTest();

}  // namespace fsd

#endif  // FSD_MD5_H_
