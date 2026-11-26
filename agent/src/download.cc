#include "download.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "fsd/secure_downloader.h"
#include "fsd/status.h"

namespace ota {
namespace {
constexpr std::uint64_t kSizeSlackBytes = 16 * 1024 * 1024;

std::string JoinUrl(const std::string& base, const std::string& path) {
  if (!base.empty() && base.back() == '/') {
    return base + path;
  }
  return base + "/" + path;
}
}  // namespace

bool DownloadComponents(const Manifest& manifest, const std::string& base_url,
                        const std::string& staging_dir,
                        const std::string& ca_file, bool verbose,
                        std::string* error) {
  if (error == nullptr) {
    return false;
  }
  try {
    std::filesystem::create_directories(staging_dir);
    for (const auto& component : manifest.components) {
      const std::string filename =
          std::filesystem::path(component.path).filename().string();
      if (filename.empty()) {
        throw std::runtime_error("component path has no filename: " +
                                 component.path);
      }

      fsd::DownloadOptions options;
      options.package_name = component.name;
      options.url = JoinUrl(base_url, component.path);
      options.output_path = std::filesystem::path(staging_dir) / filename;
      options.expected_sha256 = component.sha256;
      options.expected_size = component.size;
      options.max_bytes = component.size + kSizeSlackBytes;
      options.transfer_timeout_seconds = 3600;
      options.resume = true;
      options.force = false;
      options.ca_file = ca_file;
      if (verbose) {
        options.on_progress = [](const fsd::DownloadProgress& progress) {
          std::cout << "  " << progress.package_name << ": "
                    << progress.received << " / " << progress.total
                    << " bytes\n";
        };
      }

      fsd::SecureDownloader downloader;
      const fsd::Status status = downloader.Download(options);
      if (!status.ok()) {
        throw std::runtime_error("download " + component.name +
                                 " failed: " + std::string(status.message()));
      }
    }
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

}  // namespace ota
