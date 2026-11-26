// ota-agent：设备端 OTA 代理（合并原 ota-fetch 的取清单/验签/下载能力）。
//
// 用法:
//   ota-agent check [--once] --config <json> [--verbose]
//   ota-agent status --config <json>
#include <cstdlib>
#include <iostream>
#include <string>

#include "agent.h"
#include "config.h"
#include "state.h"

namespace {

constexpr const char* kUsage =
    "Usage:\n"
    "  ota-agent check [--once] --config <json> [--verbose]\n"
    "  ota-agent status --config <json>\n";

struct Options {
  std::string config_path;
  bool verbose = false;
};

bool ParseOptions(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config") {
      if (i + 1 >= argc) {
        return false;
      }
      options->config_path = argv[++i];
    } else if (arg == "--verbose") {
      options->verbose = true;
    } else if (arg == "--once") {
      // M2 只有一次性执行，保留参数以便 cron/systemd 调用。
    } else if (arg == "--help" || arg == "-h") {
      std::cout << kUsage;
      std::exit(0);
    } else {
      return false;
    }
  }
  return !options->config_path.empty();
}

int CheckCommand(int argc, char** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    std::cerr << kUsage;
    return 2;
  }
  ota::Config config;
  std::string error;
  if (!ota::LoadConfig(options.config_path, &config, &error)) {
    std::cerr << "ota-agent: config: " << error << "\n";
    return 1;
  }
  const ota::RunResult result = ota::RunOnce(config, options.verbose);
  std::cout << "phase=" << result.phase << " " << result.message << "\n";
  return result.exit_code;
}

int StatusCommand(int argc, char** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    std::cerr << kUsage;
    return 2;
  }
  ota::Config config;
  std::string error;
  if (!ota::LoadConfig(options.config_path, &config, &error)) {
    std::cerr << "ota-agent: config: " << error << "\n";
    return 1;
  }
  ota::AgentState state;
  if (!ota::LoadState(config.state_file, &state, &error)) {
    std::cerr << "ota-agent: state: " << error << "\n";
    return 1;
  }
  std::cout << "phase=" << state.phase << "\n"
            << "release_id=" << state.release_id << "\n"
            << "security_version_floor=" << state.security_version_floor << "\n"
            << "last_error=" << state.last_error << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << kUsage;
    return 2;
  }
  const std::string command = argv[1];
  if (command == "check") {
    return CheckCommand(argc - 1, argv + 1);
  }
  if (command == "status") {
    return StatusCommand(argc - 1, argv + 1);
  }
  std::cerr << kUsage;
  return 2;
}
