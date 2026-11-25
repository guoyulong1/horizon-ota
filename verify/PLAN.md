# M2 计划：verify → OTA 代理

> 对应 `docs/OTA.md` 第 6 章 M2。目标：把 `verify/ota-fetch`（M1 最小版）扩展成设备端 **OTA 代理**：周期发现 → 取清单验签 → 适用性判断 → 调用 fsd 下载 → 状态持久化 → 上报（可 stub）。**安装/确认/回滚留给 M3。**

## 依赖与边界

- 设备端：链接 `fsd`（`HttpClient` / `SecureDownloader`）+ OpenSSL + nlohmann。
- **不依赖 pack**（发布端）。
- 不修改 fsd 的公开语义；只**新增**一个向后兼容的进度回调（见 §6）。

## 模块结构（verify/）

```
verify/
├── CMakeLists.txt
├── src/
│   ├── crypto.h/.cc        # Ed25519 验签（已有，扩展为 ota_verify_crypto 库）
│   ├── manifest.h/.cc      # 解析 + schema 校验 + 适用性 → DownloadPlan
│   ├── fetch.h/.cc         # HttpClient 封装：取清单+签名（无摘要）、取通道指针
│   ├── download.h/.cc      # SecureDownloader 编排：逐组件、进度、重试、sync
│   ├── config.h/.cc        # 设备配置（base_url/model/channel/keys/paths）
│   ├── state_store.h/.cc   # 持久状态（state.json：phase/release_id/重试/下限）
│   ├── report.h/.cc        # 上报接口（M2 静态方案下写日志；预留 check-in）
│   ├── agent.h/.cc         # 状态机 + 一次/循环执行
│   └── ota_agent.cc        # ota-agent CLI 入口
└── README.md
```

可执行文件：新增 `ota-agent`；`ota-fetch` 保留为调试工具（或按你意见合并进 `ota-agent check --once`）。

## 状态机（M2 范围）

按 `docs/OTA.md` 1.3，M2 只实现前半段：

```
Idle → Checking(②) → FetchingManifest(③ 下载+验签+解析)
                    → Downloading(⑤ 逐组件, fsd 内部 ⑥ 校验)
                    → Downloaded(⑦ 最小复核) → Idle(上报后)
```

`Installing / PendingReboot / TrialBoot / Confirmed / RolledBack` 是 **M3 占位**，M2 在 `Downloaded` 后上报并结束。

## 一次运行的流程（`ota-agent check --once`）

1. 读配置（JSON 文件或环境变量）。
2. ① 前置检查（时间/网络/空间；M2 简化：网络可达、暂存区可写）。
3. ② 发现：静态通道轮询 `channels/<channel>/<model>/manifest.json` → 清单地址；或「无更新」。
4. ③ 取清单 + 签名 → 验签 → 解析 JSON → 校验 `schema_version`。
5. ④ 适用性判断：`model` / `hardware_compat` / `layouts` / `security_version` 下限 / `min_version_code` / `version_code` / 拉黑列表。
6. 提高防回滚下限（M2 先写 `state.json` 的 `security_version_floor`；U-Boot env 留 M3）。
7. ⑤ 逐组件下载（`SecureDownloader`，清单哈希/大小/续传/进度回调）。
8. ⑦ 下载后复核（文件存在 + 哈希；M2 重算一次）。
9. 写 `state.json`（`phase=downloaded`，带 `release_id`）。
10. 上报 `downloaded`（M2 stub：写日志）。
11. 清理旧暂存（保留当前 release）。

## CLI 设计

```
ota-agent check [--once] [--config <json>]
ota-agent status [--config <json>]
```

- `--once`：跑一个周期后退出（便于 CI / 定时器触发）。
- 常驻循环（daemon）M2 先不做；用 cron/systemd timer 触发 `--once`，M3 再加常驻与退避。
- 配置读取优先级：`--config` > 环境变量 > 默认路径（`/data/ota/agent.json`）。

## 配置（JSON 示例）

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
  "security_version_floor": 5
}
```

- 地址、密钥、CA 走配置/环境变量，**不写死、不入库**。

## fsd 扩展：进度回调（M2 必须）

现状：fsd 只有内部 curl 回调打印 stderr，**无公开 API**。M2 需要：

- `HttpRequest` 增加 `std::function<void(std::uint64_t received, std::uint64_t total)> on_progress;`
- `DownloadOptions` 增加同名字段，透传到 `HttpRequest`。
- `ReportProgress` 调用它（空回调时维持现状，仍可打印 stderr）。
- **向后兼容**：不改变现有行为；pack 不依赖 fsd，不受影响。

## 状态持久化

`state.json`：

```json
{
  "schema": 1,
  "phase": "idle|checking|downloading|downloaded|error",
  "release_id": "…",
  "last_error": "…",
  "retry": 0,
  "security_version_floor": 5
}
```

原子写（`.tmp` + rename），掉电不写坏。

## 错误与重试

- 网络类：`--once` 模式下记录并退出非零；常驻模式（M3）再指数退避。
- 验签/校验失败：不重试，记 `release_id` 到拉黑列表（M2 先写 error）。
- 错误码沿用 fsd `ErrorCode`，上报时带错误名。

## 可测试性设计（配合另一智能体的 gtest）

- `manifest`（解析 + 适用性）设计为**纯函数**，输入 JSON 字符串，输出 `DownloadPlan` 或错误，便于单测。
- `agent` 通过抽象接口注入 `fetch` / `download`，便于用 fake 实现测状态机。

## 交付物

- `verify/` 模块化重构 + `ota-agent`。
- fsd 进度回调（向后兼容）。
- `verify/README.md` 更新（配置、CLI、状态机）。
- 与 `ota-fetch` 并存（或合并，见待确认）。

## 待确认

1. `ota-fetch` 合并进 `ota-agent check --once`（只保留一个工具），还是两个都保留？
2. ② 发现 M2 只做「静态通道轮询（方案 A）」可以吗？（check-in API 是 M5）
3. `security_version` 下限 M2 先写 `state.json`（U-Boot env 留 M3）可以吗？
4. 进度回调加到 fsd `HttpRequest`/`DownloadOptions`（向后兼容）是否接受？
