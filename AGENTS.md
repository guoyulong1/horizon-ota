# AGENTS.md

本仓库协作智能体约定：议题去向、领域文档、模块地图，以及「怎么构建 / 怎么验证 / 怎么连目标板」。

## Agent skills

### Issue tracker

本仓库的 issue 记在 GitHub Issues，用 `gh` CLI 操作。见 `docs/agents/issue-tracker.md`。

### Domain docs

单上下文：根目录 `GLOSSARY.md` + `docs/adr/`。见 `docs/agents/domain.md`。

## 模块地图

| 目录 | 职责 | 状态 |
|---|---|---|
| `fsd/` | 设备端安全下载模块 | 已实现 |
| `pack/` | 打包 + 离线签名 | 待实现 |
| `verify/` | 发布清单验签 + 适用性判断（纯库，可单独构建） | 已实现 |
| `agent/` | OTA 代理：状态机 + 编排（唯一链接 fsd + verify 的模块） | 实现中（M2 下载链路；安装/回滚 M3） |
| `server/` | 发布服务 / 通道 | 待实现 |
| `tests/` | 集成测试 | 待实现 |
| `boot/` | U-Boot / 分区 / 安装 | 待实现 |
| `docs/OTA.md` | OTA 标准作业书 | — |

## 目标与约束

- 目标板：地平线 X3（ARM64 Linux）。
- 本项目依赖 POSIX 与 Linux 工具链（g++/make/perl、`flock`/`fsync`/`pthread`/`dl`），**不要在 Windows 上直接构建/运行**。Windows 只做编辑与 git 调度；构建/运行放到 WSL2（x86_64 快速自检）或直接板载。

## 构建与验证

### monorepo 一键验证（聚合：fsd + verify + agent + pack + 单元测试）

```bash
./scripts/verify.sh
```

等价于：`./3rdparty/fetch.sh`（googletest，首次拉取并校验 SHA-256）→ `(cd fsd && ./3rdparty/fetch.sh)`（OpenSSL/curl，如缺）→ `cmake -S . -B build` → `cmake --build build` → `ctest --test-dir build --output-on-failure`。

单元测试基于 GoogleTest（每个 `TEST` 用例是独立 ctest 条目）；`-DBUILD_TESTING=OFF` 可整体关掉测试。**验证标准**：ctest 全绿（`fsd_self_test` 与 `verify_self_test` 是其中两条 `--self-test` 底线）。改动涉及网络/TLS/IO/下载编排时，还需在真机跑一次真实下载验证。

### 下载模块（fsd）

```bash
cd fsd
./3rdparty/fetch.sh
cmake -S . -B build
cmake --build build
./build/firmware_secure_download --self-test
```

一键：`./fsd/scripts/verify.sh`。

### 验签库（verify，可单独构建）

```bash
cd verify
cmake -S . -B build        # 用系统 OpenSSL（需 libssl-dev）；monorepo 内则复用 fsd 的
cmake --build build
./build/ota-verify --self-test
```

### OTA 代理（agent）

```bash
./scripts/verify.sh        # 随 monorepo 构建；产物 build/bin/ota-agent
```

agent 依赖 fsd / verify 的 CMake 目标，不支持单独配置构建（CMake 有防误用守卫）。
agent 编排的后续工作**先写计划后执行**：见 `agent/PLAN.md`。

### 真机验证（板载）

```bash
./fsd/scripts/board-verify.sh
```

默认板子上 monorepo 路径为 `/home/root/workspace/horizon-ota`；若你的仓库名/路径不同，用 `BOARD_REPO_DIR` 覆盖。首次使用前需在板子上克隆本 monorepo 一次。

### 目标板访问

**板子地址不写进仓库**（本仓库公开）。用环境变量提供：

| 变量 | 含义 |
|---|---|
| `BOARD_HOST` | 目标板 SSH 地址，如 `root@<board-ip>`（必填） |
| `BOARD_REPO_DIR` | 板子上 monorepo 路径，默认 `/home/root/workspace/horizon-ota` |

```bash
export BOARD_HOST="root@<board-ip>"
ssh -i .ssh/id_ed25519 \
    -o UserKnownHostsFile=.ssh/known_hosts \
    -o StrictHostKeyChecking=accept-new \
    "$BOARD_HOST" "uname -a"
```

> 私钥在 `.ssh/`（已 gitignore），**不要提交**；`known_hosts` 用工作区内的，避免写 `~/.ssh`。

首次在板子上准备仓库：

```bash
ssh "$BOARD_HOST" "git clone https://github.com/guoyulong1/horizon-ota.git /home/root/workspace/horizon-ota"
```

## 仓库约定

- `main` 保持可构建；功能走 feature 分支 + PR；CI 跑构建 + `--self-test`，merge 前必须绿。
- `docs/OTA.md` 是 OTA 端到端作业书；模块行为与其冲突时，先对齐文档再改。
- **模块独立、可单独部署**：发布端（`pack/`、`ota-sign`）跑在服务器/CI/离线签名机，设备端（`fsd/`、`verify/`、`agent/`）跑在固件本机，二者互不依赖；跨模块只通过发布清单 JSON 契约通信，**`pack` 不得引用 `fsd` 头文件**。
- **模块边界不越界**（`docs/adr/0001`）：`verify` 只拥有「清单验签 + 适用性判断」，纯函数、无状态，不依赖 fsd API、不做下载/发现/安装编排；`agent` 是唯一的状态机与编排拥有者，也是唯一同时链接 `fsd` 与 `verify` 的模块；`fsd_openssl` 只共享 OpenSSL 产物，不得借它引用 fsd API。新能力先归入对应职责模块，接口不够表达时先扩接口再调用；跨模块的编排设计**先写计划（对应模块 `PLAN.md`）评审，再执行**。
