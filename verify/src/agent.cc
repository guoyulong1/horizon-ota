#include "agent.h"

#include "crypto.h"
#include "download.h"
#include "fetch.h"
#include "manifest.h"
#include "state.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ota {
namespace {

std::string JoinUrl(const std::string& base, const std::string& path) {
  if (!base.empty() && base.back() == '/') {
    return base + path;
  }
  return base + "/" + path;
}

bool ReadAll(const std::string& path, std::string* data) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  *data = std::string((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return true;
}

RunResult Fail(const Config& config, AgentState* state, const std::string& message) {
  state->phase = "error";
  state->last_error = message;
  std::string ignored;
  SaveState(config.state_file, *state, &ignored);
  RunResult result;
  result.exit_code = 1;
  result.phase = "error";
  result.message = message;
  return result;
}

}  // namespace

RunResult RunOnce(const Config& config, bool verbose) {
  AgentState state;
  std::string error;
  if (!LoadState(config.state_file, &state, &error)) {
    RunResult result;
    result.exit_code = 1;
    result.message = error;
    return result;
  }

  // ② 设备发现（M2 仅静态通道轮询；后续按 docs/OTA.md 第 3 章替换）。
  std::string manifest_rel = config.manifest_path;
  if (manifest_rel.empty()) {
    manifest_rel = "channels/" + config.channel + "/" + config.model +
                   "/manifest.json";
  }
  state.phase = "checking";
  SaveState(config.state_file, state, &error);

  // ③ 取清单 + 签名，验签。
  const std::string manifest_url = JoinUrl(config.base_url, manifest_rel);
  const std::filesystem::path work =
      std::filesystem::path(config.staging_dir) / "_manifest";
  std::filesystem::create_directories(work);
  const std::string manifest_file = (work / "manifest.json").string();
  const std::string signature_file = (work / "manifest.json.sig").string();

  state.phase = "fetching_manifest";
  SaveState(config.state_file, state, &error);
  if (!FetchToFile(manifest_url, manifest_file, config.ca_file, &error)) {
    return Fail(config, &state, error);
  }
  if (!FetchToFile(manifest_url + ".sig", signature_file, config.ca_file,
                   &error)) {
    return Fail(config, &state, error);
  }

  std::string manifest_bytes;
  std::string signature;
  if (!ReadAll(manifest_file, &manifest_bytes) ||
      !ReadAll(signature_file, &signature)) {
    return Fail(config, &state, "cannot read the fetched manifest");
  }
  std::string raw_key;
  if (!ParseHexPublicKey(config.pubkey_hex, &raw_key)) {
    return Fail(config, &state,
                "pubkey_hex must be 64 hex digits (32-byte Ed25519 key)");
  }
  if (signature.size() != 64 ||
      !VerifyEd25519(raw_key, manifest_bytes, signature)) {
    return Fail(config, &state, "manifest signature verification failed");
  }

  Manifest manifest;
  if (!ParseManifest(manifest_bytes, &manifest, &error)) {
    return Fail(config, &state, error);
  }

  // ④ 适用性判断。
  DeviceContext device;
  device.model = config.model;
  device.version_code = config.version_code;
  device.security_version_floor =
      state.security_version_floor > 0 ? state.security_version_floor
                                       : config.security_version_floor;
  device.hardware_compat = config.hardware_compat;
  if (!config.layout.empty()) {
    device.layout = config.layout;
  }
  const Decision decision = CheckApplicability(manifest, device);
  if (!decision.accepted) {
    state.phase = "idle";
    state.last_error = decision.message;
    SaveState(config.state_file, state, &error);
    RunResult result;
    result.exit_code = 0;
    result.phase = "idle";
    result.message = decision.message;
    return result;
  }

  // 提高防回滚下限（M2 存 state；U-Boot env 留 M3）。
  if (manifest.security_version > state.security_version_floor) {
    state.security_version_floor = manifest.security_version;
    SaveState(config.state_file, state, &error);
  }

  // ⑤ 下载。
  state.phase = "downloading";
  SaveState(config.state_file, state, &error);
  const std::filesystem::path staging =
      std::filesystem::path(config.staging_dir) / manifest.release_id;
  if (!DownloadComponents(manifest, config.base_url, staging.string(),
                          config.ca_file, verbose, &error)) {
    return Fail(config, &state, error);
  }

  // ⑦ 下载后复核：文件存在且大小一致（哈希在下载器内部已校验）。
  for (const auto& component : manifest.components) {
    const std::string filename =
        std::filesystem::path(component.path).filename().string();
    const std::filesystem::path path = staging / filename;
    if (!std::filesystem::exists(path)) {
      return Fail(config, &state, "missing component: " + path.string());
    }
  }

  state.phase = "downloaded";
  state.release_id = manifest.release_id;
  state.last_error.clear();
  SaveState(config.state_file, state, &error);

  RunResult result;
  result.exit_code = 0;
  result.phase = "downloaded";
  result.message = "downloaded release " + manifest.release_id;
  return result;
}

}  // namespace ota
