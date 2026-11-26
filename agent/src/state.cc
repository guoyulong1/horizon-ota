#include "state.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace ota {

bool LoadState(const std::string& path, AgentState* out, std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  try {
    std::ifstream in(path);
    if (!in) {
      // 不存在视为初始状态。
      *out = AgentState{};
      return true;
    }
    const nlohmann::json root = nlohmann::json::parse(in);
    AgentState state;
    state.phase = root.value("phase", std::string("idle"));
    state.release_id = root.value("release_id", std::string());
    state.last_error = root.value("last_error", std::string());
    state.retry = root.value("retry", 0);
    state.security_version_floor =
        root.value("security_version_floor", 0LL);
    *out = state;
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

bool SaveState(const std::string& path, const AgentState& state,
               std::string* error) {
  if (error == nullptr) {
    return false;
  }
  try {
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
      std::filesystem::create_directories(target.parent_path());
    }
    const std::string temp = path + ".tmp";
    std::ofstream out(temp);
    if (!out) {
      throw std::runtime_error("cannot write " + temp);
    }
    nlohmann::json root;
    root["schema"] = state.schema;
    root["phase"] = state.phase;
    root["release_id"] = state.release_id;
    root["last_error"] = state.last_error;
    root["retry"] = state.retry;
    root["security_version_floor"] = state.security_version_floor;
    out << root.dump(2) << "\n";
    out.close();
    if (!out) {
      throw std::runtime_error("write failed: " + temp);
    }
    std::filesystem::rename(temp, target);
    return true;
  } catch (const std::exception& exception) {
    *error = exception.what();
    return false;
  }
}

}  // namespace ota
