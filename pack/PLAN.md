# M1 计划：打包 + 签名（C++ / 独立模块）

> 对应 `docs/OTA.md` 第 6 章 M1。目标：把一次构建的产物整理成可发布目录，并在设备端走通「取清单 → 验签 → 下载」。

## 架构原则（开发前置条件）

- **模块独立、可单独部署**：
  - 发布端 `pack/`（打包 + 签名）最终跑在 **服务器 / CI / 离线签名机**；
  - 设备端 `fsd/`（下载库）、`verify/`（验签）、`agent/`（代理）跑在 **固件本机**；
  - 二者互不依赖，可分别构建、分别部署。
- **跨模块只靠契约通信**：发布清单 JSON 的格式（`docs/OTA.md` 2.3）是唯一契约，**不共享 C++ 代码/头文件**。
- **依赖方向单向、无环**：

| 模块 | 允许依赖 | 禁止依赖 |
|---|---|---|
| `pack/`（打包 + 签名） | OpenSSL（EVP SHA-256 / Ed25519）、nlohmann/json | **fsd 的任何头/库** |
| `fsd/`（下载库） | OpenSSL、libcurl | 任何 OTA 工具 |
| `verify/`（设备端） | fsd、OpenSSL、nlohmann/json | pack |

- 具体地：`pack` 算组件 SHA-256 **直接用 OpenSSL `EVP_Digest`**，不引用 `fsd/sha256.h`。

## 打包产物

一次打包（`ota-pack`）产出一个发布目录：

```
releases/<model>/<version>/<release_id>/
├── manifest.json      ← 发布清单（JSON），契约核心
├── manifest.json.sig  ← 对 manifest 原始字节的 Ed25519 签名
└── files/             ← 组件本体（Image / dtb / rootfs.squashfs）
```

## 工具设计（C++20）

### 发布端 `pack/`（独立工程，不链接 fsd）

- `ota-pack`：
  - 读取产物与参数（env/args）：`IMAGES_DIR`、`MODEL`、`VERSION`、`DTB`、`SECURITY_VERSION`、`MIN_VERSION_CODE`、`HARDWARE_COMPAT`、`OUTPUT_DIR`。
  - 用 **OpenSSL EVP**（`EVP_DigestInit/Update/Final`，SHA-256）算组件哈希；`release_id` = 组件哈希列表的哈希前 12 位。
  - 用 **nlohmann/json** 生成 `manifest.json`（字段按 `docs/OTA.md` 2.3）。
  - 拷贝组件到发布目录；校验输入，非法输入拒绝且不留半成品。
- `ota-sign`：OpenSSL **`EVP_DigestSign`**（Ed25519，raw）对清单原始字节签名 → `manifest.json.sig`。私钥走 `OTA_PRIVATE_KEY`。
- 依赖：OpenSSL（系统或固定版本）+ vendored `nlohmann/json.hpp`。**没有 fsd 依赖**。
- 构建：`pack/CMakeLists.txt` 独立工程，可单独 `cmake -S pack -B build-pack`。

### 设备端 `verify/`（M1 最小版，M2 长成 agent）

- `ota-fetch`：
  1. 用 `fsd::HttpClient` 取 `manifest.json` + `.sig`（`max_bytes` 限制，**无摘要**）。
  2. OpenSSL 验签（`EVP_DigestVerify`，Ed25519；内置或 `OTA_PUBLIC_KEY` 提供的 32 字节原始公钥）。
  3. nlohmann 解析，校验 `schema_version` 与字段。
  4. 适用性判断（`model`/`hardware_compat`/`layouts`/`security_version`/`min_version_code`/`version_code`）。
  5. 用 `fsd::SecureDownloader` 下载各组件（`url` + `sha256` + `expected_size` + `resume`）。
- 依赖：fsd 静态库 + OpenSSL + nlohmann。

### fsd 改造（为设备端工具服务，不影响 pack）

- 把 `include/` + `src/`（除 `main.cc`）抽成静态库目标 `fsd`；`firmware_secure_download` CLI 链接它。
- fsd 自身不依赖任何 OTA 工具；只是被 `verify/`、`agent/` 复用。

## 契约（语言无关）

- 发布清单格式以 `docs/OTA.md` 2.3 为准。
- 可选：在 `docs/` 放 `manifest.schema.json`（JSON Schema），作为与语言/实现无关的契约。

## 构建与部署

- 每个模块有独立 `CMakeLists.txt`，可单独构建、单独部署（pack 到服务器、fsd/verify 到设备）。
- 根 `CMakeLists.txt`（可选）只做开发期聚合构建，不改变模块独立性。

## 测试（扩展 `tests/integration/`）

| 用例 | 操作 | 期望 |
|---|---|---|
| P1 | fake 产物 → `ota-pack` | 清单合法、哈希/大小一致、`release_id` 与目录一致 |
| P2 | 同一输入重复打包 | 结果一致 |
| P3 | 篡改清单一个字节 → 验签 | 失败 |
| P4 | 正确签名 → 验签 | 成功 |
| P5 | 本地 HTTPS → `ota-fetch` 全流程 | 验签 + 下载成功、内容一致 |
| P6 | 篡改组件 → `ota-fetch` | 哈希不符（`kChecksumMismatch`=9） |

复用 `tests/integration/serve.py`（HTTPS + Range）。

## 公开仓库安全

- 私钥 / 板子地址 / 服务器域名 / CA 一律用环境变量（`OTA_PRIVATE_KEY`、`OTA_PUBLIC_KEY`、`OTA_BASE_URL`、`OTA_CA`、`BOARD_HOST`）。
- `.gitignore` 增加：`/releases/`、`*.pem`、`*.sig`、构建目录。
- 文档用占位（`ota.example.com`、`<board-ip>`、`<release_id>`）。

## 验收标准

- X3 上跑通：**构建 → `ota-pack` → `ota-sign` → 静态托管 → `ota-fetch`（验签 + 下载各组件 + 哈希一致）**。
- 对应 `docs/OTA.md` 5.2 的 T2（清单篡改）、T3（组件篡改）、T4（续传）通过。

## 待确认

1. 按「模块独立」构建：`pack/` 独立 CMake、`verify/` 链接 fsd —— 按你的要求，采纳。
2. nlohmann 用 vendored 单头（`3rdparty/nlohmann/json.hpp`）—— 建议是。
3. 契约是否额外落一份 `docs/manifest.schema.json` —— 可选。
