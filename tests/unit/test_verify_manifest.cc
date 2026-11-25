// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// verify/manifest：发布清单 JSON 解析与适用性判断（docs/OTA.md 3.5）。
// 覆盖完整解析、可选字段缺省、必填字段缺失、components 形状错误，以及
// 全部七类拒绝原因与「空字段跳过检查」的语义。

#include "manifest.h"

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "fsd_test_util.h"

namespace ota {
namespace {

using fsd::testing::WriteTempFile;

constexpr const char* kComponentSha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

// 与 docs/OTA.md 发布清单契约一致的最小完整清单。
std::string ValidManifestJson() {
  return std::string(R"({
  "schema_version": 1,
  "release_id": "rel-2026-001",
  "version": "2.3.1",
  "version_code": 20260301,
  "security_version": 5,
  "model": "X3-ABC",
  "hardware_compat": ["x3m30x", "x3m30x-v2"],
  "min_version_code": 100,
  "created_at": "2026-11-26T00:00:00Z",
  "install": {"mode": "ab", "layouts": ["ab", "single"]},
  "components": [
    {"name": "rootfs", "path": "images/rootfs.img",
     "sha256": ")") + kComponentSha256 + R"(",
     "size": 1024, "target": "rootfs"}
  ]
})";
}

// 构造一个恰好可用的清单/设备组合，用例里按需改字段。
Manifest BaseManifest() {
  Manifest manifest;
  manifest.schema_version = 1;
  manifest.release_id = "rel-2026-001";
  manifest.version = "2.3.1";
  manifest.version_code = 20260301;
  manifest.security_version = 5;
  manifest.model = "X3-ABC";
  manifest.hardware_compat = {"x3m30x", "x3m30x-v2"};
  manifest.min_version_code = 100;
  manifest.created_at = "2026-11-26T00:00:00Z";
  manifest.install_mode = "ab";
  manifest.layouts = {"ab", "single"};
  Component component;
  component.name = "rootfs";
  component.path = "images/rootfs.img";
  component.sha256 = kComponentSha256;
  component.size = 1024;
  component.target = "rootfs";
  manifest.components.push_back(component);
  return manifest;
}

DeviceContext BaseDevice() {
  DeviceContext device;
  device.model = "X3-ABC";
  device.version_code = 500;
  device.security_version_floor = 3;
  device.hardware_compat = {"x3m30x"};
  device.layout = "ab";
  return device;
}

void ExpectRejected(const Manifest& manifest, const DeviceContext& device,
                    RejectReason reason) {
  const Decision decision = CheckApplicability(manifest, device);
  EXPECT_FALSE(decision.accepted);
  EXPECT_EQ(decision.reason, reason);
  EXPECT_FALSE(decision.message.empty());
}

TEST(ParseManifest, ParsesCompleteManifest) {
  Manifest manifest;
  std::string error;
  ASSERT_TRUE(ParseManifest(ValidManifestJson(), &manifest, &error)) << error;
  EXPECT_EQ(manifest.schema_version, 1);
  EXPECT_EQ(manifest.release_id, "rel-2026-001");
  EXPECT_EQ(manifest.version, "2.3.1");
  EXPECT_EQ(manifest.version_code, 20260301);
  EXPECT_EQ(manifest.security_version, 5);
  EXPECT_EQ(manifest.model, "X3-ABC");
  ASSERT_EQ(manifest.hardware_compat.size(), 2U);
  EXPECT_EQ(manifest.hardware_compat[1], "x3m30x-v2");
  EXPECT_EQ(manifest.min_version_code, 100);
  EXPECT_EQ(manifest.created_at, "2026-11-26T00:00:00Z");
  EXPECT_EQ(manifest.install_mode, "ab");
  ASSERT_EQ(manifest.layouts.size(), 2U);
  EXPECT_EQ(manifest.layouts[0], "ab");
  ASSERT_EQ(manifest.components.size(), 1U);
  EXPECT_EQ(manifest.components[0].name, "rootfs");
  EXPECT_EQ(manifest.components[0].path, "images/rootfs.img");
  EXPECT_EQ(manifest.components[0].sha256, kComponentSha256);
  EXPECT_EQ(manifest.components[0].size, 1024U);
  EXPECT_EQ(manifest.components[0].target, "rootfs");
}

TEST(ParseManifest, DefaultsOptionalFieldsWhenAbsent) {
  const std::string text = R"({
  "schema_version": 1,
  "release_id": "rel",
  "version": "1.0.0",
  "version_code": 10,
  "security_version": 1,
  "model": "X3-ABC",
  "created_at": "2026-11-26T00:00:00Z",
  "components": [
    {"name": "app", "path": "images/app.img",
     "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
     "size": 3, "target": "app"}
  ]
})";
  Manifest manifest;
  std::string error;
  ASSERT_TRUE(ParseManifest(text, &manifest, &error)) << error;
  EXPECT_EQ(manifest.min_version_code, 0);
  EXPECT_TRUE(manifest.hardware_compat.empty());
  EXPECT_TRUE(manifest.install_mode.empty());
  EXPECT_TRUE(manifest.layouts.empty());
}

TEST(ParseManifest, RejectsMissingRequiredFields) {
  Manifest manifest;
  std::string error;
  EXPECT_FALSE(ParseManifest("{ \"schema_version\": 1 }", &manifest, &error));
  EXPECT_FALSE(error.empty());

  const std::string no_created_at = R"({
  "schema_version": 1,
  "release_id": "rel",
  "version": "1.0.0",
  "version_code": 10,
  "security_version": 1,
  "model": "X3-ABC",
  "components": [
    {"name": "app", "path": "images/app.img",
     "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
     "size": 3, "target": "app"}
  ]
})";
  EXPECT_FALSE(ParseManifest(no_created_at, &manifest, &error));
}

