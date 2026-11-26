// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// agent/state：OTA 代理持久状态。覆盖「文件不存在视为初始状态」、
// 原子写回读往返、父目录自动创建、坏 JSON 与空指针。

#include "state.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "fsd_test_util.h"

namespace ota {
namespace {

using fsd::testing::MakeTempDir;
using fsd::testing::TempPathGuard;

TEST(LoadState, MissingFileYieldsInitialIdleState) {
  AgentState state;
  state.phase = "downloading";  // 预置非默认值，确认被重置
  std::string error;
  ASSERT_TRUE(LoadState("/nonexistent/ota-state.json", &state, &error))
      << error;
  EXPECT_EQ(state.schema, 1);
  EXPECT_EQ(state.phase, "idle");
  EXPECT_TRUE(state.release_id.empty());
  EXPECT_TRUE(state.last_error.empty());
  EXPECT_EQ(state.retry, 0);
  EXPECT_EQ(state.security_version_floor, 0);
}

TEST(SaveAndLoadState, RoundTripsAllFields) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);
  const std::string path = (dir / "state.json").string();

  AgentState state;
  state.schema = 1;
  state.phase = "waiting";
  state.release_id = "rel-2026-001";
  state.last_error = "previous failure";
  state.retry = 2;
  state.security_version_floor = 7;

  std::string error;
  ASSERT_TRUE(SaveState(path, state, &error)) << error;

  AgentState loaded;
  ASSERT_TRUE(LoadState(path, &loaded, &error)) << error;
  EXPECT_EQ(loaded.phase, "waiting");
  EXPECT_EQ(loaded.release_id, "rel-2026-001");
  EXPECT_EQ(loaded.last_error, "previous failure");
  EXPECT_EQ(loaded.retry, 2);
  EXPECT_EQ(loaded.security_version_floor, 7);
}

TEST(SaveState, CreatesParentDirectoriesAtomically) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);
  // 父目录不存在：SaveState 负责创建（嵌套两级）。
  const std::string path =
      (dir / "nested" / "deeper" / "state.json").string();

  AgentState state;
  state.phase = "downloading";
  state.release_id = "rel-42";
  std::string error;
  ASSERT_TRUE(SaveState(path, state, &error)) << error;

  AgentState loaded;
  ASSERT_TRUE(LoadState(path, &loaded, &error)) << error;
  EXPECT_EQ(loaded.phase, "downloading");

  // 原子写：落地后不留 .tmp 残留。
  EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
}

TEST(LoadState, RejectsCorruptJson) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);
  const std::string path = (dir / "state.json").string();
  {
    std::ofstream out(path, std::ios::binary);
    out << "{ this is not json";
  }

  AgentState state;
  std::string error;
  EXPECT_FALSE(LoadState(path, &state, &error));
  EXPECT_FALSE(error.empty());
}

TEST(StateIo, RejectsNullOutputArguments) {
  const std::filesystem::path dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  TempPathGuard guard(dir);
  const std::string path = (dir / "state.json").string();

  AgentState state;
  std::string error;
  EXPECT_FALSE(LoadState(path, nullptr, &error));
  EXPECT_FALSE(SaveState(path, state, nullptr));
}

}  // namespace
}  // namespace ota
