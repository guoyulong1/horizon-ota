#include "manifest.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace ota {
namespace {
using json = nlohmann::json;

std::string RequireString(const json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    throw std::runtime_error(std::string("missing or invalid string field: ") +
                             key);
  }
  return it->get<std::string>();
}

long long RequireInt(const json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_number_integer()) {
    throw std::runtime_error(std::string("missing or invalid integer field: ") +
                             key);
  }
  return it->get<long long>();
}

long long OptionalInt(const json& object, const char* key, long long fallback) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return fallback;
  }
  if (!it->is_number_integer()) {
    throw std::runtime_error(std::string("invalid integer field: ") + key);
  }
  return it->get<long long>();
}

std::vector<std::string> StringArray(const json& object, const char* key) {
  std::vector<std::string> values;
  const auto it = object.find(key);
  if (it == object.end()) {
    return values;
  }
  if (!it->is_array()) {
    throw std::runtime_error(std::string("field is not an array: ") + key);
  }
  for (const auto& item : *it) {
    values.push_back(item.get<std::string>());
  }
  return values;
}

bool Contains(const std::vector<std::string>& values, const std::string& target) {
  for (const auto& value : values) {
    if (value == target) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool ParseManifest(const std::string& text, Manifest* out, std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  try {
    const json root = json::parse(text);
    Manifest manifest;
    manifest.schema_version =
        static_cast<int>(RequireInt(root, "schema_version"));
    manifest.release_id = RequireString(root, "release_id");
    manifest.version = RequireString(root, "version");
    manifest.version_code = RequireInt(root, "version_code");
    manifest.security_version = RequireInt(root, "security_version");
    manifest.model = RequireString(root, "model");
    manifest.hardware_compat = StringArray(root, "hardware_compat");
    manifest.min_version_code = OptionalInt(root, "min_version_code", 0);
    manifest.created_at = RequireString(root, "created_at");

    const auto install = root.find("install");
    if (install != root.end() && install->is_object()) {
      manifest.install_mode = install->value("mode", std::string());
      manifest.layouts = StringArray(*install, "layouts");
    }

    const auto components = root.find("components");
    if (components == root.end() || !components->is_array()) {
      throw std::runtime_error("missing components array");
    }
    for (const auto& entry : *components) {
      Component component;
      component.name = RequireString(entry, "name");
      component.path = RequireString(entry, "path");
      component.sha256 = RequireString(entry, "sha256");
      component.size = static_cast<std::uint64_t>(RequireInt(entry, "size"));
      component.target = RequireString(entry, "target");
      manifest.components.push_back(std::move(component));
    }
    if (manifest.components.empty()) {
      throw std::runtime_error("components array is empty");
    }

    *out = std::move(manifest);
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

Decision CheckApplicability(const Manifest& manifest,
                            const DeviceContext& device) {
  Decision decision;
  if (manifest.schema_version != 1) {
    decision.reason = RejectReason::kUnsupportedSchema;
    decision.message =
        "unsupported schema_version " + std::to_string(manifest.schema_version);
    return decision;
  }
  if (!device.model.empty() && manifest.model != device.model) {
    decision.reason = RejectReason::kModelMismatch;
    decision.message =
        "model mismatch: manifest=" + manifest.model + " device=" + device.model;
    return decision;
  }
  if (!device.hardware_compat.empty() && !manifest.hardware_compat.empty() &&
      !Contains(manifest.hardware_compat, device.hardware_compat.front())) {
    bool compatible = false;
    for (const auto& device_id : device.hardware_compat) {
      if (Contains(manifest.hardware_compat, device_id)) {
        compatible = true;
        break;
      }
    }
    if (!compatible) {
      decision.reason = RejectReason::kHardwareMismatch;
      decision.message = "no matching hardware_compat";
      return decision;
    }
  }
  if (device.layout.has_value() && !manifest.layouts.empty() &&
      !Contains(manifest.layouts, *device.layout)) {
    decision.reason = RejectReason::kLayoutUnsupported;
    decision.message = "layout not supported: " + *device.layout;
    return decision;
  }
  if (manifest.security_version < device.security_version_floor) {
    decision.reason = RejectReason::kRollback;
    decision.message = "security_version " +
                       std::to_string(manifest.security_version) +
                       " is below the device floor " +
                       std::to_string(device.security_version_floor);
    return decision;
  }
  if (device.version_code < manifest.min_version_code) {
    decision.reason = RejectReason::kNeedsIntermediate;
    decision.message = "needs an intermediate version (min_version_code=" +
                       std::to_string(manifest.min_version_code) + ")";
    return decision;
  }
  if (manifest.version_code <= device.version_code) {
    decision.reason = RejectReason::kNotNewer;
    decision.message = "manifest is not newer (version_code=" +
                       std::to_string(manifest.version_code) + ")";
    return decision;
  }
  decision.accepted = true;
  return decision;
}

}  // namespace ota
