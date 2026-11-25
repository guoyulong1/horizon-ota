# verify

设备端 **OTA 代理**（M2）。链接 `fsd` 静态库，复用它做 HTTPS 传输与组件下载；不依赖 `pack`（发布端）。

## 能力

- ② 发现：静态通道轮询（M2）。**会被替换**：`docs/OTA.md` 第 3 章的 check-in / MQTT 方案后续替换这一层。
- ③ 取发布清单 + 签名（`fsd::HttpClient`，无摘要）→ Ed25519 验签 → JSON 解析。
- ④ 适用性判断（model / hardware_compat / layouts / security_version 下限 / min_version_code / version_code）。
- ⑤ 逐组件下载（`fsd::SecureDownloader`，清单哈希 + 续传 + 进度回调）。
- ⑦ 下载后复核（文件存在）。
- 状态持久化 `state.json`（phase 完整：`idle/checking/fetching_manifest/downloading/downloaded/waiting/installing/pending_reboot/trial_boot/confirmed/rolled_back/error`，M3 的 phase 先占位）。

安装 / 确认 / 回滚是 **M3**。

## 用法

```bash
ota-agent check [--once] --config <json> [--verbose]
ota-agent status --config <json>
```

`--once`：跑一个周期后退出（用 cron / systemd timer 触发；常驻与退避在 M3）。

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
| `crypto.*` | Ed25519 验签（与 fsd 同一 OpenSSL） |
| `manifest.*` | 清单解析 + schema 校验 + 适用性（纯函数，便于 gtest） |
| `config.*` | 配置加载 |
| `fetch.*` | `HttpClient` 取小文件 |
| `download.*` | `SecureDownloader` 逐组件下载 + 进度 |
| `state.*` | `state.json` 原子读写 |
| `agent.*` | 状态机 `RunOnce` |
| `ota_agent.cc` | CLI |
