## 背景

为 monorepo 补充 GTest 单元测试体系，规则与 fsd 仓库（firmware_secure_download#1）一致：GoogleTest v1.17.0 由固定标签 + SHA-256 校验从 GitHub 拉取，CTest 统一编排（每个 `TEST` 用例是独立 ctest 条目）。

## 改动

### GTest 基建

- 根 `3rdparty/fetch.sh`：拉取 googletest v1.17.0（codeload 归档 + SHA-256 校验，标记 `.ota-source-tag`）；设备端 OpenSSL/curl 仍由 `fsd/3rdparty/fetch.sh` 负责
- `tests/CMakeLists.txt`：gtest 门控（缺源码时 configure 报错提示先 fetch）+ `gtest_discover_tests`；`-DBUILD_TESTING=OFF` 可整体关掉
- 根 `scripts/verify.sh`：一键流程（googletest → fsd 3rdparty → 聚合构建 → ctest）

### 已有 CTest 转为 GTest 并扩展

| 测试目标 | 覆盖 |
| --- | --- |
| `tests/unit/test_pack_crypto.cc` | SHA-256 FIPS 180-4 向量、文件哈希与 64KiB 读缓冲边界、嵌入 NUL 的二进制数据、Ed25519 签名往返（密钥现场生成，验签在测试内独立完成） |
| `tests/unit/test_verify_crypto.cc` | 公钥十六进制解析（大小写/长度/非法字符）、Ed25519 验签（篡改消息/篡改签名/错钥/坏长度拒绝） |

### M2 verify 模块新增单测（此前零覆盖）

| 测试目标 | 覆盖 |
| --- | --- |
| `test_verify_manifest` | 发布清单 JSON 解析（完整/缺省/必填缺失/components 形状）、适用性判断全部 7 类拒绝原因与「空字段跳过检查」语义 |
| `test_verify_config` | 配置解析、缺省值、必填字段、文件缺失、坏 JSON |
| `test_verify_state` | 状态持久化：文件缺失视为初始状态、原子写往返、父目录自动创建、坏 JSON、空指针 |
| `test_verify_agent` | `RunOnce` 离线负路径：状态损坏直接失败、清单不可达时 error 状态原子落盘（完整 ②–⑦ 链路由集成测试覆盖） |

### fsd 模块移植测试（守护 vendored 副本）

`tests/fsd/`：从 fsd 仓库移植全套 8 个测试文件（text_util/status/url/sha256/md5/manifest/http_client/secure_downloader，共 96 用例）+ 共用工具头，逐字一致便于同步；`fsd_self_test` 继续注册为 ctest 底线。

### 顺带修复一个真实缺陷

移植的 url 测试暴露 monorepo 内 `fsd/src/url.cc` 同样的 `RemoveDotSegments` 缺陷（裸 `../` 前缀分支吞掉分隔斜杠，`/p/./../fw.bin` 会解析成 `https://hostfw.bin`）。已按 RFC 3986 §5.2.4 修复并附回归用例。

### CI 与文档

- `.github/workflows/ci.yml` 新增 `unit-tests` job（聚合构建 + ctest，缓存 googletest 与 fsd 3rdparty）
- `AGENTS.md` / `README.md` / `tests/README.md` 同步构建与测试说明

## 验证

目标板（地平线 X3，aarch64，GCC 11.2 / CMake 3.22）：

```
==> [4/4] 单元测试 (ctest + GoogleTest，含 fsd_self_test)
100% tests passed, 0 tests failed out of 154
Total Test time (real) =   1.56 sec
verify: OK
```

构成：pack_crypto 11、verify_crypto 6、verify_manifest 19、verify_config 5、verify_state 5、verify_agent 3、fsd 移植测试 96、fsd_self_test 1。

[![](https://img.shields.io/badge/powered_by-dsh-4D6BFE?style=flat-square&logo=deepseek&logoColor=white)](https://github.com/deepseek-ai/deepseek-harness)
