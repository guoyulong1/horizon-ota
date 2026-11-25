// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// 命令行入口。下载参数来自清单文件，或来自 --url/--sha256/--output。
// 摘要始终由调用方提供，程序不会自己从下载源再取一份“官方校验值”。

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "fsd/manifest.h"
#include "fsd/md5.h"
#include "fsd/secure_downloader.h"
#include "fsd/sha256.h"
#include "fsd/text_util.h"
#include "fsd/version.h"

namespace {
constexpr std::string_view kUsage =
    "Firmware secure download demo\n"
    "\n"
    "Usage:\n"
    "  firmware_secure_download --self-test\n"
    "  firmware_secure_download --manifest <file>\n"
    "  firmware_secure_download --url <https://...> --output <path>\n"
    "                            (--sha256 <64 hex digits> | --md5 <32 hex "
    "digits>)\n"
    "\n"
    "Options:\n"
    "  --sha256 <64 hex digits>  SHA-256. Provide this or --md5\n"
    "  --md5 <32 hex digits>     MD5. Weaker than SHA-256; optional integrity "
    "check\n"
    "  --expected-size <bytes>   Exact size declared by the manifest\n"
    "  --max-bytes <bytes>       Maximum size, default 256MiB\n"
    "  --connect-timeout <sec>   Connect timeout, default 15\n"
    "  --timeout <sec>           Whole transfer timeout, default 300\n"
    "  --max-redirects <count>   Follow HTTPS redirects only, default 5\n"
    "  --ca-file <path>          CA certificate bundle\n"
    "  --pinned-pubkey <pin>     Public key pin, format sha256//BASE64\n"
    "  --resume                  Keep and resume the .part file\n"
    "  --force                   Replace an existing output file\n"
    "  --self-test               Run SHA-256, MD5, and manifest self-tests\n"
    "  -h, --help                Show this help\n"
    "\n"
    "These checks cannot be disabled: HTTPS only, verify the certificate and\n"
    "hostname, TLS 1.2 minimum. A digest mismatch deletes the temporary file.\n"
    "Author: GuoYuLong\n";

struct Cli {
  bool help = false;
  bool self_test = false;
  bool resume = false;
  bool force = false;
  std::optional<std::string> manifest_path;
  std::optional<std::string> url;
  std::optional<std::string> sha256;
  std::optional<std::string> md5;
  std::optional<std::string> output;
  std::optional<std::uint64_t> max_bytes;
  std::optional<std::uint64_t> expected_size;
  std::optional<long> connect_timeout;
  std::optional<long> timeout;
  std::optional<long> max_redirects;
  std::optional<std::string> ca_file;
  std::optional<std::string> pinned_public_key;
};

fsd::Status TakeValue(int argc, char** argv, int* index, std::string_view flag,
                      std::string_view* value) {
  if (*index + 1 >= argc) {
    return fsd::Status::Failure(
        fsd::ErrorCode::kInvalidArgument,
        "option " + std::string(flag) + " requires a value");
  }
  ++(*index);
  *value = argv[*index];
  return fsd::Status::Ok();
}

fsd::Status ParseCli(int argc, char** argv, Cli* cli) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      cli->help = true;
      continue;
    }
    if (arg == "--self-test") {
      cli->self_test = true;
      continue;
    }
    if (arg == "--resume") {
      cli->resume = true;
      continue;
    }
    if (arg == "--force") {
      cli->force = true;
      continue;
    }

    std::string_view value;
    fsd::Status taken = fsd::Status::Ok();
    if (arg == "--manifest") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->manifest_path = std::string(value);
      }
    } else if (arg == "--url") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->url = std::string(value);
      }
    } else if (arg == "--sha256") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->sha256 = std::string(value);
      }
    } else if (arg == "--md5") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->md5 = std::string(value);
      }
    } else if (arg == "--output") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->output = std::string(value);
      }
    } else if (arg == "--max-bytes") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->max_bytes = fsd::ParseUint64(value);
        if (!cli->max_bytes.has_value()) {
          return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                      "--max-bytes is not an integer");
        }
      }
    } else if (arg == "--expected-size") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->expected_size = fsd::ParseUint64(value);
        if (!cli->expected_size.has_value()) {
          return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                      "--expected-size is not an integer");
        }
      }
    } else if (arg == "--connect-timeout") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->connect_timeout = fsd::ParseLong(value);
        if (!cli->connect_timeout.has_value()) {
          return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                      "--connect-timeout is not an integer");
        }
      }
    } else if (arg == "--timeout") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->timeout = fsd::ParseLong(value);
        if (!cli->timeout.has_value()) {
          return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                      "--timeout is not an integer");
        }
      }
    } else if (arg == "--max-redirects") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->max_redirects = fsd::ParseLong(value);
        if (!cli->max_redirects.has_value()) {
          return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                      "--max-redirects is not an integer");
        }
      }
    } else if (arg == "--ca-file") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->ca_file = std::string(value);
      }
    } else if (arg == "--pinned-pubkey") {
      taken = TakeValue(argc, argv, &index, arg, &value);
      if (taken.ok()) {
        cli->pinned_public_key = std::string(value);
      }
    } else {
      return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                  "unrecognized argument " + std::string(arg));
    }
    if (!taken.ok()) {
      return taken;
    }
  }
  return fsd::Status::Ok();
}

