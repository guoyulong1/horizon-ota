// ota-fetch：设备端 M1 最小实现。
// 取发布清单 + 签名（fsd::HttpClient，无摘要）→ 验签（OpenSSL）→ 解析（nlohmann）
// → 适用性判断 → 用 fsd::SecureDownloader 下载各组件（带清单里的哈希）。
//
// 链接 fsd 静态库；不依赖 pack。
//
// 用法:
//   ota-fetch --base-url <url> --manifest <rel-path> --out-dir <dir>
//             --pubkey <64-hex> [--ca-file <pem>] [--model <m>]
//             [--version-code <n>] [--hardware-compat <id>]... [--layout <l>]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "crypto.h"
#include "fsd/http_client.h"
#include "fsd/secure_downloader.h"
#include "fsd/status.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr std::uint64_t kManifestMaxBytes = 64 * 1024;
constexpr std::uint64_t kSizeSlackBytes = 16 * 1024 * 1024;

constexpr const char* kUsage =
    "Usage: ota-fetch --base-url <url> --manifest <rel-path> --out-dir <dir>\n"
    "                --pubkey <64-hex> [--ca-file <pem>] [--model <m>]\n"
    "                [--version-code <n>] [--hardware-compat <id>]... "
    "[--layout <l>]\n";

std::string ValueOf(int argc, char** argv, int* index, const char* flag) {
  if (*index + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires a value");
  }
  ++(*index);
  return argv[*index];
}

std::string ReadAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

std::string UrlJoin(const std::string& base, const std::string& path) {
  if (!base.empty() && base.back() == '/') {
    return base + path;
  }
  return base + "/" + path;
}

