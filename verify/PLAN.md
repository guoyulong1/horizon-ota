# M2 计划：verify → OTA 代理

> **2026-11 更新（已被拆分取代）**：按 `docs/adr/0001`，本目录已收缩为纯验签库
> （`crypto.*` / `manifest.*` + `ota-verify` CLI）；原计划中的编排部分
> （config / fetch / download / state / agent / CLI）整体迁至 `agent/`。
> 下文的模块结构与交付物以 `verify/README.md` 与 `agent/README.md` 为准，
> 本文仅作 M2 历史记录。


> 对应 `docs/OTA.md` 第 6 章 M2。目标：把 M1 的 `ota-fetch` 扩展成设备端 **OTA 代理**。**安装/确认/回滚的具体实现留给 M3**，但状态机的 phase 枚举在 M2 一次补全。

## 依赖与边界

- 设备端：链接 `fsd`（`HttpClient` / `SecureDownloader`）+ OpenSSL + nlohmann。
- **不依赖 pack**（发布端）。
- 不修改 fsd 的公开语义；只**新增**向后兼容的进度回调（见 §6）。

## 模块结构（verify/）

```
verify/
├── CMakeLists.txt
├── src/
│   ├── crypto.h/.cc        # Ed25519 验签（已有）
│   ├── manifest.h/.cc      # 解析 + schema 校验 + 适用性 → DownloadPlan（纯函数）
│   ├── fetch.h/.cc         # HttpClient 封装：取清单/签名/通道指针（无摘要）
│   ├── download.h/.cc      # SecureDownloader 编排：逐组件、进度、重试、sync
│   ├── config.h/.cc        # 设备配置
│   ├── state_store.h/.cc   # state.json 原子持久化
│   ├── report.h/.cc        # 上报接口（M2 写日志，预留 check-in）
│   ├── agent.h/.cc         # 状态机
│   └── ota_agent.cc        # ota-agent CLI 入口
└── README.md
```

**只保留一个可执行文件 `ota-agent`**（原 `ota-fetch` 的能力并入 `ota-agent check --once`）：

```
ota-agent check [--once] [--config <json>]
ota-agent status [--config <json>]
```

## 状态机（phase 枚举在 M2 一次补全）

```
Idle → Checking(②) → FetchingManifest(③) → Downloading(⑤/⑥)
     → Downloaded(⑦复核) → [M3] Waiting → Installing → PendingReboot
     → TrialBoot → Confirmed / RolledBack
```

| phase | M2 | 说明 |
|---|---|---|
| `idle` | ✅ | 空闲 |
| `checking` | ✅ | ② 发现 |
| `fetching_manifest` | ✅ | ③ 取清单 + 验签 + 解析 |
| `downloading` | ✅ | ⑤ 逐组件下载（fsd 内部 ⑥ 校验） |
| `downloaded` | ✅ | ⑦ 最小复核完成，等待安装（M3） |
| `waiting` | 占位 | 等待安装条件/时间窗（M3） |
| `installing` | 占位 | ⑧ 安装（M3） |
| `pending_reboot` | 占位 | 已提交待重启（M3） |
| `trial_boot` | 占位 | ⑨ 试启动（M3） |
| `confirmed` | 占位 | 已确认（M3） |
| `rolled_back` | 占位 | 已回滚（M3） |
| `error` | ✅ | 失败 |

M2 在 `downloaded` 后上报并结束；其余 phase 已定义但不会到达。

## ② 发现：M2 只做静态通道轮询（**会被替换**）

> **明确标注**：M2 只实现「静态通道轮询（方案 A）」：`GET channels/<channel>/<model>/manifest.json`（可选 `If-None-Match`）。`docs/OTA.md` 第 3 章已规划 B（check-in API）/C（MQTT）/D，M5 会按计划替换 ② 这一层。代码里把 ② 抽成 `Discovery` 接口，后续只替换实现。

## 一次运行的流程（`ota-agent check --once`）

1. 读配置。
2. ① 前置检查（时间/网络/空间；M2 简化）。
3. ② 发现（静态轮询）→ 清单地址 / 无更新。
4. ③ 取清单 + 签名 → 验签 → 解析 → schema 校验。
5. ④ 适用性判断（model / hardware_compat / layouts / security_version 下限 / min_version_code / version_code / 拉黑）。
6. 提高防回滚下限（M2 写 `state.json.security_version_floor`；U-Boot env 留 M3）。
7. ⑤ 逐组件下载（`SecureDownloader`，清单哈希/大小/续传/进度回调）。
8. ⑦ 下载后复核（文件存在 + 哈希）。
9. 写 `state.json`（`phase=downloaded`）。
10. 上报 `downloaded`（M2 写日志）。
11. 清理旧暂存（保留当前 release）。

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

地址/密钥/CA 走配置或环境变量，不写死、不入库。

## fsd 进度回调：最优分层（两级）

**结论：传输层与编排层各加一个回调，语义不同、各有用处。**

1. **传输层** `HttpRequest`：
   ```cpp
   std::function<void(std::uint64_t received, std::uint64_t total)> on_progress;
   ```
   字节级进度，直接挂在 curl 的 `XFERINFO` 上。`HttpClient` 的独立用途（取清单/签名）也能用。

2. **编排层** `DownloadOptions`：
   ```cpp
   struct DownloadProgress {
     std::string package_name;
     std::uint64_t received;
     std::uint64_t total;
   };
   std::function<void(const DownloadProgress&)> on_progress;
   ```
   `SecureDownloader` 内部把 1 的字节回调包装成 2，并补上 `package_name`。

3. **Agent** 订阅 2，聚合成 release 级进度（组件 + 整体）。

**为什么不在单层**：只有字节级，agent 得自己维护「哪个组件」的上下文；只有语义级，`HttpClient` 的独立用途就拿不到进度。两级各司其职。
**为什么用 `std::function`**：fsd 风格简洁、进度是单一事件；将来若要 `OnRetry`/`OnRedirect` 等，再升级为观察者接口。

## 状态持久化（phase 完整）

`state.json`：

```json
{
  "schema": 1,
  "phase": "idle|checking|fetching_manifest|downloading|downloaded|waiting|installing|pending_reboot|trial_boot|confirmed|rolled_back|error",
  "release_id": "…",
  "last_error": "…",
  "retry": 0,
  "security_version_floor": 5
}
```

原子写（`.tmp` + rename），掉电不写坏。

## 错误与重试

- 网络类：`--once` 记录并退出非零；常驻模式（M3）再加退避。
- 验签/校验失败：不重试，记 `release_id` 拉黑。
- 错误码沿用 fsd `ErrorCode`。

## 可测试性（配合另一智能体的 gtest）

- `manifest`（解析 + 适用性）设计为**纯函数**，输入 JSON，输出 `DownloadPlan` 或错误。
- `agent` 通过接口注入 `Discovery` / `Fetch` / `Download`，便于 fake 测状态机。

## 交付物

- `verify/` 模块化重构 + `ota-agent`（唯一可执行文件）。
- fsd 两级进度回调（向后兼容）。
- `verify/README.md` 更新。

## 已确认

1. ota-fetch 合并进 ota-agent（唯一工具）。
2. M2 只做静态轮询，代码抽成 `Discovery` 接口，明确后续会替换。
3. 状态机 phase 一次补全（含 M3 占位）。
4. fsd 进度回调采用两级（传输层 `HttpRequest` + 编排层 `DownloadOptions`）。
