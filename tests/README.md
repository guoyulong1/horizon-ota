# tests

单元测试（GoogleTest + CTest）与集成测试。

## 单元测试（CTest + GoogleTest）

根目录先拉第三方源码再构建（googletest 由根 `3rdparty/fetch.sh` 按固定标签拉取并校验 SHA-256；fsd 的 OpenSSL/curl 由 `fsd/3rdparty/fetch.sh` 负责）：

```bash
./3rdparty/fetch.sh
(cd fsd && ./3rdparty/fetch.sh)
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

一键：`./scripts/verify.sh`。`-DBUILD_TESTING=OFF` 可整体关掉单元测试。

每个 `TEST` 用例都是一条独立的 ctest 条目（`gtest_discover_tests`）。只跑某一组：`ctest -R test_verify_manifest --output-on-failure`。

| 测试目标 | ctest 前缀 | 覆盖 |
|---|---|---|
| `tests/unit/test_pack_crypto.cc` | `test_pack_crypto` | SHA-256 已知向量（FIPS 180-4）、文件哈希与读缓冲边界、嵌入 NUL 的二进制、Ed25519 签名往返（OpenSSL 系统版） |
| `tests/unit/test_verify_crypto.cc` | `test_verify_crypto` | 公钥十六进制解析（大小写/非法输入）、Ed25519 验签（篡改/错钥/坏长度拒绝） |
| `tests/unit/test_verify_manifest.cc` | `test_verify_manifest` | 发布清单 JSON 解析、可选字段缺省、必填缺失、components 形状；适用性判断的全部拒绝原因与空字段跳过语义 |
| `tests/unit/test_verify_config.cc` | `test_verify_config` | 配置解析、缺省值、必填字段、文件缺失/坏 JSON |
| `tests/unit/test_verify_state.cc` | `test_verify_state` | 状态持久化：文件缺失视为初始状态、原子写往返、父目录创建、坏 JSON |
| `tests/unit/test_verify_agent.cc` | `test_verify_agent` | `RunOnce` 离线负路径：状态损坏、清单不可达时 error 状态落盘（完整 ②–⑦ 链路走集成测试） |
| `tests/fsd/test_*.cc` | `test_fsd_*` | fsd 模块移植测试（text_util/status/url/sha256/md5/manifest/http_client/secure_downloader），守护 monorepo 内的 vendored 副本 |
| （tests/CMakeLists.txt 注册） | `fsd_self_test` | fsd 命令行自检（SHA-256/MD5 向量、清单解析） |

googletest 版本：v1.17.0（`3rdparty/fetch.sh` 固定标签 + SHA-256 校验）。

## 集成测试（`tests/integration/`）

- `run.sh`：驱动 `fsd` 二进制，验证下载行为（T1/T2/T3/T5/T6）。
- `m1-e2e.sh`：M1 端到端（`ota-pack` → `ota-sign` → 本地 HTTPS → `ota-fetch` 验签 + 下载 + 篡改拒绝）。
- `serve.py`：支持 Range 的本地 HTTPS 测试服务器。

```bash
tests/integration/run.sh
tests/integration/m1-e2e.sh
tests/integration/m1-e2e.sh --bin-dir <dir>   # 用现成二进制
```

运行环境：Linux（板载 / WSL2 / CI），需要 `openssl`、`python3`、`curl`，构建时另需 `cmake`、`g++`。

## 参考

- 测试用例：`docs/OTA.md` 5.2