void ApplyOverrides(const Cli& cli, fsd::DownloadOptions* options) {
  options->resume = cli.resume;
  options->force = cli.force;
  if (cli.max_bytes.has_value()) {
    options->max_bytes = *cli.max_bytes;
  }
  if (cli.expected_size.has_value()) {
    options->expected_size = cli.expected_size;
  }
  if (cli.connect_timeout.has_value()) {
    options->connect_timeout_seconds = *cli.connect_timeout;
  }
  if (cli.timeout.has_value()) {
    options->transfer_timeout_seconds = *cli.timeout;
  }
  if (cli.max_redirects.has_value()) {
    options->max_redirects = *cli.max_redirects;
  }
  if (cli.ca_file.has_value()) {
    options->ca_file = *cli.ca_file;
  }
  if (cli.pinned_public_key.has_value()) {
    options->pinned_public_key = *cli.pinned_public_key;
  }
}

fsd::Status BuildOptions(const Cli& cli, fsd::DownloadOptions* options) {
  if (cli.manifest_path.has_value() &&
      (cli.url.has_value() || cli.sha256.has_value() || cli.md5.has_value() ||
       cli.output.has_value())) {
    return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                "--manifest cannot be combined with --url, "
                                "--sha256, --md5, or --output");
  }

  if (cli.manifest_path.has_value()) {
    fsd::FirmwareManifest manifest;
    if (fsd::Status status = fsd::LoadManifest(*cli.manifest_path, &manifest);
        !status.ok()) {
      return status;
    }
    options->package_name = std::move(manifest.name);
    options->url = std::move(manifest.url);
    options->expected_sha256 = std::move(manifest.sha256_hex);
    options->expected_md5 = std::move(manifest.md5_hex);
    options->output_path = std::move(manifest.output_path);
    options->expected_size = manifest.size_bytes;
    if (manifest.max_bytes.has_value()) {
      options->max_bytes = *manifest.max_bytes;
    }
  } else {
    if (!cli.url.has_value() || !cli.output.has_value() ||
        (!cli.sha256.has_value() && !cli.md5.has_value())) {
      return fsd::Status::Failure(fsd::ErrorCode::kInvalidArgument,
                                  "provide --manifest, or --url, --output, and "
                                  "either --sha256 or --md5");
    }
    options->url = *cli.url;
    options->output_path = *cli.output;
    options->package_name = "command-line";
    if (cli.sha256.has_value()) {
      options->expected_sha256 = *cli.sha256;
    }
    if (cli.md5.has_value()) {
      options->expected_md5 = *cli.md5;
    }
  }

  ApplyOverrides(cli, options);
  return fsd::Status::Ok();
}

int Fail(const fsd::Status& status) {
  std::cerr << "error[" << fsd::ErrorCodeName(status.code())
            << "]: " << status.message() << '\n';
  return static_cast<int>(status.code());
}

}  // namespace

int main(int argc, char** argv) {
  Cli cli;
  if (fsd::Status status = ParseCli(argc, argv, &cli); !status.ok()) {
    std::cerr << kUsage << '\n';
    return Fail(status);
  }
  if (cli.help || argc == 1) {
    std::cout << kUsage;
    return cli.help ? EXIT_SUCCESS
                    : static_cast<int>(fsd::ErrorCode::kInvalidArgument);
  }
  if (cli.self_test) {
    std::cerr << "Firmware secure download demo " << fsd::kProjectVersion
              << "  author " << fsd::kProjectAuthor << '\n';
    if (fsd::Status status = fsd::RunSha256SelfTest(); !status.ok()) {
      return Fail(status);
    }
    if (fsd::Status status = fsd::RunMd5SelfTest(); !status.ok()) {
      return Fail(status);
    }
    if (fsd::Status status = fsd::RunManifestSelfTest(); !status.ok()) {
      return Fail(status);
    }
    std::cout << "Self-test passed: SHA-256, MD5 vectors, and manifest parsing "
                 "match expectations.\n";
    return EXIT_SUCCESS;
  }

  std::cerr << "Firmware secure download demo " << fsd::kProjectVersion
            << "  author " << fsd::kProjectAuthor << '\n';
  fsd::DownloadOptions options;
  if (fsd::Status status = BuildOptions(cli, &options); !status.ok()) {
    return Fail(status);
  }
  fsd::SecureDownloader downloader;
  if (fsd::Status status = downloader.Download(options); !status.ok()) {
    return Fail(status);
  }
  return EXIT_SUCCESS;
}
