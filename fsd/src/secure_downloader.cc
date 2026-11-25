// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong

#include "fsd/secure_downloader.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "fsd/http_client.h"
#include "fsd/md5.h"
#include "fsd/sha256.h"
#include "fsd/text_util.h"
#include "fsd/url.h"

namespace fsd {
namespace {

class UniqueFd {
 public:
  explicit UniqueFd(int fd) noexcept : fd_(fd) {}
  ~UniqueFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_;
};

class PartFile {
 public:
  PartFile() = default;
  ~PartFile() { Close(); }

  PartFile(const PartFile&) = delete;
  PartFile& operator=(const PartFile&) = delete;

  [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }
  [[nodiscard]] FILE* file() const noexcept { return file_; }
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }

  // append 为真时不创建文件。文件不存在则 *missing 为真且保持未打开。
  Status Open(const std::filesystem::path& path, bool append, bool* missing) {
    Close();
    if (missing != nullptr) {
      *missing = false;
    }
    int flags = O_CLOEXEC | O_NOFOLLOW | O_WRONLY;
    if (append) {
      flags |= O_APPEND;
    } else {
      flags |= O_CREAT | O_TRUNC;
    }
    const int raw = ::open(path.c_str(), flags, 0644);
    if (raw < 0) {
      if (append && errno == ENOENT) {
        if (missing != nullptr) {
          *missing = true;
        }
        return Status::Ok();
      }
      if (errno == ELOOP) {
        return Status::Failure(ErrorCode::kInvalidArgument,
                               "temporary file is a symbolic link");
      }
      return Status::Failure(
          ErrorCode::kIo,
          ErrnoMessage("failed to open the temporary firmware file"));
    }
    if (::flock(raw, LOCK_EX | LOCK_NB) != 0) {
      const int saved = errno;
      ::close(raw);
      errno = saved;
      if (saved == EWOULDBLOCK || saved == EAGAIN) {
        return Status::Failure(ErrorCode::kIo,
                               "another download is using this temporary file");
      }
      return Status::Failure(
          ErrorCode::kIo,
          ErrnoMessage("failed to lock the temporary firmware file"));
    }
    struct stat state {};
    if (::fstat(raw, &state) != 0) {
      const int saved = errno;
      ::close(raw);
      errno = saved;
      return Status::Failure(
          ErrorCode::kIo, ErrnoMessage("cannot read the temporary file size"));
    }
    if (!S_ISREG(state.st_mode) || state.st_size < 0) {
      ::close(raw);
      return Status::Failure(ErrorCode::kInvalidArgument,
                             "temporary file is not a regular file");
    }
    FILE* opened = ::fdopen(raw, append ? "ab" : "wb");
    if (opened == nullptr) {
      const int saved = errno;
      ::close(raw);
      errno = saved;
      return Status::Failure(
          ErrorCode::kIo,
          ErrnoMessage("failed to open the temporary firmware file"));
    }
    file_ = opened;
    size_ = static_cast<std::uint64_t>(state.st_size);
    return Status::Ok();
  }

