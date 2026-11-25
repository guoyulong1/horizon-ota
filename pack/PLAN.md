# M1 计划：打包 + 签名（C++ / nlohmann）

> 对应 `docs/OTA.md` 第 6 章 M1。目标：把一次构建的产物整理成可发布目录，并在设备端走通「取清单 → 验签 → 下载」。

## 决策变更（相对初版）

- **不采用 Python**：低内存 / 小 flash 的嵌入式系统可能裁掉 Python。改为 **C++20**，与设备端同构。
- **JSON 用 nlohmann/json**，加密用 **OpenSSL**，下载复用 **fsd**——能复用第三方就不自己造。
- 打包产物不变，仍是「发布目录」：`manifest.json` + 组件 + `manifest.json.sig`。

## 打包产物是什么

一次打包（`ota-pack`）产出：

| 产物 | 作用 |
|---|---|
| `manifest.json` | **发布清单（JSON，契约核心）**：版本、型号、硬件兼容、组件哈希与路径、安装策略 |
| `files/{Image,<dtb>,rootfs.squashfs}` | 组件本体 |
| `manifest.json.sig` | 对 `manifest.json` 原始字节的 Ed25519 签名（64 字节，独立文件） |

目录：`releases/<model>/<version>/<release_id>/`。

## 设备端为什么不能直接用 fsd 取清单

结论：**fsd 不缺能力，缺的是对外入口。**

| 层 | 能否无摘要取清单 | 说明 |
|---|---|---|
| `fsd::HttpClient`（库） | ✅ 能 | 只要求 URL + 目标 `FILE*` + `max_bytes`，不要求摘要；正是取清单/签名的正确层 |
| `fsd::SecureDownloader`（库/CLI 核心） | ❌ 不设计用来 | 职责是「已知哈希的固件包」；CLI 强制 `--sha256/--md5` |
| `firmware_secure_download`（CLI） | ❌ | `--url` 模式强制摘要；`--manifest` 模式要求清单内已含摘要 |

所以缺口是：**没有一个 digest-less 的 CLI 入口**（`HttpClient` 只作为库 API 存在）。修法二选一：

1. 给 fsd CLI 加 `--fetch <url> --output <file> [--max-bytes N]` 模式（用 `HttpClient`）；
2. 设备端写 `ota-fetch` 工具，链接 fsd 并直接调 `HttpClient`。

本计划选 **2**，因为它同时是 M2 OTA 代理的种子。

## 工具设计（C++20）

### 发布端 `pack/`

- `ota-pack`（C++，链接 fsd + nlohmann + OpenSSL）：
  - 读取产物与参数（env/args）：`IMAGES_DIR`、`MODEL`、`VERSION`、`DTB`、`SECURITY_VERSION`、`MIN_VERSION_CODE`、`HARDWARE_COMPAT`、`OUTPUT_DIR`。
  - 用 `fsd::HashFile`（或 OpenSSL EVP）算组件 SHA-256；`release_id` = 组件哈希列表的哈希前 12 位。
  - 用 nlohmann 生成 `manifest.json`（字段严格按 `docs/OTA.md` 2.3）。
  - 拷贝组件到发布目录；校验输入，非法输入拒绝且不留半成品。
- `ota-sign`（或 `ota-pack --sign`）：OpenSSL **Ed25519** 对清单原始字节签名 → `manifest.json.sig`。私钥路径走 `OTA_PRIVATE_KEY`。

### 设备端 `verify/`（M1 最小版，M2 长成 agent）

- `ota-fetch`（C++，链接 fsd）：
  1. 用 `fsd::HttpClient` 取 `manifest.json` + `manifest.json.sig`（`max_bytes` 限制，**无摘要**）。
  2. OpenSSL 验签（内置或 `OTA_PUBLIC_KEY` 提供的 32 字节原始公钥，可多把）。
  3. nlohmann 解析，校验 `schema_version` 与字段范围。
  4. 适用性判断（`model`/`hardware_compat`/`layouts`/`security_version`/`min_version_code`/`version_code`）。
  5. 对每个组件用 `fsd::SecureDownloader` 下载（`url` + `sha256` + `expected_size` + `resume`）。

### fsd 改造（M1 前置）

把 fsd 的 `include/` + `src/`（除 `main.cc`）抽成 **静态库目标 `fsd`**；`firmware_secure_download` CLI 链接它。这样 `pack/`、`verify/` 可直接链接，复用 `HttpClient`/`SecureDownloader`/`Status`/`HashFile`。

### 第三方

- **nlohmann/json**：把单头文件 `json.hpp` vendored 到 `3rdparty/nlohmann/json.hpp`（离线可用，固定版本）。
- OpenSSL / libcurl：已在 fsd 的 `3rdparty/`，复用。

## 构建

- 根 `CMakeLists.txt` 构建 `fsd` 静态库 + `ota-pack`/`ota-sign`/`ota-fetch`。
- 沿用 fsd 的严格编译选项（`-Wall -Wextra -Wpedantic ...`）。

## 测试（扩展 `tests/integration/`）

| 用例 | 操作 | 期望 |
|---|---|---|
| P1 | 生成 fake 产物 → `ota-pack` | 清单合法、哈希/大小一致、`release_id` 与目录一致 |
| P2 | 同一输入重复打包 | 结果一致 |
| P3 | 篡改清单一个字节 → 验签 | 失败 |
| P4 | 正确签名 → 验签 | 成功 |
| P5 | 本地 HTTPS → `ota-fetch` 全流程 | 验签 + 下载成功、内容一致 |
| P6 | 篡改组件 → `ota-fetch` | 哈希不符（`kChecksumMismatch`=9） |

复用 `tests/integration/serve.py`（HTTPS + Range）作静态发布服务器。

## 公开仓库安全

- 私钥 / 板子地址 / 服务器域名 / CA 一律用环境变量（`OTA_PRIVATE_KEY`、`OTA_PUBLIC_KEY`、`OTA_BASE_URL`、`OTA_CA`、`BOARD_HOST`）。
- `.gitignore` 增加：`/releases/`、`*.pem`、`*.sig`（签名产物）、构建目录。
- 文档用占位（`ota.example.com`、`<board-ip>`、`<release_id>`）。

## 验收标准

- X3 上跑通：**构建 → `ota-pack` → `ota-sign` → 静态托管 → `ota-fetch`（验签 + 下载各组件 + 哈希一致）**。
- 对应 `docs/OTA.md` 5.2 的 T2（清单篡改）、T3（组件篡改）、T4（续传）通过。

## 待确认

1. fsd 静态库抽取作为 M1 前置（小改 CMake）——建议是。
2. nlohmann 用 vendored 单头还是 fetch.sh 拉取固定版本——建议 vendored（离线可用）。
3. 设备端直接做 `ota-fetch`（C++）——建议是，不先给 CLI 加 `--fetch`。
