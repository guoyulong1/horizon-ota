// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// verify/agent：OTA 代理状态机的一个周期（RunOnce）。单元测试只覆盖
// 不依赖服务器的路径：状态文件损坏直接失败；清单不可达时把 error 阶段
// 原子写入状态文件。完整 ②–⑦ 链路由 tests/integration/m1-e2e.sh 覆盖。

#include "agent.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "fsd_test_util.h"
#include "state.h"

namespace ota {
namespace {

using fsd::testing::MakeTempDir;
using fsd::testing::TempPathGuard;

// 指向本机无人监听的端口：参数合法，连接立即被拒，不访问外网。
Config BaseConfig(const std::filesystem::path& dir) {
  Config config;
  config.base_url = "https://127.0.0.1:1";
  config.model = "X3-ABC";
  config.pubkey_hex =
      "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  config.staging_dir = (dir / "staging").string();
  config.state_file = (dir / "state.json").string();
  return config;
}

TEST(RunOnce, FailsWhenStateFileIsCorrupt) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);

  Config config = BaseConfig(dir);
  {
    std::ofstream out(config.state_file, std::ios::binary);
    out << "{ this is not json";
  }

  const RunResult result = RunOnce(config, false);
  EXPECT_EQ(result.exit_code, 1);
  EXPECT_FALSE(result.message.empty());
}

TEST(RunOnce, PersistsErrorStateWhenManifestIsUnreachable) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);

  Config config = BaseConfig(dir);
  const RunResult result = RunOnce(config, false);
  EXPECT_EQ(result.exit_code, 1);
  EXPECT_EQ(result.phase, "error");
  EXPECT_FALSE(result.message.empty());

  // 失败状态被原子落盘：phase=error、last_error 记录 fetch 失败原因。
  AgentState persisted;
  std::string error;
  ASSERT_TRUE(LoadState(config.state_file, &persisted, &error)) << error;
  EXPECT_EQ(persisted.phase, "error");
  EXPECT_NE(persisted.last_error.find("failed"), std::string::npos);

  // 清单工作目录已经建好，等待下次周期复用。
  EXPECT_TRUE(std::filesystem::is_directory(
      std::filesystem::path(config.staging_dir) / "_manifest"));
}

TEST(RunOnce, RecoversFromMissingStateFile) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);

  // 状态文件不存在是正常起点：RunOnce 不得把它当成状态损坏，
  // 而是继续走 ②（这里清单不可达，最终以 fetch 失败收场）。
  Config config = BaseConfig(dir);
  const RunResult result = RunOnce(config, false);
  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(result.message.find("fetch"), std::string::npos) << result.message;

  // 与「状态损坏」用例的区别：周期已经跑起来，失败状态被写入。
  AgentState persisted;
  std::string error;
  ASSERT_TRUE(LoadState(config.state_file, &persisted, &error)) << error;
  EXPECT_EQ(persisted.phase, "error");
}

}  // namespace
}  // namespace ota