  void Close() {
    if (file_ != nullptr) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

 private:
  FILE* file_ = nullptr;
  std::uint64_t size_ = 0;
};

std::filesystem::path PartialPath(const std::filesystem::path& output) {
  return std::filesystem::path(output.string() + ".part");
}

struct ExpectedDigests {
  std::string sha256;
  std::string md5;
};

struct VerifiedDigests {
  std::string sha256;
  std::string md5;
};

Status ValidateOptions(const DownloadOptions& options,
                       ExpectedDigests* expected) {
  if (Status status = ValidateUrl(options.url); !status.ok()) {
    return status;
  }
  if (options.output_path.empty()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "an output path is required");
  }
  const std::optional<std::string> sha256 =
      options.expected_sha256.empty()
          ? std::optional<std::string>{}
          : NormalizeSha256Hex(options.expected_sha256);
  if (!options.expected_sha256.empty() && !sha256.has_value()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "expected SHA-256 must be 64 hexadecimal digits");
  }
  const std::optional<std::string> md5 =
      options.expected_md5.empty() ? std::optional<std::string>{}
                                   : NormalizeMd5Hex(options.expected_md5);
  if (!options.expected_md5.empty() && !md5.has_value()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "expected MD5 must be 32 hexadecimal digits");
  }
  if (options.max_bytes == 0) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "the size limit must be greater than 0");
  }
  if (options.expected_size.has_value() && *options.expected_size == 0) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "declared size must be greater than 0");
  }
  if (options.expected_size.has_value() &&
      *options.expected_size > options.max_bytes) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "declared size exceeds the size limit");
  }
  if (options.connect_timeout_seconds <= 0 ||
      options.connect_timeout_seconds > 600) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "connect timeout must be between 1 and 600 seconds");
  }
  if (options.transfer_timeout_seconds <= 0 ||
      options.transfer_timeout_seconds > 86400) {
    return Status::Failure(
        ErrorCode::kInvalidArgument,
        "transfer timeout must be between 1 and 86400 seconds");
  }
  if (options.connect_timeout_seconds > options.transfer_timeout_seconds) {
    return Status::Failure(
        ErrorCode::kInvalidArgument,
        "connect timeout cannot exceed the transfer timeout");
  }
  if (options.max_redirects < 0 || options.max_redirects > 20) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "redirect count must be between 0 and 20");
  }
  if (options.user_agent.empty() || options.user_agent.size() > 256) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "User-Agent is empty or too long");
  }
  for (const char ch : options.user_agent) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      return Status::Failure(ErrorCode::kInvalidArgument,
                             "User-Agent contains a control character");
    }
  }
  if (!options.ca_file.empty()) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(options.ca_file, error) || error) {
      return Status::Failure(ErrorCode::kIo,
                             "CA file does not exist or is not readable");
    }
  }
  if (!options.pinned_public_key.empty()) {
    if (options.pinned_public_key.size() > 512 ||
        options.pinned_public_key.find_first_of(" \t\r\n") !=
            std::string::npos) {
      return Status::Failure(ErrorCode::kInvalidArgument,
                             "public key pin is invalid");
    }
  }
  expected->sha256 = sha256.value_or("");
  expected->md5 = md5.value_or("");
  return Status::Ok();
}

Status VerifyFileDigests(const std::filesystem::path& path,
                         const ExpectedDigests& expected,
                         VerifiedDigests* verified) {
  VerifiedDigests result;
  if (!expected.sha256.empty()) {
    const Sha256Digest digest = HashFile(path);
    if (!digest.status.ok()) {
      return digest.status;
    }
    if (!DigestsEqual(digest.hex, expected.sha256)) {
      return Status::Failure(ErrorCode::kChecksumMismatch,
                             "SHA-256 mismatch. Expected " + expected.sha256 +
                                 ", actual " + digest.hex +
                                 ". The temporary file was removed");
    }
    result.sha256 = digest.hex;
  }
  if (!expected.md5.empty()) {
    const Md5Digest digest = HashFileMd5(path);
    if (!digest.status.ok()) {
      return digest.status;
    }
    if (!Md5DigestsEqual(digest.hex, expected.md5)) {
      return Status::Failure(ErrorCode::kChecksumMismatch,
                             "MD5 mismatch. Expected " + expected.md5 +
                                 ", actual " + digest.hex +
                                 ". The temporary file was removed");
    }
    result.md5 = digest.hex;
  }
  *verified = std::move(result);
  return Status::Ok();
}

Status PrepareDestination(const DownloadOptions& options) {
  std::error_code error;
  if (std::filesystem::is_directory(options.output_path, error)) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "output path is a directory");
  }
  if (std::filesystem::exists(options.output_path, error) && !options.force) {
    return Status::Failure(
        ErrorCode::kIo,
        "output file already exists; pass --force to replace it");
  }

  std::filesystem::path parent = options.output_path.parent_path();
  if (parent.empty()) {
    parent = ".";
  }
  std::filesystem::create_directories(parent, error);
  if (error) {
    return Status::Failure(
        ErrorCode::kIo,
        "cannot create the output directory: " + error.message());
  }
  return Status::Ok();
}

