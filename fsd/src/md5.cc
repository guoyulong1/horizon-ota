// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong

#include "fsd/md5.h"

#include <openssl/evp.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

namespace fsd {
namespace {

constexpr std::size_t kReadBufferSize = 64 * 1024;

struct EvpMdDeleter {
  void operator()(EVP_MD_CTX* ctx) const noexcept { EVP_MD_CTX_free(ctx); }
};

using EvpMdPtr = std::unique_ptr<EVP_MD_CTX, EvpMdDeleter>;

std::string ToHex(std::span<const unsigned char> bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string hex(bytes.size() * 2, '\0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto value = static_cast<unsigned int>(bytes[i]);
    hex[i * 2] = kDigits[(value >> 4U) & 0x0fU];
    hex[i * 2 + 1] = kDigits[value & 0x0fU];
  }
  return hex;
}

Md5Digest FailDigest(ErrorCode code, std::string message) {
  Md5Digest digest;
  digest.status = Status::Failure(code, std::move(message));
  return digest;
}

}  // namespace

struct Md5Hasher::Context {
  EvpMdPtr md;
  bool finalized = false;
};

Md5Hasher::Md5Hasher() : ctx_(std::make_unique<Context>()) {
  ctx_->md.reset(EVP_MD_CTX_new());
  if (ctx_->md == nullptr ||
      EVP_DigestInit_ex2(ctx_->md.get(), EVP_md5(), nullptr) != 1) {
    ctx_.reset();
  }
}

Md5Hasher::~Md5Hasher() = default;

bool Md5Hasher::ok() const {
  return ctx_ != nullptr && ctx_->md != nullptr && !ctx_->finalized;
}

void Md5Hasher::Update(std::span<const std::uint8_t> data) {
  if (!ok() || data.empty()) {
    return;
  }
  EVP_DigestUpdate(ctx_->md.get(), data.data(), data.size());
}

std::string Md5Hasher::FinalizeHex() {
  if (!ok()) {
    return {};
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  const int rc = EVP_DigestFinal_ex(ctx_->md.get(), digest.data(), &length);
  ctx_->finalized = true;
  if (rc != 1 || length != kMd5Bytes) {
    return {};
  }
  return ToHex(std::span<const unsigned char>(digest.data(), length));
}

std::optional<std::string> NormalizeMd5Hex(std::string_view hex) {
  if (hex.size() != kMd5HexLength) {
    return std::nullopt;
  }
  std::string normalized(kMd5HexLength, '\0');
  for (std::size_t i = 0; i < hex.size(); ++i) {
    const auto ch = static_cast<unsigned char>(hex[i]);
    if (ch >= '0' && ch <= '9') {
      normalized[i] = static_cast<char>(ch);
    } else if (ch >= 'a' && ch <= 'f') {
      normalized[i] = static_cast<char>(ch);
    } else if (ch >= 'A' && ch <= 'F') {
      normalized[i] = static_cast<char>(ch - 'A' + 'a');
    } else {
      return std::nullopt;
    }
  }
  return normalized;
}

bool IsMd5Hex(std::string_view hex) { return NormalizeMd5Hex(hex).has_value(); }

bool Md5DigestsEqual(std::string_view actual_hex,
                     std::string_view expected_hex) {
  const std::optional<std::string> actual = NormalizeMd5Hex(actual_hex);
  const std::optional<std::string> expected = NormalizeMd5Hex(expected_hex);
  if (!actual.has_value() || !expected.has_value()) {
    return false;
  }
  unsigned char diff = 0;
  for (std::size_t i = 0; i < actual->size(); ++i) {
    diff = static_cast<unsigned char>(
        diff | static_cast<unsigned char>((*actual)[i] ^ (*expected)[i]));
  }
  return diff == 0;
}

Md5Digest HashFileMd5(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return FailDigest(ErrorCode::kIo,
                      "cannot open the firmware file to compute MD5");
  }

  Md5Hasher hasher;
  if (!hasher.ok()) {
    return FailDigest(ErrorCode::kIo, "MD5 initialization failed");
  }

  std::array<char, kReadBufferSize> buffer{};
  while (true) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) {
      hasher.Update(std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t*>(buffer.data()),
          static_cast<std::size_t>(count)));
    }
    if (input.eof()) {
      break;
    }
    if (!input) {
      return FailDigest(ErrorCode::kIo,
                        "error while reading the firmware file");
    }
  }

  Md5Digest digest;
  digest.hex = hasher.FinalizeHex();
  if (digest.hex.size() != kMd5HexLength) {
    return FailDigest(ErrorCode::kIo, "MD5 computation failed");
  }
  digest.status = Status::Ok();
  return digest;
}

Status RunMd5SelfTest() {
  constexpr std::string_view kEmptyDigest = "d41d8cd98f00b204e9800998ecf8427e";

  Md5Hasher empty_hasher;
  empty_hasher.Update({});
  if (!Md5DigestsEqual(empty_hasher.FinalizeHex(), kEmptyDigest)) {
    return Status::Failure(ErrorCode::kChecksumMismatch,
                           "MD5 empty-string self-test failed");
  }

  if (IsMd5Hex("abc") || IsMd5Hex(std::string(31, 'a')) ||
      !IsMd5Hex(kEmptyDigest)) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "MD5 hexadecimal format check failed");
  }

  std::string pattern =
      (std::filesystem::temp_directory_path() / "fsd-XXXXXX").string();
  const int descriptor = ::mkstemp(pattern.data());
  if (descriptor < 0) {
    return Status::Failure(ErrorCode::kIo,
                           "cannot create the MD5 self-test file");
  }
  const std::filesystem::path path = pattern;
  if (::close(descriptor) != 0) {
    std::filesystem::remove(path);
    return Status::Failure(ErrorCode::kIo,
                           "cannot create the MD5 self-test file");
  }
  const Md5Digest file_digest = HashFileMd5(path);
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  if (!file_digest.status.ok()) {
    return file_digest.status;
  }
  if (!Md5DigestsEqual(file_digest.hex, kEmptyDigest)) {
    return Status::Failure(
        ErrorCode::kChecksumMismatch,
        "empty-file MD5 does not match the empty-string vector");
  }
  return Status::Ok();
}

}  // namespace fsd
