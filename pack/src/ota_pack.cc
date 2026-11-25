// ota-pack：把一次构建的产物整理成一个待签名的发布目录。
// 只依赖 OpenSSL EVP（SHA-256）与 nlohmann/json；不依赖 fsd。
//
// 用法:
//   ota-pack --images <dir> --model <m> --version <v> --dtb <file>
//            --security-version <n> --min-version-code <n>
//            --hardware-compat <a,b,c> --output <dir>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "crypto.h"

namespace fs = std::filesystem;
using nlohmann::ordered_json;

namespace {

constexpr const char* kUsage =
    "Usage: ota-pack --images <dir> --model <m> --version <v> --dtb <file>\n"
    "                --security-version <n> --min-version-code <n>\n"
    "                --hardware-compat <a,b,c> --output <dir>\n";

bool HasUnsafeChar(const std::string& value) {
  return value.find_first_of("\"\\") != std::string::npos ||
         value.find(' ') != std::string::npos;
}

long long VersionCode(const std::string& version) {
  int major = 0;
  int minor = 0;
  int patch = 0;
  int consumed = 0;
  if (std::sscanf(version.c_str(), "%d.%d.%d%n", &major, &minor, &patch,
                  &consumed) != 3 ||
      consumed != static_cast<int>(version.size())) {
    throw std::runtime_error(
        "version must be exactly major.minor.patch: " + version);
  }
  // version_code = major*10000 + minor*100 + patch，要求 minor/patch 在 0..99，
  // 否则会和相邻主版本号撞值（例如 1.100.0 与 2.0.0）。
  if (major < 0 || minor < 0 || patch < 0 || minor > 99 || patch > 99) {
    throw std::runtime_error(
        "version minor and patch must be between 0 and 99: " + version);
  }
  return static_cast<long long>(major) * 10000 + minor * 100 + patch;
}

std::string NowUtc() {
  std::time_t now = std::time(nullptr);
  // 支持可复现打包：固定 SOURCE_DATE_EPOCH 时，重复打包得到相同清单字节。
  if (const char* epoch = std::getenv("SOURCE_DATE_EPOCH")) {
    try {
      now = static_cast<std::time_t>(std::stoll(epoch));
    } catch (const std::exception&) {
      throw std::runtime_error("SOURCE_DATE_EPOCH is not a valid integer");
    }
  }
  const std::tm* tm = std::gmtime(&now);
  if (tm == nullptr) {
    throw std::runtime_error("cannot format current time");
  }
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", tm);
  return buffer;
}

struct Component {
  std::string name;
  std::string file;
  std::string sha256;
  std::string target;
  std::uint64_t size = 0;
};

std::string ValueOf(int argc, char** argv, int* index, const char* flag) {
  if (*index + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires a value");
  }
  ++(*index);
  return argv[*index];
}

}  // namespace

