// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong

#include "fsd/sha256.h"

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

Sha256Digest FailDigest(ErrorCode code, std::string message) {
  Sha256Digest digest;
  digest.status = Status::Failure(code, std::move(message));
  return digest;
}

}  // namespace

struct Sha256Hasher::Context {
  EvpMdPtr md;
  bool finalized = false;
};

Sha256Hasher::Sha256Hasher() : ctx_(std::make_unique<Context>()) {
  ctx_->md.reset(EVP_MD_CTX_new());
  // EVP_DigestInit_ex2 从 OpenSSL 3.0 起可用，3.5 中替代已弃用的
  // EVP_DigestInit_ex。params 为空表示不附加额外摘要参数。
  if (ctx_->md == nullptr ||
      EVP_DigestInit_ex2(ctx_->md.get(), EVP_sha256(), nullptr) != 1) {
    ctx_.reset();
  }
}

Sha256Hasher::~Sha256Hasher() = default;

bool Sha256Hasher::ok() const {
  return ctx_ != nullptr && ctx_->md != nullptr && !ctx_->finalized;
}

void Sha256Hasher::Update(std::span<const std::uint8_t> data) {
  if (!ok() || data.empty()) {
    return;
  }
  EVP_DigestUpdate(ctx_->md.get(), data.data(), data.size());
}

std::string Sha256Hasher::FinalizeHex() {
  if (!ok()) {
    return {};
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  const int rc = EVP_DigestFinal_ex(ctx_->md.get(), digest.data(), &length);
  ctx_->finalized = true;
  if (rc != 1 || length != kSha256Bytes) {
    return {};
  }
  return ToHex(std::span<const unsigned char>(digest.data(), length));
}

std::optional<std::string> NormalizeSha256Hex(std::string_view hex) {
  if (hex.size() != kSha256HexLength) {
    return std::nullopt;
  }
  std::string normalized(kSha256HexLength, '\0');
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

bool IsSha256Hex(std::string_view hex) {
  return NormalizeSha256Hex(hex).has_value();
}

bool DigestsEqual(std::string_view actual_hex, std::string_view expected_hex) {
  const std::optional<std::string> actual = NormalizeSha256Hex(actual_hex);
  const std::optional<std::string> expected = NormalizeSha256Hex(expected_hex);
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

Sha256Digest HashFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return FailDigest(ErrorCode::kIo,
                      "cannot open the firmware file to compute SHA-256");
  }

  Sha256Hasher hasher;
  if (!hasher.ok()) {
    return FailDigest(ErrorCode::kIo, "SHA-256 initialization failed");
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

  Sha256Digest digest;
  digest.hex = hasher.FinalizeHex();
  if (digest.hex.size() != kSha256HexLength) {
    return FailDigest(ErrorCode::kIo, "SHA-256 computation failed");
  }
  digest.status = Status::Ok();
  return digest;
}

Status RunSha256SelfTest() {
  constexpr std::string_view kEmptyDigest =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  constexpr std::string_view kAbcDigest =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

  Sha256Hasher empty_hasher;
  empty_hasher.Update({});
  if (empty_hasher.FinalizeHex() != kEmptyDigest) {
    return Status::Failure(ErrorCode::kChecksumMismatch,
                           "SHA-256 empty-string self-test failed");
  }

  constexpr std::string_view kAbc = "abc";
  Sha256Hasher abc_hasher;
  abc_hasher.Update(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(kAbc.data()), kAbc.size()));
  if (!DigestsEqual(abc_hasher.FinalizeHex(), kAbcDigest)) {
    return Status::Failure(ErrorCode::kChecksumMismatch,
                           "SHA-256 \"abc\" vector self-test failed");
  }

  if (IsSha256Hex("abc") || IsSha256Hex(std::string(63, 'a')) ||
      !IsSha256Hex(kAbcDigest)) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "SHA-256 hexadecimal format check failed");
  }

  std::string pattern =
      (std::filesystem::temp_directory_path() / "fsd-XXXXXX").string();
  const int descriptor = ::mkstemp(pattern.data());
  if (descriptor < 0) {
    return Status::Failure(ErrorCode::kIo,
                           "cannot create the SHA-256 self-test file");
  }
  const std::filesystem::path path = pattern;
  const ssize_t wrote = ::write(descriptor, kAbc.data(), kAbc.size());
  const int closed = ::close(descriptor);
  if (wrote != static_cast<ssize_t>(kAbc.size()) || closed != 0) {
    std::filesystem::remove(path);
    return Status::Failure(ErrorCode::kIo,
                           "cannot write the SHA-256 self-test file");
  }
  const Sha256Digest file_digest = HashFile(path);
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  if (!file_digest.status.ok()) {
    return file_digest.status;
  }
  if (!DigestsEqual(file_digest.hex, kAbcDigest)) {
    return Status::Failure(ErrorCode::kChecksumMismatch,
                           "file SHA-256 does not match the in-memory vector");
  }
  return Status::Ok();
}

}  // namespace fsd
