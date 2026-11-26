// 设备端代理配置（JSON 文件），地址/密钥/CA 不写死。
#ifndef OTA_AGENT_CONFIG_H_
#define OTA_AGENT_CONFIG_H_

#include <string>
#include <vector>

namespace ota {

struct Config {
  std::string base_url;
  std::string model;
  std::string channel = "beta";
  std::string layout = "ab";
  std::string staging_dir = "/data/ota/staging";
  std::string state_file = "/data/ota/state.json";
  std::string pubkey_hex;
  std::string ca_file;
  long long version_code = 0;
  std::vector<std::string> hardware_compat;
  long long security_version_floor = 0;
  // 直接指定清单相对路径（调试/测试用；为空则按通道轮询 ②）。
  std::string manifest_path;
};

bool LoadConfig(const std::string& path, Config* out, std::string* error);

}  // namespace ota

#endif  // OTA_VERIFY_CONFIG_H_
