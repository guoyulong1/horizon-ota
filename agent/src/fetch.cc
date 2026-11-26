#include "fetch.h"

#include <cstdio>
#include <stdexcept>

#include "fsd/http_client.h"
#include "fsd/status.h"

namespace ota {
namespace {
constexpr std::uint64_t kSmallFileMaxBytes = 64 * 1024;
}  // namespace

bool FetchToFile(const std::string& url, const std::string& path,
                 const std::string& ca_file, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    *error = "cannot open " + path;
    return false;
  }
  fsd::HttpRequest request;
  request.url = url;
  request.destination_file = file;
  request.max_bytes = kSmallFileMaxBytes;
  request.transfer_timeout_seconds = 60;
  request.ca_file = ca_file;

  fsd::HttpClient client;
  fsd::HttpResult result;
  const fsd::Status status = client.Download(request, &result);
  std::fclose(file);
  if (!status.ok()) {
    *error = "fetch " + url + " failed: " + std::string(status.message());
    return false;
  }
  return true;
}

}  // namespace ota
