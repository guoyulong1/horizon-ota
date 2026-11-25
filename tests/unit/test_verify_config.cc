// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// verify/config：设备端代理配置（JSON 文件）。覆盖完整解析、缺省值、
// 必填字段缺失、文件不存在与坏 JSON。

#include "config.h"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "fsd_test_util.h"

namespace ota {
namespace {

using fsd::testing::TempPathGuard;
using fsd::testing::WriteTempFile;

constexpr const char* kPubkeyHex =
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

// 写入临时配置文件；通过 path 取用，离开作用域自动清理。
std::filesystem::path WriteConfigFile(const std::string& text) {
  return WriteTempFile(text);
}

TEST(LoadConfig, ReadsCompleteConfig) {
  const std::string text = std::string(R"({
  "base_url": "https://ota.example.com/firmware",
  "model": "X3-ABC",
  "channel": "stable",
  "layout": "single",
  "staging_dir": "/tmp/ota-staging",
  "state_file": "/tmp/ota-state/state.json",
  "pubkey_hex": ")") + kPubkeyHex + R"(",
  "ca_file": "/etc/ota/ca.pem",
  "version_code": 500,
  "hardware_compat": ["x3m30x", "x3m30x-v2"],
  "security_version_floor": 3,
  "manifest_path": "manifests/custom.json"
})";
  const std::filesystem::path path = WriteConfigFile(text);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);

  Config config;
  std::string error;
  ASSERT_TRUE(LoadConfig(path.string(), &config, &error)) << error;
  EXPECT_EQ(config.base_url, "https://ota.example.com/firmware");
  EXPECT_EQ(config.model, "X3-ABC");
  EXPECT_EQ(config.channel, "stable");
  EXPECT_EQ(config.layout, "single");
  EXPECT_EQ(config.staging_dir, "/tmp/ota-staging");
  EXPECT_EQ(config.state_file, "/tmp/ota-state/state.json");
  EXPECT_EQ(config.pubkey_hex, kPubkeyHex);
  EXPECT_EQ(config.ca_file, "/etc/ota/ca.pem");
  EXPECT_EQ(config.version_code, 500);
  ASSERT_EQ(config.hardware_compat.size(), 2U);
  EXPECT_EQ(config.hardware_compat[0], "x3m30x");
  EXPECT_EQ(config.security_version_floor, 3);
  EXPECT_EQ(config.manifest_path, "manifests/custom.json");
}

TEST(LoadConfig, AppliesDefaultsForOptionalFields) {
  const std::string text = std::string(R"({
  "base_url": "https://ota.example.com",
  "model": "X3-ABC",
  "pubkey_hex": ")") + kPubkeyHex + R"("
})";
  const std::filesystem::path path = WriteConfigFile(text);
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);

  Config config;
  std::string error;
  ASSERT_TRUE(LoadConfig(path.string(), &config, &error)) << error;
  EXPECT_EQ(config.channel, "beta");
  EXPECT_EQ(config.layout, "ab");
  EXPECT_EQ(config.staging_dir, "/data/ota/staging");
  EXPECT_EQ(config.state_file, "/data/ota/state.json");
  EXPECT_TRUE(config.ca_file.empty());
  EXPECT_TRUE(config.manifest_path.empty());
  EXPECT_EQ(config.version_code, 0);
  EXPECT_TRUE(config.hardware_compat.empty());
  EXPECT_EQ(config.security_version_floor, 0);
}

TEST(LoadConfig, RejectsMissingRequiredFields) {
  const std::string no_base_url =
      std::string(R"({ "model": "X3-ABC", "pubkey_hex": ")") + kPubkeyHex +
                  R"(" })";
  const std::string no_model =
      std::string(R"({ "base_url": "https://ota.example.com", "pubkey_hex": ")") +
                  kPubkeyHex + R"(" })";
  const std::string no_pubkey =
      R"({ "base_url": "https://ota.example.com", "model": "X3-ABC" })";

  Config config;
  std::string error;
  for (const std::string& text : {no_base_url, no_model, no_pubkey}) {
    const std::filesystem::path path = WriteConfigFile(text);
    ASSERT_FALSE(path.empty());
    TempPathGuard guard(path);
    EXPECT_FALSE(LoadConfig(path.string(), &config, &error)) << text;
    EXPECT_FALSE(error.empty());
  }
}

TEST(LoadConfig, RejectsMissingFileAndBadJson) {
  Config config;
  std::string error;
  EXPECT_FALSE(LoadConfig("/nonexistent/ota-agent.json", &config, &error));
  EXPECT_FALSE(error.empty());

  const std::filesystem::path path = WriteConfigFile("{ not json");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);
  EXPECT_FALSE(LoadConfig(path.string(), &config, &error));
}

TEST(LoadConfig, RejectsNullOutputArguments) {
  const std::filesystem::path path = WriteConfigFile("{}");
  ASSERT_FALSE(path.empty());
  TempPathGuard guard(path);

  Config config;
  std::string error;
  EXPECT_FALSE(LoadConfig(path.string(), nullptr, &error));
  EXPECT_FALSE(LoadConfig(path.string(), &config, nullptr));
}

}  // namespace
}  // namespace ota