// 用 fsd::HttpClient 取小文件（不要求摘要），写到 path。
void FetchToFile(const std::string& url, const fs::path& path,
                 const std::string& ca_file) {
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    throw std::runtime_error("cannot open " + path.string());
  }
  fsd::HttpRequest request;
  request.url = url;
  request.destination_file = file;
  request.max_bytes = kManifestMaxBytes;
  request.transfer_timeout_seconds = 60;
  request.ca_file = ca_file;

  fsd::HttpClient client;
  fsd::HttpResult result;
  const fsd::Status status = client.Download(request, &result);
  std::fclose(file);
  if (!status.ok()) {
    throw std::runtime_error("fetch " + url + " failed: " +
                             std::string(status.message()));
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string base_url;
    std::string manifest_rel;
    std::string out_dir;
    std::string pubkey_hex;
    std::string ca_file;
    std::optional<std::string> model;
    std::optional<long long> version_code;
    std::vector<std::string> hardware_ids;
    std::optional<std::string> layout;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--base-url") {
        base_url = ValueOf(argc, argv, &i, "--base-url");
      } else if (arg == "--manifest") {
        manifest_rel = ValueOf(argc, argv, &i, "--manifest");
      } else if (arg == "--out-dir") {
        out_dir = ValueOf(argc, argv, &i, "--out-dir");
      } else if (arg == "--pubkey") {
        pubkey_hex = ValueOf(argc, argv, &i, "--pubkey");
      } else if (arg == "--ca-file") {
        ca_file = ValueOf(argc, argv, &i, "--ca-file");
      } else if (arg == "--model") {
        model = ValueOf(argc, argv, &i, "--model");
      } else if (arg == "--version-code") {
        version_code =
            std::stoll(ValueOf(argc, argv, &i, "--version-code"));
      } else if (arg == "--hardware-compat") {
        hardware_ids.push_back(ValueOf(argc, argv, &i, "--hardware-compat"));
      } else if (arg == "--layout") {
        layout = ValueOf(argc, argv, &i, "--layout");
      } else if (arg == "--help" || arg == "-h") {
        std::cout << kUsage;
        return 0;
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }

    if (base_url.empty() || manifest_rel.empty() || out_dir.empty() ||
        pubkey_hex.empty()) {
      std::cerr << kUsage;
      return 2;
    }

    std::string raw_key;
    if (!ota::ParseHexPublicKey(pubkey_hex, &raw_key)) {
      throw std::runtime_error(
          "--pubkey must be 64 hex digits (32-byte Ed25519 public key)");
    }

    const fs::path work(out_dir);
    fs::create_directories(work);
    const fs::path manifest_path = work / "manifest.json";
    const fs::path signature_path = work / "manifest.json.sig";

    const std::string manifest_url = UrlJoin(base_url, manifest_rel);
    FetchToFile(manifest_url, manifest_path, ca_file);
    FetchToFile(manifest_url + ".sig", signature_path, ca_file);

    const std::string manifest_bytes = ReadAll(manifest_path);
    const std::string signature = ReadAll(signature_path);
    if (signature.size() != 64) {
      throw std::runtime_error("signature file is not 64 bytes");
    }
    if (!ota::VerifyEd25519(raw_key, manifest_bytes, signature)) {
      throw std::runtime_error("manifest signature verification failed");
    }

    const json manifest = json::parse(manifest_bytes);
    if (!manifest.contains("schema_version") ||
        manifest.at("schema_version").get<int>() != 1) {
      throw std::runtime_error("unsupported schema_version");
    }

    if (model.has_value() &&
        manifest.value("model", std::string()) != *model) {
      throw std::runtime_error("model mismatch: manifest=" +
                               manifest.value("model", std::string()) +
                               " device=" + *model);
    }
    if (version_code.has_value()) {
      const long long manifest_version = manifest.value("version_code", 0LL);
      const long long min_version = manifest.value("min_version_code", 0LL);
      if (*version_code < min_version) {
        throw std::runtime_error(
            "needs intermediate version (min_version_code=" +
            std::to_string(min_version) + ")");
      }
      if (manifest_version <= *version_code) {
        throw std::runtime_error(
            "manifest is not newer (version_code=" +
            std::to_string(manifest_version) + ")");
      }
    }
    if (!hardware_ids.empty()) {
      bool compatible = false;
      if (manifest.contains("hardware_compat")) {
        for (const auto& id : manifest["hardware_compat"]) {
          for (const auto& device_id : hardware_ids) {
            if (id.get<std::string>() == device_id) {
              compatible = true;
              break;
            }
          }
          if (compatible) {
            break;
          }
        }
      }
      if (!compatible) {
        throw std::runtime_error("hardware_compat mismatch");
      }
    }
    if (layout.has_value()) {
      bool found = false;
      if (manifest.contains("install") &&
          manifest["install"].contains("layouts")) {
        for (const auto& entry : manifest["install"]["layouts"]) {
          if (entry.get<std::string>() == *layout) {
            found = true;
            break;
          }
        }
      }
      if (!found) {
        throw std::runtime_error("layout not supported: " + *layout);
      }
    }

    for (const auto& component : manifest.at("components")) {
      const std::string path = component.at("path").get<std::string>();
      const std::uint64_t size = component.at("size").get<std::uint64_t>();

      fsd::DownloadOptions options;
      options.package_name = component.at("name").get<std::string>();
      options.url = UrlJoin(base_url, path);
      options.output_path = work / fs::path(path).filename();
      options.expected_sha256 = component.at("sha256").get<std::string>();
      options.expected_size = size;
      options.max_bytes = size + kSizeSlackBytes;
      options.transfer_timeout_seconds = 3600;
      options.resume = true;
      options.force = false;
      options.ca_file = ca_file;

      fsd::SecureDownloader downloader;
      const fsd::Status status = downloader.Download(options);
      if (!status.ok()) {
        throw std::runtime_error("download " + options.package_name +
                                 " failed: " + std::string(status.message()));
      }
      std::cout << "downloaded " << options.package_name << " -> "
                << options.output_path.string() << "\n";
    }

    std::cout << "ota-fetch: OK (release_id="
              << manifest.value("release_id", std::string()) << ")\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ota-fetch: " << error.what() << "\n";
    return 1;
  }
}