void RemoveQuietly(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

Status SyncDirectory(const std::filesystem::path& directory) {
  UniqueFd fd(::open(directory.c_str(), O_RDONLY | O_DIRECTORY));
  if (fd.get() < 0) {
    return Status::Failure(ErrorCode::kIo,
                           ErrnoMessage("cannot open the output directory"));
  }
  if (::fsync(fd.get()) != 0) {
    return Status::Failure(ErrorCode::kIo,
                           ErrnoMessage("cannot sync the output directory"));
  }
  return Status::Ok();
}

Status Publish(const std::filesystem::path& part,
               const DownloadOptions& options, std::uint64_t bytes,
               const VerifiedDigests& digests, std::string_view url,
               bool transferred) {
  std::error_code error;
  // 固件包是数据文件。去掉可执行位，避免把下载物直接当成程序跑起来。
  std::filesystem::permissions(part,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::others_read,
                               std::filesystem::perm_options::replace, error);
  if (error) {
    std::cerr << "warning: failed to reset file permissions ("
              << error.message()
              << "); the verified content will still be published\n";
  }

  if (std::filesystem::exists(options.output_path, error) && !options.force) {
    return Status::Failure(
        ErrorCode::kIo,
        "output file appeared before publish; the temporary file was kept");
  }
  std::filesystem::rename(part, options.output_path, error);
  if (error) {
    return Status::Failure(
        ErrorCode::kIo,
        "failed to publish the firmware package: " + error.message());
  }

  std::filesystem::path parent = options.output_path.parent_path();
  if (parent.empty()) {
    parent = ".";
  }
  // 文件内容在改名前已经 fsync。目录项落盘失败只告警，避免脚本把
  // 已经发布成功的固件包当成失败再重试一次。
  if (Status status = SyncDirectory(parent); !status.ok()) {
    std::cerr << "warning: " << status.message() << '\n';
  }

  const std::string name = options.package_name.empty()
                               ? std::string("unnamed package")
                               : options.package_name;
  const bool verified = !digests.sha256.empty() || !digests.md5.empty();
  std::cout << (verified ? "firmware package verified\n"
                         : "firmware package saved\n")
            << "  name: " << name << '\n'
            << "  path: " << options.output_path.string() << '\n'
            << "  size: " << bytes << " bytes\n";
  if (!digests.sha256.empty()) {
    std::cout << "  SHA-256: " << digests.sha256 << '\n';
  }
  if (!digests.md5.empty()) {
    std::cout << "  MD5: " << digests.md5 << '\n';
  }
  std::cout << (transferred ? "  final URL: " : "  requested URL: ") << url
            << '\n';
  if (!digests.sha256.empty()) {
    std::cout << "  note: TLS and SHA-256 checks passed. Publisher signature "
                 "verification is separate.\n";
  } else if (!digests.md5.empty()) {
    std::cout << "  note: TLS and MD5 checks passed. MD5 is weaker than "
                 "SHA-256, and publisher signature verification is separate.\n";
  }
  return Status::Ok();
}

Status DownloadOnce(const DownloadOptions& options, FILE* destination,
                    std::uint64_t resume_from, HttpResult* result) {
  HttpRequest request;
  request.url = options.url;
  request.destination_file = destination;
  request.resume_from = resume_from;
  request.max_bytes = options.max_bytes;
  request.expected_size = options.expected_size;
  request.connect_timeout_seconds = options.connect_timeout_seconds;
  request.transfer_timeout_seconds = options.transfer_timeout_seconds;
  request.max_redirects = options.max_redirects;
  request.ca_file = options.ca_file;
  request.pinned_public_key = options.pinned_public_key;
  request.user_agent = options.user_agent;

  HttpClient client;
  return client.Download(request, result);
}

}  // namespace

