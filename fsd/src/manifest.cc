// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong

#include "fsd/manifest.h"

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include "fsd/md5.h"
#include "fsd/sha256.h"
#include "fsd/text_util.h"

namespace fsd {
namespace {

Status WriteExclusiveTemp(std::string_view payload,
                          std::filesystem::path* path) {
  std::string pattern =
      (std::filesystem::temp_directory_path() / "fsd-XXXXXX").string();
  const int descriptor = ::mkstemp(pattern.data());
  if (descriptor < 0) {
    return Status::Failure(ErrorCode::kIo,
                           "cannot create the manifest self-test file");
  }
  const ssize_t wrote = ::write(descriptor, payload.data(), payload.size());
  const int closed = ::close(descriptor);
  *path = pattern;
  if (wrote != static_cast<ssize_t>(payload.size()) || closed != 0) {
    std::filesystem::remove(*path);
    return Status::Failure(ErrorCode::kIo,
                           "cannot write the manifest self-test file");
  }
  return Status::Ok();
}

constexpr std::uint64_t kMaxManifestBytes = 64 * 1024;
constexpr std::size_t kMaxLineLength = 4096;
constexpr int kMaxLines = 200;

Status AssignString(std::string* field, std::string value, std::string_view key,
                    int line_number) {
  if (!field->empty()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "manifest line " + std::to_string(line_number) +
                               ": duplicate field " + std::string(key));
  }
  if (value.empty()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "manifest line " + std::to_string(line_number) +
                               ": empty field " + std::string(key));
  }
  *field = std::move(value);
  return Status::Ok();
}

Status AssignSize(std::optional<std::uint64_t>* field, std::string_view value,
                  std::string_view key, int line_number) {
  if (field->has_value()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "manifest line " + std::to_string(line_number) +
                               ": duplicate field " + std::string(key));
  }
  const std::optional<std::uint64_t> parsed = ParseUint64(value);
  if (!parsed.has_value()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "manifest line " + std::to_string(line_number) +
                               ": field " + std::string(key) +
                               " is not a decimal integer");
  }
  *field = *parsed;
  return Status::Ok();
}

}  // namespace

