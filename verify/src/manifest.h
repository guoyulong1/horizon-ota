// 发布清单模型、解析与适用性判断（纯函数，便于单元测试）。
#ifndef OTA_VERIFY_MANIFEST_H_
#define OTA_VERIFY_MANIFEST_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ota {

struct Component {
  std::string name;
  std::string path;
  std::string sha256;
  std::uint64_t size = 0;
  std::string target;
};

struct Manifest {
  int schema_version = 0;
  std::string release_id;
  std::string version;
  long long version_code = 0;
  long long security_version = 0;
  std::string model;
  std::vector<std::string> hardware_compat;
  long long min_version_code = 0;
  std::string created_at;
  std::string install_mode;
  std::vector<std::string> layouts;
  std::vector<Component> components;
};

struct DeviceContext {
  std::string model;
  long long version_code = 0;
  long long security_version_floor = 0;
  std::vector<std::string> hardware_compat;
  std::optional<std::string> layout;
};

enum class RejectReason {
  kNone,
  kUnsupportedSchema,
  kModelMismatch,
  kHardwareMismatch,
  kLayoutUnsupported,
  kRollback,
  kNeedsIntermediate,
  kNotNewer,
};

struct Decision {
  bool accepted = false;
  RejectReason reason = RejectReason::kNone;
  std::string message;
};

// 解析发布清单 JSON；失败时写 error。
bool ParseManifest(const std::string& text, Manifest* out, std::string* error);

// 按 docs/OTA.md 3.5 的规则判断适用性（纯函数，不访问 IO）。
Decision CheckApplicability(const Manifest& manifest, const DeviceContext& device);

}  // namespace ota

#endif  // OTA_VERIFY_MANIFEST_H_