Status SecureDownloader::Download(const DownloadOptions& options) const {
  ExpectedDigests expected;
  if (Status status = ValidateOptions(options, &expected); !status.ok()) {
    return status;
  }
  if (Status status = PrepareDestination(options); !status.ok()) {
    return status;
  }

  const std::filesystem::path part = PartialPath(options.output_path);
  PartFile part_file;
  std::uint64_t resume_from = 0;
  if (options.resume) {
    bool missing = false;
    if (Status status = part_file.Open(part, true, &missing); !status.ok()) {
      return status;
    }
    if (!missing) {
      const std::uint64_t existing = part_file.size();
      if (existing > options.max_bytes) {
        part_file.Close();
        return Status::Failure(
            ErrorCode::kSizeLimit,
            "existing temporary file exceeds the size limit");
      }
      if (options.expected_size.has_value() &&
          existing > *options.expected_size) {
        part_file.Close();
        RemoveQuietly(part);
      } else if (options.expected_size.has_value() &&
                 existing == *options.expected_size &&
                 (!expected.sha256.empty() || !expected.md5.empty())) {
        VerifiedDigests verified;
        const Status matched = VerifyFileDigests(part, expected, &verified);
        if (matched.ok()) {
          const Status published =
              Publish(part, options, existing, verified, options.url, false);
          part_file.Close();
          return published;
        }
        part_file.Close();
        if (matched.code() != ErrorCode::kChecksumMismatch) {
          return matched;
        }
        // 长度碰巧相同但摘要不对，这段前缀不能再续。
        std::cerr << "temporary file has the declared size but the digest does "
                     "not match; discarding it and downloading again\n";
        RemoveQuietly(part);
      } else if (existing > 0) {
        resume_from = existing;
      } else {
        part_file.Close();
      }
    }
  }
  if (!part_file.is_open()) {
    if (Status status = part_file.Open(part, false, nullptr); !status.ok()) {
      return status;
    }
  }

  std::cerr << "starting firmware download\n"
            << "  URL: " << options.url << '\n'
            << "  temporary file: " << part.string() << '\n';
  if (!expected.sha256.empty()) {
    std::cerr << "  expected SHA-256: " << expected.sha256 << '\n';
  }
  if (!expected.md5.empty()) {
    std::cerr << "  expected MD5: " << expected.md5 << '\n';
  }
  std::cerr << "  resume offset: " << resume_from << '\n'
            << "  TLS certificate and hostname verification: on, TLS 1.2 "
               "minimum, HTTPS only\n";

  HttpResult transferred;
  Status transfer =
      DownloadOnce(options, part_file.file(), resume_from, &transferred);
  if (!transfer.ok() && resume_from > 0 &&
      transfer.code() == ErrorCode::kRangeNotSupported) {
    std::cerr << "server did not provide a usable range; downloading the full "
                 "file\n";
    part_file.Close();
    RemoveQuietly(part);
    resume_from = 0;
    if (Status status = part_file.Open(part, false, nullptr); !status.ok()) {
      return status;
    }
    transfer = DownloadOnce(options, part_file.file(), 0, &transferred);
  }
  if (!transfer.ok()) {
    // 摘要还没算。网络中断且用户要求续传时保留前缀；其余失败删掉，
    // 避免下一次把不可信的半包当成进度。
    const bool remove = !options.resume ||
                        transfer.code() == ErrorCode::kInvalidArgument ||
                        transfer.code() == ErrorCode::kSizeLimit ||
                        transfer.code() == ErrorCode::kProtocol;
    part_file.Close();
    if (remove) {
      RemoveQuietly(part);
    }
    return transfer;
  }

  if (transferred.bytes_on_disk == 0) {
    part_file.Close();
    RemoveQuietly(part);
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "downloaded firmware package is empty");
  }
  if (options.expected_size.has_value() &&
      transferred.bytes_on_disk != *options.expected_size) {
    part_file.Close();
    RemoveQuietly(part);
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "downloaded size " +
                               std::to_string(transferred.bytes_on_disk) +
                               " does not match declared size " +
                               std::to_string(*options.expected_size));
  }

  VerifiedDigests verified;
  if (!expected.sha256.empty() || !expected.md5.empty()) {
    if (Status status = VerifyFileDigests(part, expected, &verified);
        !status.ok()) {
      part_file.Close();
      RemoveQuietly(part);
      return status;
    }
  }
  const Status published = Publish(part, options, transferred.bytes_on_disk,
                                   verified, transferred.effective_url, true);
  part_file.Close();
  return published;
}

}  // namespace fsd