Status LoadManifest(const std::filesystem::path& path,
                    FirmwareManifest* manifest) {
  if (manifest == nullptr) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "manifest output argument is null");
  }

  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return Status::Failure(
        ErrorCode::kIo,
        "manifest is missing or is not a regular file: " + path.string());
  }
  const std::uint64_t file_size = std::filesystem::file_size(path, error);
  if (error) {
    return Status::Failure(ErrorCode::kIo,
                           "cannot read manifest size: " + error.message());
  }
  if (file_size > kMaxManifestBytes) {
    return Status::Failure(ErrorCode::kSizeLimit, "manifest exceeds 64 KiB");
  }

  std::ifstream input(path);
  if (!input) {
    return Status::Failure(ErrorCode::kIo,
                           "cannot open manifest: " + path.string());
  }

  FirmwareManifest parsed;
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line_number > kMaxLines) {
      return Status::Failure(ErrorCode::kSizeLimit,
                             "manifest has too many lines");
    }
    if (line.size() > kMaxLineLength) {
      return Status::Failure(
          ErrorCode::kInvalidArgument,
          "manifest line " + std::to_string(line_number) + " is too long");
    }
    if (line_number == 1 && line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
      line.erase(0, 3);
    }

    const std::string_view trimmed = Trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    const std::size_t separator = trimmed.find('=');
    if (separator == std::string_view::npos) {
      return Status::Failure(
          ErrorCode::kInvalidArgument,
          "manifest line " + std::to_string(line_number) + " is missing '='");
    }
    const std::string key(Trim(trimmed.substr(0, separator)));
    const std::string value(Trim(trimmed.substr(separator + 1)));

    Status status = Status::Ok();
    if (key == "name") {
      status = AssignString(&parsed.name, value, key, line_number);
    } else if (key == "url") {
      status = AssignString(&parsed.url, value, key, line_number);
    } else if (key == "sha256") {
      const std::optional<std::string> hex = NormalizeSha256Hex(value);
      if (!hex.has_value()) {
        status =
            Status::Failure(ErrorCode::kInvalidArgument,
                            "manifest line " + std::to_string(line_number) +
                                ": sha256 must be 64 hexadecimal digits");
      } else {
        status = AssignString(&parsed.sha256_hex, *hex, key, line_number);
      }
    } else if (key == "md5") {
      const std::optional<std::string> hex = NormalizeMd5Hex(value);
      if (!hex.has_value()) {
        status =
            Status::Failure(ErrorCode::kInvalidArgument,
                            "manifest line " + std::to_string(line_number) +
                                ": md5 must be 32 hexadecimal digits");
      } else {
        status = AssignString(&parsed.md5_hex, *hex, key, line_number);
      }
    } else if (key == "output") {
      if (!parsed.output_path.empty()) {
        status =
            Status::Failure(ErrorCode::kInvalidArgument,
                            "manifest line " + std::to_string(line_number) +
                                ": duplicate field output");
      } else if (value.empty()) {
        status =
            Status::Failure(ErrorCode::kInvalidArgument,
                            "manifest line " + std::to_string(line_number) +
                                ": empty field output");
      } else {
        parsed.output_path = value;
      }
    } else if (key == "size") {
      status = AssignSize(&parsed.size_bytes, value, key, line_number);
    } else if (key == "max_bytes") {
      status = AssignSize(&parsed.max_bytes, value, key, line_number);
    } else {
      status = Status::Failure(ErrorCode::kInvalidArgument,
                               "manifest line " + std::to_string(line_number) +
                                   ": unknown field " + key);
    }
    if (!status.ok()) {
      return status;
    }
  }
  if (input.bad()) {
    return Status::Failure(ErrorCode::kIo, "error while reading the manifest");
  }

  if (parsed.url.empty() || parsed.output_path.empty() ||
      (parsed.sha256_hex.empty() && parsed.md5_hex.empty())) {
    return Status::Failure(
        ErrorCode::kInvalidArgument,
        "manifest requires url, output, and either sha256 or md5");
  }

  *manifest = std::move(parsed);
  return Status::Ok();
}

Status RunManifestSelfTest() {
  std::filesystem::path path;
  if (Status status = WriteExclusiveTemp(
          "# self-test\n"
          "name=self-test\n"
          "url=https://example.com/firmware.bin\n"
          "sha256="
          "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\n"
          "size=3\n"
          "output=/tmp/fsd-selftest.bin\n",
          &path);
      !status.ok()) {
    return status;
  }

  FirmwareManifest manifest;
  const Status loaded = LoadManifest(path, &manifest);
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  if (!loaded.ok()) {
    return loaded;
  }
  if (manifest.name != "self-test" || manifest.size_bytes != 3 ||
      manifest.sha256_hex !=
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "manifest parse result did not match expectations");
  }

  if (Status status = WriteExclusiveTemp(
          "url=https://example.com/firmware.bin\n"
          "sha256="
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
          "typo=1\n",
          &path);
      !status.ok()) {
    return status;
  }
  FirmwareManifest rejected;
  const Status bad = LoadManifest(path, &rejected);
  std::filesystem::remove(path, remove_error);
  if (bad.ok() || bad.code() != ErrorCode::kInvalidArgument) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "unknown manifest fields must be rejected");
  }

  if (Status status =
          WriteExclusiveTemp("url=https://example.com/firmware.bin\n"
                             "md5=D41D8CD98F00B204E9800998ECF8427E\n"
                             "output=/tmp/fsd-md5.bin\n",
                             &path);
      !status.ok()) {
    return status;
  }
  FirmwareManifest md5_only;
  const Status md5_loaded = LoadManifest(path, &md5_only);
  std::filesystem::remove(path, remove_error);
  if (!md5_loaded.ok() ||
      md5_only.md5_hex != "d41d8cd98f00b204e9800998ecf8427e" ||
      !md5_only.sha256_hex.empty()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "a manifest with only MD5 must be accepted");
  }
  return Status::Ok();
}

}  // namespace fsd
