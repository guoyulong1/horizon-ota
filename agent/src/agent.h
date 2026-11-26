// OTA 代理状态机（M2：②–⑦，安装/确认/回滚为 M3 占位）。
// agent 是唯一的编排拥有者：验签与适用性判断调 verify 库，传输调 fsd。
#ifndef OTA_AGENT_AGENT_H_
#define OTA_AGENT_AGENT_H_

#include "config.h"

#include <string>

namespace ota {

struct RunResult {
  int exit_code = 0;
  std::string phase;
  std::string message;
};

// 跑一个周期：② 发现 → ③ 取清单验签 → ④ 适用性 → ⑤ 下载 → ⑦ 复核。
RunResult RunOnce(const Config& config, bool verbose);

}  // namespace ota

#endif  // OTA_VERIFY_AGENT_H_
