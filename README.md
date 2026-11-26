# horizon-ota

地平线 X3（ARM64）嵌入式设备的 OTA 全流程工程：从发布端产物、离线签名、安全下载，到设备端安装、确认与自动回滚。

工程按模块拆分，每个目录一个关注点：

| 目录 | 职责 | 状态 |
|---|---|---|
| [`fsd/`](fsd/) | 设备端安全下载模块（HTTPS/TLS、断点续传、SHA-256 校验、原子发布） | 已实现 |
| `pack/` | 打包 + 离线签名：产物 → 发布目录（manifest + sig） | 待实现 |
| `verify/` | 发布清单解析 + Ed25519 验签 + 适用性判断 | 待实现 |
| `agent/` | OTA 代理：状态机、发现、下载编排、安装、确认、上报 | 待实现 |
| `server/` | 静态发布服务配置 / 上传脚本 / 通道指针 | 待实现 |
| `tests/` | 集成测试：本地 HTTPS + 篡改/断网/续传用例 | 待实现 |
| `boot/` | U-Boot 启动脚本、分区布局、A/B 与 recovery | 待实现 |

## 文档

- [docs/OTA.md](docs/OTA.md)：**OTA 标准作业书**——从发布端到设备端的完整流程、契约、判据、测试与落地路线。
- [fsd/README.md](fsd/README.md)：下载模块的构建、参数与退出码。

## 快速开始（下载模块）

```bash
cd fsd
./3rdparty/fetch.sh
cmake -S . -B build
cmake --build build
./build/firmware_secure_download --self-test
```

一键验证：`./fsd/scripts/verify.sh`（Linux 侧，WSL2 或板载）。

## 快速开始（monorepo 聚合构建 + 单元测试）

聚合构建 fsd + verify + pack，ctest 编排 GoogleTest 单元测试（每个 `TEST` 用例是独立 ctest 条目）：

```bash
./scripts/verify.sh
```

等价于：`./3rdparty/fetch.sh`（googletest v1.17.0，固定标签 + SHA-256 校验）→ `(cd fsd && ./3rdparty/fetch.sh)`（OpenSSL/curl，如缺）→ `cmake -S . -B build` → `cmake --build build` → `ctest --test-dir build --output-on-failure`。`-DBUILD_TESTING=OFF` 可整体关掉测试。测试项明细见 [tests/README.md](tests/README.md)。

## 落地路线

见 [docs/OTA.md](docs/OTA.md) 第 6 章：

- **M1** 打包 + 签名 + 静态通道
- **M2** 清单验签 + OTA 代理
- **M3** A/B 安装 + 启动脚本 + 确认回滚
- **M4** recovery 方案
- **M5** check-in API、上报、灰度
