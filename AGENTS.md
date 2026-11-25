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
| `verify/` | 发布清单验签 + 适用性判断 | 待实现 |
| `agent/` | OTA 代理 | 待实现 |
| `server/` | 发布服务 / 通道 | 待实现 |
| `tests/` | 集成测试 | 待实现 |
| `boot/` | U-Boot / 分区 / 安装 | 待实现 |
| `docs/OTA.md` | OTA 标准作业书 | — |

## 目标与约束

- 目标板：地平线 X3（ARM64 Linux）。
- 本项目依赖 POSIX 与 Linux 工具链（g++/make/perl、`flock`/`fsync`/`pthread`/`dl`），**不要在 Windows 上直接构建/运行**。Windows 只做编辑与 git 调度；构建/运行放到 WSL2（x86_64 快速自检）或直接板载。

## 构建与验证

### 下载模块（fsd）

```bash
cd fsd
./3rdparty/fetch.sh
cmake -S . -B build
cmake --build build
./build/firmware_secure_download --self-test
```

一键：`./fsd/scripts/verify.sh`。

### 真机验证（板载）

```bash
./fsd/scripts/board-verify.sh
```

默认板子上 monorepo 路径为 `/home/root/workspace/horizon-ota`；若你的仓库名/路径不同，用 `BOARD_REPO_DIR` 覆盖。首次使用前需在板子上克隆本 monorepo 一次。

### 目标板访问

- SSH：`root@192.168.31.35:22`
- 免密私钥：`.ssh/id_ed25519`（已 gitignore）
- 非交互登录：

```bash
ssh -i .ssh/id_ed25519 \
    -o UserKnownHostsFile=.ssh/known_hosts \
    -o StrictHostKeyChecking=accept-new \
    root@192.168.31.35 "uname -a"
```

> 本机 `~/.ssh/known_hosts` 在工作区之外，写入会被拦；用工作区内的 `-o UserKnownHostsFile=.ssh/known_hosts`。

首次在板子上准备仓库（板子能访问 GitHub 时）：

```bash
ssh root@192.168.31.35 "git clone https://github.com/guoyulong1/horizon-ota.git /home/root/workspace/horizon-ota"
```

## 仓库约定

- `main` 保持可构建；功能走 feature 分支 + PR；CI 跑构建 + `--self-test`，merge 前必须绿。
- `docs/OTA.md` 是 OTA 端到端作业书；模块行为与其冲突时，先对齐文档再改。
- 模块之间不互相 import；跨模块契约（发布清单格式）以 `docs/OTA.md` 为准。
