# agent

OTA 代理：设备端**状态机与编排**（`docs/OTA.md` 1.3）。唯一同时链接 `fsd`
（安全传输）与 `verify`（验签 + 适用性）的模块；自身不做密码学判断。
边界见 `docs/adr/0001`。

## 职责

- ①–⑩ 状态机：M2 已实现 `idle → checking → fetching_manifest → downloading →
  downloaded`；M3 补齐 `waiting / installing / pending_reboot / trial_boot /
  confirmed / rolled_back`（枚举与 `state.json` 已占位）。
- ② 发现：静态通道轮询（`Discovery` 抽象；M5 可替换为 check-in / MQTT）。
- ③④ 调 `verify`：取清单 + 签名 → Ed25519 验签 → 解析 → 适用性判断。
- ⑤⑦ 调 `fsd`：逐组件下载（清单哈希 + 续传 + 进度）+ 下载后复核。
- 防回滚下限持久化（M2 写 `state.json`；U-Boot env 留 M3）。
- **M3 待实现**（先评审 `PLAN.md` 再动手）：安装后端（A/B / recovery）、提交、
  试启动确认、自动回滚、拉黑、常驻运行与退避、上报。

## 用法

```bash
ota-agent check [--once] --config <json> [--verbose]
ota-agent status --config <json>
```

`--once`：跑一个周期后退出（cron / systemd timer 触发）；常驻与退避在 M3。

## 配置（JSON）

```json
{
  "base_url": "https://ota.example.com",
  "model": "demo-board",
  "channel": "beta",
  "layout": "ab",
  "staging_dir": "/data/ota/staging",
  "state_file": "/data/ota/state.json",
  "pubkey_hex": "<64 hex>",
  "ca_file": "/etc/ota/ca.pem",
  "version_code": 10202,
  "hardware_compat": ["example,demo-board"],
  "security_version_floor": 5,
  "manifest_path": "releases/demo-board/1.2.3/<release_id>/manifest.json"
}
```

- `manifest_path` 为空时，按通道轮询 `channels/<channel>/<model>/manifest.json`。
- 地址 / 密钥 / CA 走配置，不写死、不入库。

## 模块

| 文件 | 职责 |
|---|---|
| `src/config.*` | 设备配置加载 |
| `src/fetch.*` | `fsd::HttpClient` 取小文件（清单/签名） |
| `src/download.*` | `fsd::SecureDownloader` 逐组件下载 + 进度 |
| `src/state.*` | `state.json` 原子读写 |
| `src/agent.*` | 状态机 `RunOnce` |
| `src/ota_agent.cc` | CLI 入口 |

## 构建与测试

- 随 monorepo 构建：`./scripts/verify.sh`；产物 `build/bin/ota-agent`。
- 依赖 fsd / verify 的 CMake 目标，**不支持单独配置构建**（CMake 有防误用守卫）。
- 单元测试：`tests/unit/test_agent_config.cc`、`test_agent_state.cc`、
  `test_agent_run_once.cc`（离线负路径）；端到端见 `tests/integration/m1-e2e.sh`。
- 后续编排的计划与详细架构：见 `PLAN.md`（先评审，后执行）。
