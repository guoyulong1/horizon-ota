// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 固件包完整性摘要。SHA-256 用来确认“下载到的字节”与可信清单一致，
// 不能代替发布者签名：拿到清单的人必须先通过带外渠道信任这份摘要。

#ifndef FSD_SHA256_H_
#define FSD_SHA256_H_

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

inline constexpr std::size_t kSha256Bytes = 32;
inline constexpr std::size_t kSha256HexLength = 64;

// 接受大小写十六进制。非法输入返回空 optional，不抛异常。
[[nodiscard]] std::optional<std::string> NormalizeSha256Hex(
    std::string_view hex);

[[nodiscard]] bool IsSha256Hex(std::string_view hex);

// 比较两份摘要。长度或字符非法时返回 false。
// 摘要不是密钥，这里仍按字节累加差异后再判断，避免调用方写成提前返回的比较。
[[nodiscard]] bool DigestsEqual(std::string_view actual_hex,
                                std::string_view expected_hex);

// OpenSSL 的 EVP_MD_CTX 只在 .cc 中出现，头文件不依赖 openssl。
class Sha256Hasher {
 public:
  Sha256Hasher();
  ~Sha256Hasher();

  Sha256Hasher(const Sha256Hasher&) = delete;
  Sha256Hasher& operator=(const Sha256Hasher&) = delete;
  Sha256Hasher(Sha256Hasher&&) = delete;
  Sha256Hasher& operator=(Sha256Hasher&&) = delete;

  [[nodiscard]] bool ok() const;
  void Update(std::span<const std::uint8_t> data);
  // 每个实例只能收尾一次。失败时返回空字符串。
  [[nodiscard]] std::string FinalizeHex();

 private:
  struct Context;
  std::unique_ptr<Context> ctx_;
};

struct Sha256Digest {
  Status status;
  std::string hex;
};

// 以固定大小的缓冲区流式读取，避免把整包固件读进内存。
[[nodiscard]] Sha256Digest HashFile(const std::filesystem::path& path);

// 用 FIPS 180-4 的空串和 "abc" 向量做自检，不访问网络。
[[nodiscard]] Status RunSha256SelfTest();

}  // namespace fsd

#endif  // FSD_SHA256_H_