TEST(ParseManifest, RejectsBadComponentsShape) {
  Manifest manifest;
  std::string error;
  // 缺 components。
  EXPECT_FALSE(ParseManifest("{ \"schema_version\": 1 }", &manifest, &error));
  // components 不是数组。
  EXPECT_FALSE(ParseManifest(
      R"({ "schema_version": 1, "components": {"name": "x"} })", &manifest,
      &error));
  // components 为空数组。
  EXPECT_FALSE(ParseManifest(R"({ "schema_version": 1, "components": [] })",
                             &manifest, &error));
  // 组件缺 target。
  EXPECT_FALSE(ParseManifest(
      R"({ "schema_version": 1, "components": [
            {"name": "app", "path": "a.img",
             "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
             "size": 3} ] })",
      &manifest, &error));
  // size 不是整数。
  EXPECT_FALSE(ParseManifest(
      R"({ "schema_version": 1, "components": [
            {"name": "app", "path": "a.img",
             "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
             "size": "3", "target": "app"} ] })",
      &manifest, &error));
}

TEST(ParseManifest, RejectsInvalidJsonAndNullPointers) {
  Manifest manifest;
  std::string error;
  EXPECT_FALSE(ParseManifest("not json at all", &manifest, &error));
  EXPECT_FALSE(ParseManifest(ValidManifestJson(), nullptr, &error));
  EXPECT_FALSE(ParseManifest(ValidManifestJson(), &manifest, nullptr));
}

TEST(CheckApplicability, AcceptsApplicableRelease) {
  const Decision decision = CheckApplicability(BaseManifest(), BaseDevice());
  EXPECT_TRUE(decision.accepted);
  EXPECT_EQ(decision.reason, RejectReason::kNone);
}

TEST(CheckApplicability, RejectsUnsupportedSchema) {
  Manifest manifest = BaseManifest();
  manifest.schema_version = 2;
  ExpectRejected(manifest, BaseDevice(), RejectReason::kUnsupportedSchema);
}

TEST(CheckApplicability, RejectsModelMismatch) {
  DeviceContext device = BaseDevice();
  device.model = "X3-OTHER";
  ExpectRejected(BaseManifest(), device, RejectReason::kModelMismatch);
}

TEST(CheckApplicability, SkipsModelCheckWhenDeviceModelEmpty) {
  DeviceContext device = BaseDevice();
  device.model.clear();
  const Decision decision = CheckApplicability(BaseManifest(), device);
  EXPECT_TRUE(decision.accepted);
}

TEST(CheckApplicability, RejectsHardwareMismatch) {
  Manifest manifest = BaseManifest();
  manifest.hardware_compat = {"x3m99x"};
  ExpectRejected(manifest, BaseDevice(), RejectReason::kHardwareMismatch);
}

TEST(CheckApplicability, AcceptsWhenAnyDeviceHardwareIdMatches) {
  Manifest manifest = BaseManifest();
  manifest.hardware_compat = {"x3m99x", "x3m30x"};
  DeviceContext device = BaseDevice();
  device.hardware_compat = {"x3m77x", "x3m30x"};
  const Decision decision = CheckApplicability(manifest, device);
  EXPECT_TRUE(decision.accepted);
}

TEST(CheckApplicability, SkipsHardwareCheckWhenEitherSideEmpty) {
  Manifest manifest = BaseManifest();
  manifest.hardware_compat.clear();
  EXPECT_TRUE(CheckApplicability(manifest, BaseDevice()).accepted);

  manifest.hardware_compat = {"x3m99x"};
  DeviceContext device = BaseDevice();
  device.hardware_compat.clear();
  EXPECT_TRUE(CheckApplicability(manifest, device).accepted);
}

TEST(CheckApplicability, RejectsUnsupportedLayout) {
  Manifest manifest = BaseManifest();
  manifest.layouts = {"single"};
  ExpectRejected(manifest, BaseDevice(), RejectReason::kLayoutUnsupported);
}

TEST(CheckApplicability, SkipsLayoutCheckWhenDeviceHasNoLayout) {
  Manifest manifest = BaseManifest();
  manifest.layouts = {"single"};
  DeviceContext device = BaseDevice();
  device.layout = std::nullopt;
  const Decision decision = CheckApplicability(manifest, device);
  EXPECT_TRUE(decision.accepted);
}

TEST(CheckApplicability, RejectsSecurityRollback) {
  Manifest manifest = BaseManifest();
  manifest.security_version = 2;  // 低于设备下限 3
  ExpectRejected(manifest, BaseDevice(), RejectReason::kRollback);
}

TEST(CheckApplicability, AcceptsSecurityVersionAtFloor) {
  Manifest manifest = BaseManifest();
  manifest.security_version = 3;  // 恰好等于下限，不算回滚
  const Decision decision = CheckApplicability(manifest, BaseDevice());
  EXPECT_TRUE(decision.accepted);
}

TEST(CheckApplicability, RejectsWhenIntermediateVersionNeeded) {
  Manifest manifest = BaseManifest();
  manifest.min_version_code = 600;  // 设备 500 过不了台阶
  ExpectRejected(manifest, BaseDevice(), RejectReason::kNeedsIntermediate);
}

TEST(CheckApplicability, RejectsReleaseThatIsNotNewer) {
  DeviceContext device = BaseDevice();
  device.version_code = 20260301;  // 与清单持平
  ExpectRejected(BaseManifest(), device, RejectReason::kNotNewer);

  device.version_code = 20260401;  // 设备比清单还新
  ExpectRejected(BaseManifest(), device, RejectReason::kNotNewer);
}

}  // namespace
}  // namespace ota