int main(int argc, char** argv) {
  fs::path release_dir;
  bool created = false;
  try {
    std::string images;
    std::string model;
    std::string version;
    std::string dtb;
    std::vector<std::string> hardware_ids;
    std::string output;
    long long security_version = -1;
    long long min_version_code = -1;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--images") {
        images = ValueOf(argc, argv, &i, "--images");
      } else if (arg == "--model") {
        model = ValueOf(argc, argv, &i, "--model");
      } else if (arg == "--version") {
        version = ValueOf(argc, argv, &i, "--version");
      } else if (arg == "--dtb") {
        dtb = ValueOf(argc, argv, &i, "--dtb");
      } else if (arg == "--security-version") {
        security_version = std::stoll(ValueOf(argc, argv, &i, "--security-version"));
      } else if (arg == "--min-version-code") {
        min_version_code = std::stoll(ValueOf(argc, argv, &i, "--min-version-code"));
      } else if (arg == "--hardware-compat") {
        hardware_ids.push_back(ValueOf(argc, argv, &i, "--hardware-compat"));
      } else if (arg == "--output") {
        output = ValueOf(argc, argv, &i, "--output");
      } else if (arg == "--help" || arg == "-h") {
        std::cout << kUsage;
        return 0;
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }

    if (images.empty() || model.empty() || version.empty() || dtb.empty() ||
        hardware_ids.empty() || output.empty() || security_version < 0 ||
        min_version_code < 0) {
      std::cerr << kUsage;
      return 2;
    }
    if (HasUnsafeChar(model) || HasUnsafeChar(version) || HasUnsafeChar(dtb)) {
      throw std::runtime_error(
          "model, version or dtb must not contain quotes, backslash or space");
    }
    const long long version_code = VersionCode(version);
    if (min_version_code > version_code) {
      throw std::runtime_error(
          "min-version-code must not exceed version_code");
    }

    const fs::path images_dir(images);
    const fs::path kernel = images_dir / "Image";
    const fs::path rootfs = images_dir / "rootfs.squashfs";
    const fs::path dtb_path = images_dir / dtb;
    for (const fs::path& path : {kernel, rootfs, dtb_path}) {
      if (!fs::is_regular_file(path)) {
        throw std::runtime_error("missing component: " + path.string());
      }
    }

    const std::vector<Component> components = {
        {"kernel", "Image", ota::Sha256FileHex(kernel.string()), "boot",
         static_cast<std::uint64_t>(fs::file_size(kernel))},
        {"dtb", dtb, ota::Sha256FileHex(dtb_path.string()), "boot",
         static_cast<std::uint64_t>(fs::file_size(dtb_path))},
        {"rootfs", "rootfs.squashfs", ota::Sha256FileHex(rootfs.string()),
         "rootfs", static_cast<std::uint64_t>(fs::file_size(rootfs))},
    };

    // release_id = 组件哈希列表（固定格式）的哈希前 12 位。
    std::ostringstream list;
    for (const auto& component : components) {
      list << component.sha256 << "  " << component.file << "\n";
    }
    const std::string release_id = ota::Sha256Hex(list.str()).substr(0, 12);

    release_dir = fs::path(output) / "releases" / model / version / release_id;
    const fs::path files_dir = release_dir / "files";
    fs::create_directories(files_dir);
    created = true;

    for (const auto& component : components) {
      const fs::path source = (component.name == "dtb")
                                  ? dtb_path
                                  : (component.name == "kernel" ? kernel : rootfs);
      fs::copy_file(source, files_dir / component.file,
                    fs::copy_options::overwrite_existing);
    }

    ordered_json hardware = ordered_json::array();
    for (const auto& hardware_id : hardware_ids) {
      if (hardware_id.empty()) {
        throw std::runtime_error("hardware-compat must not be empty");
      }
      if (hardware_id.find_first_of("\"\\") != std::string::npos) {
        throw std::runtime_error(
            "hardware-compat must not contain quotes or backslash");
      }
      hardware.push_back(hardware_id);
    }

    ordered_json layouts = ordered_json::array();
    layouts.push_back("ab");
    layouts.push_back("recovery");

    ordered_json manifest;
    manifest["schema_version"] = 1;
    manifest["release_id"] = release_id;
    manifest["version"] = version;
    manifest["version_code"] = version_code;
    manifest["security_version"] = security_version;
    manifest["model"] = model;
    manifest["hardware_compat"] = hardware;
    manifest["min_version_code"] = min_version_code;
    manifest["created_at"] = NowUtc();
    manifest["install"]["mode"] = "reboot-required";
    manifest["install"]["window"] = nullptr;
    manifest["install"]["reboot_delay_seconds"] = 60;
    manifest["install"]["layouts"] = layouts;

    ordered_json component_list = ordered_json::array();
    for (const auto& component : components) {
      ordered_json entry;
      entry["name"] = component.name;
      entry["path"] = "releases/" + model + "/" + version + "/" + release_id +
                      "/files/" + component.file;
      entry["sha256"] = component.sha256;
      entry["size"] = component.size;
      entry["target"] = component.target;
      component_list.push_back(entry);
    }
    manifest["components"] = component_list;

    const fs::path manifest_path = release_dir / "manifest.json";
    std::ofstream out(manifest_path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("cannot write " + manifest_path.string());
    }
    out << manifest.dump(2) << "\n";
    out.close();
    if (!out) {
      throw std::runtime_error("write failed: " + manifest_path.string());
    }

    std::cout << "release: " << release_dir.string() << "\n"
              << "release_id: " << release_id << "\n"
              << "manifest_sha256: " << ota::Sha256FileHex(manifest_path.string())
              << "\n";
    return 0;
  } catch (const std::exception& error) {
    if (created) {
      std::error_code ignored;
      fs::remove_all(release_dir, ignored);
    }
    std::cerr << "ota-pack: " << error.what() << "\n";
    return 1;
  }
}
