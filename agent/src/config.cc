#include "config.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace ota {
namespace {
using json = nlohmann::json;

std::string OptionalString(const json& root, const char* key,
                           const std::string& fallback) {
  const auto it = root.find(key);
  if (it == root.end()) {
    return fallback;
  }
  return it->get<std::string>();
}
}  // namespace

bool LoadConfig(const std::string& path, Config* out, std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  try {
    std::ifstream in(path);
    if (!in) {
      throw std::runtime_error("cannot open config: " + path);
    }
    const json root = json::parse(in);
    Config config;
    config.base_url = root.at("base_url").get<std::string>();
    config.model = root.at("model").get<std::string>();
    config.channel = OptionalString(root, "channel", config.channel);
    config.layout = OptionalString(root, "layout", config.layout);
    config.staging_dir = OptionalString(root, "staging_dir", config.staging_dir);
    config.state_file = OptionalString(root, "state_file", config.state_file);
    config.pubkey_hex = root.at("pubkey_hex").get<std::string>();
    config.ca_file = OptionalString(root, "ca_file", config.ca_file);
    config.version_code = root.value("version_code", 0LL);
    if (root.contains("hardware_compat")) {
      config.hardware_compat =
          root.at("hardware_compat").get<std::vector<std::string>>();
    }
    config.security_version_floor =
        root.value("security_version_floor", 0LL);
    config.manifest_path = OptionalString(root, "manifest_path", "");
    *out = std::move(config);
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

}  // namespace ota
