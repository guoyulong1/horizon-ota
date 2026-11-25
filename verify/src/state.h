// OTA 代理持久状态。原子写，掉电不写坏。
#ifndef OTA_VERIFY_STATE_H_
#define OTA_VERIFY_STATE_H_

#include <string>

namespace ota {

struct AgentState {
  int schema = 1;
  // idle | checking | fetching_manifest | downloading | downloaded |
  // waiting | installing | pending_reboot | trial_boot | confirmed |
  // rolled_back | error
  std::string phase = "idle";
  std::string release_id;
  std::string last_error;
  int retry = 0;
  long long security_version_floor = 0;
};

bool LoadState(const std::string& path, AgentState* out, std::string* error);
bool SaveState(const std::string& path, const AgentState& state,
               std::string* error);

}  // namespace ota

#endif  // OTA_VERIFY_STATE_H_
