# agent 编排：软件架构与执行计划（M3）

> **状态：计划文档。未经评审确认，不得开始对应编码**——本文件是「先写计划、
> 后执行」的执行前提。
> 依据：`docs/OTA.md`（端到端作业书）、`docs/adr/0001`（模块边界）、`GLOSSARY.md`。
> 范围：agent 从 M2 现状到 M3（安装/确认/回滚/常驻）的完整软件架构与落地里程碑；
> M5（check-in / MQTT / HTTP 上报）只做接口预留，不在本计划实现。

## 1. 现状与目标

**M2 现状**（已随边界拆分迁入 `agent/src/`）：

| 已有 | 说明 |
|---|---|
| `RunOnce(config, verbose)` | 单周期 ②发现 → ③取清单验签 → ④适用性 → ⑤下载 → ⑦复核 |
| `state.json`（v1） | phase / release_id / last_error / retry / security_version_floor，原子写 |
| `ota-agent check --once` / `status` | CLI，一次性执行 |

**M3 目标**（验收用例 = `docs/OTA.md` 5.2 的 T8、T9、T10、T11）：

1. ⑧ 安装：把暂存区组件写入**非活动槽**（A/B）或经 recovery 写入主系统。
2. ⑨ 确认与回滚：试启动健康检查、`bootlimit` 超限自动回滚、版本拉黑。
3. 常驻运行：定时轮询、失败退避、时间窗、单实例锁；`--once` 形态保留。
4. 掉电安全：`docs/OTA.md` 4.11 掉电矩阵逐行成立。

**明确不做**（防越界，见第 11 节）：发布端/服务端任何逻辑、MQTT、`boot.scr`
脚本本体、分区表变更、`fsd`/`verify` 内部改造。

## 2. 总体架构

### 2.1 分层与依赖方向

```
ota-agent (CLI / 常驻进程, src/ota_agent.cc —— 组合根)
   │
   └── Orchestrator core (src/agent.cc)
         ├── Scheduler          定时器/退避/时间窗（常驻模式）
         ├── Discovery          ② 发现（接口）
         ├── ManifestPipeline   ③④ 编排：fetch → 调 verify 库 → 决定
         ├── DownloadGateway    ⑤⑥⑦ 编排：调 fsd → 复核 → StagedRelease
         ├── InstallBackend     ⑧ 安装（接口）→ AbInstallBackend / RecoveryPlan
         ├── BootControl        槽位/试启动/bootcount/防回滚下限（U-Boot env）
         ├── HealthChecker      ⑨ 健康检查
         ├── StateStore         state.json 原子读写（已有，扩 v2）
         ├── Blacklist          本机拉黑列表（data 分区 JSON）
         └── Reporter           ⑩ 上报（接口；M3 只写日志）
```

**依赖规则（边界即架构）**：

- `ManifestPipeline` 是 `ota_verify` 库的唯一调用方（③④）；`DownloadGateway`
  是 `fsd` 的唯一调用方（⑤⑦）。二者之外不得出现 `fsd/*`、`manifest.h`、
  `crypto.h` 的 include。
- `BootControl` 是 `fw_printenv` / `fw_setenv` 的唯一调用方（子进程封装）。
- 除 `ManifestPipeline` / `DownloadGateway` / `BootControl` 三个「真实副作用」
  适配器外，其余组件全部面向接口编程，测试用 fake 注入（见第 9 节）。

### 2.2 进程与线程模型

- 单进程、单线程（无内部并发）。并发风险只有「两个实例同时跑」，用
  `flock(/data/ota/agent.lock)` 排他锁解决：拿不到锁 → 本次周期直接退出 0。
- recovery 侧是**另一个进程形态**（同一套源码、静态链接、入口不同），不是线程。
- 常驻模式与 `--once` 模式共用同一个 `Orchestrator`，只差 `Scheduler` 是否装配。

## 3. 接口设计（签名级）

以下签名是 M3-a 评审对象；实现时允许微调字段名，不允许改变依赖方向。

### 3.1 Discovery（②，M5 替换点）

```cpp
// src/discovery.h
struct DiscoveryResult {
  enum class Outcome { kNoUpdate, kManifestAvailable };
  Outcome outcome = Outcome::kNoUpdate;
  std::string manifest_path;      // 相对 base_url 的路径
  std::chrono::seconds poll_after{0};  // B/C 形态下发；静态形态为 0（用本地配置）
};

class Discovery {
 public:
  virtual ~Discovery() = default;
  // 一次发现。网络失败抛 DiscoveryError（退避由 Scheduler 统一管）。
  virtual DiscoveryResult Discover() = 0;
};
```

M3 实现 `StaticChannelDiscovery`（现 `RunOnce` 里的 `channels/<c>/<m>/manifest.json`
轮询 + ETag 条件请求，ETag 存 `state.json`）。**M5 只新增实现类，不改接口**——
这就是「② 的输出固定成『一个清单地址』」在代码上的落点。

### 3.2 ManifestPipeline（③④）

```cpp
// src/manifest_pipeline.h
struct ManifestDecision {
  bool proceed = false;           // false → 不下载，原因在 decision/message
  verify::Manifest manifest;      // 已验签、已解析
  verify::Decision decision;      // verify::CheckApplicability 的原始结论
};

class ManifestPipeline {
 public:
  // 下载 manifest+sig → 验签 → 解析 → 适用性 →（通过时）提高防回滚下限。
  // 任何一步失败抛 PipelineError，分类见第 7 节。
  virtual ManifestDecision Evaluate(const std::string& manifest_path) = 0;
};
```

内部固定顺序（信任链，顺序不可变）：`FetchToFile(manifest)` →
`FetchToFile(manifest + ".sig")` → `ReadAll` → `ParseHexPublicKey` →
`VerifyEd25519`（原始字节）→ `ParseManifest` → `CheckApplicability` →
`BootControl::RaiseSecurityFloor`。验签失败 = **本周期终止 + 记录 release_id**，
绝不进入下载。

### 3.3 DownloadGateway（⑤⑥⑦）

```cpp
// src/download_gateway.h
struct StagedRelease {
  std::string release_id;
  std::filesystem::path dir;            // <staging_dir>/<release_id>/
  std::vector<verify::Component> components;  // 已存在且哈希一致
};

class DownloadGateway {
 public:
  // 下载前本地判断：已存在且哈希一致 → 跳过；不符 → 删除重下（docs/OTA.md 4.5）。
  // ⑥ 完整性校验在 fsd 内部完成（摘要一致才原子改名）。
  // 全部组件完成后执行一次 sync()。
  virtual StagedRelease DownloadAll(const verify::Manifest& manifest) = 0;
  // ⑦ 安装前复核：重算暂存文件哈希（下载与安装间可能断电）。
  virtual void ReVerify(const StagedRelease& staged) = 0;
};
```

### 3.4 InstallBackend（⑧，M3 核心）

```cpp
// src/install_backend.h
struct InstallPlan {
  std::string target_slot;                 // "b"（A/B）或 "main"（recovery）
  std::vector<WriteOp> writes;             // 有序写入操作
  bool reboot_after = true;
};

class InstallBackend {
 public:
  virtual bool Supports(const std::string& layout) const = 0;  // "ab"|"recovery"
  // ⑦ 的安装侧断言在这里复核：目标必须是非活动槽、容量足够。
  // 返回的 plan 已通过断言；调用方负责执行与提交。
  virtual InstallPlan Plan(const StagedRelease& staged,
                           const verify::Manifest& manifest) = 0;
  // 执行写入 + 回读校验。失败抛 InstallError（当前系统不受影响）。
  virtual void Execute(const InstallPlan& plan) = 0;
  // 提交：唯一提交点。A/B = fw_setenv 一次写三值；recovery = 置 ota_recovery。
  // 幂等：重复执行结果一致。
  virtual void Commit(const InstallPlan& plan) = 0;
};
```

**AbInstallBackend 写入顺序**（设活动槽 = A，目标 = B，对应 `docs/OTA.md` 4.8）：

1. 写 `rootfs_b` 整分区 → 回读校验（只比镜像长度那么多字节）。
2. 格式化 `boot_b` → 拷贝 kernel/dtb → 校验哈希 → `sync` + 卸载。
3. `sync`（data 分区，保证暂存与状态文件落盘）。
4. 写 `state.json`：`phase=pending_reboot`，带 `release_id` 与目标槽。
5. `Commit()`：`fw_setenv` **一条命令**写 `bootslot` / `upgrade_available` /
   `bootcount=0`（三值一次写入，无中间态）。
6. 按 `install.mode` 决定重启时机（见 3.8 Scheduler）。

**RecoveryPlan（M4，接口先留位）**：主系统侧 = 置 `ota_recovery=1` + 状态文件
（待 recovery，带 release_id）+ 重启；recovery 侧 = 同一源码静态链接出的
`ota-agent --recovery-upgrade` 入口（重新验签 → 重算哈希 → 写分区 → 清标志 →
置试启动）。M3 只交付 A/B，recovery 的入口桩与文档位先建好（M4 计划另立文档）。

### 3.5 BootControl（U-Boot 环境真值）

```cpp
// src/boot_control.h
class BootControl {
 public:
  virtual std::string ActiveSlot() = 0;                 // 读 bootslot / 启动参数
  virtual bool UpgradeAvailable() = 0;                  // upgrade_available
  virtual int BootCount() = 0;                          // bootcount
  virtual int BootLimit() = 0;                          // bootlimit（出厂定，只读）
  virtual void RaiseSecurityFloor(long long v) = 0;     // 只在新值更大时写
  virtual long long SecurityFloor() = 0;
  virtual void ClearTrialBoot() = 0;                    // 确认：清标志+计数
};
```

- 实现 `UbootEnvControl`：`fw_printenv` / `fw_setenv` 子进程，输出解析 +
  超时（默认 10 s）+ 失败重试 1 次；**写入口全进程唯一**（防并发写环境）。
- 所有标志只用 `0/1` 赋值，不做「删除变量」写法（`docs/OTA.md` 4.9）。
- `RaiseSecurityFloor` 在**适用性通过后、下载前**调用（先提下限再安装）。

### 3.6 HealthChecker（⑨）

```cpp
// src/health.h
struct HealthReport {
  bool healthy = false;
  std::vector<std::string> failed_checks;
};

class HealthChecker {
 public:
  // 逐项执行（docs/OTA.md 4.9）：根分区只读挂载、关键目录、主应用存活、
  // 网络连通（一次 check-in 或 TCP 探测）、外设自检。全部通过才算健康。
  virtual HealthReport Run() = 0;
};
```

- 试启动期间：**全部通过才开始喂狗**（watchdog 子系统 `/dev/watchdog`，
  超时时间从配置读，默认取 `bootlimit` 对应窗口的一半）；任一项失败 → 不喂，
  交给看门狗复位，`boot.scr` 计数。
- 无看门狗的设备：`ConfirmDeadline`（试启动开始后 N 分钟未确认 → 主动重启不确认）。
- 健康检查**只在试启动期间运行**（`BootControl::UpgradeAvailable()` 为真）；
  正常运行不跑、不喂狗。

### 3.7 StateStore / Blacklist / Reporter

```cpp
// state.json v2（向后兼容读 v1）：
struct AgentState {
  int schema = 2;
  std::string phase;            // 12 值枚举，见第 4 节
  std::string release_id;
  std::string target_slot;      // v2 新增：pending_reboot 后记录目标槽
  std::string manifest_etag;    // v2 新增：② 条件请求
  std::string last_error;
  int retry = 0;
  long long security_version_floor = 0;
};

// blacklist.json（data 分区）：由 rolled_back 事件追加
struct BlacklistEntry { std::string release_id; std::string reason; std::string at; };

// Reporter：M3 = LogReporter（结构化一行一事件到 stdout/journald）；
// M5 换 HttpReporter（check-in POST），接口不变。
class Reporter { public: virtual void Emit(const ReportEvent& event) = 0; };
```

事件类型沿用 `docs/OTA.md` 4.10：`checking / downloading / downloaded /
pending_reboot / confirmed / failed / rolled_back`。

### 3.8 Scheduler（常驻模式）

```cpp
// src/scheduler.h
struct PollPolicy {
  std::chrono::seconds base_interval;     // stable 6h / beta 15m / dev 5m
  double jitter_ratio = 0.2;              // ±20% 抖动
  std::chrono::seconds backoff_max{24h};  // 失败退避封顶
  std::optional<TimeWindow> install_window;  // install.window，只约束「重启」
  std::chrono::seconds reboot_delay{0};   // install.reboot_delay_seconds
};
```

- 轮询循环：`sleep(interval ± jitter)` → `RunOnce()`；网络类失败按
  1→2→4→…h 退避（封顶 24h）；验签/适用性失败不退避（下周期再见）。
- **检查与下载不受时间窗限制**；`install.window` 只约束「重启」这一个动作：
  提交后若不在窗内 → `phase=waiting`，窗内触发重启；`immediate` 模式加
  `reboot_delay` 后立即重启；`download-only` 在 ⑦ 后停止，等外部确认命令。
- 信号：SIGTERM/SIGINT → 停止接受新周期、等待当前周期结束（安装进行中不中断，
  写完当前组件后退出）。

## 4. 状态机与跨重启恢复

### 4.1 phase 与迁移

phase 枚举沿用 v1 的 12 值。迁移表（事件 → 次态）：

| 现态 | 事件 | 次态 | 动作 |
|---|---|---|---|
| idle | 定时器到 / 推送 | checking | ② |
| checking | 无更新 | idle | — |
| checking | 拿到清单地址 | fetching_manifest | ③ |
| fetching_manifest | 验签失败 / 不适用 | idle | 记 release_id；验签失败上报 |
| fetching_manifest | 通过 | downloading | 提防回滚下限已完成 |
| downloading | 网络失败 | downloading | fsd 内续传；退避 |
| downloading | 重试耗尽 / 哈希不符 | error | 上报 failed |
| downloading | 全部组件通过 | downloaded | ⑦ 复核 |
| downloaded | 条件不满足 | waiting | 等时间窗 / 电源 |
| waiting / downloaded | 条件满足 | installing | ⑧ |
| installing | 写入/校验失败 | error | 清非活动槽状态；当前系统不受影响 |
| installing | 已提交 | pending_reboot | 状态文件已写 |
| pending_reboot | 重启 | trial_boot | **跨重启，见 4.3** |
| trial_boot | 健康检查通过 | confirmed | 清试启动标志；上报；清理暂存 |
| trial_boot | 超 bootlimit | rolled_back | （旧系统里）上报 + 拉黑 + 清理 |
| confirmed / rolled_back | 收尾完成 | idle | — |

### 4.2 真值来源优先级

**U-Boot env > state.json > 缺省**。env 是提交点真值（`upgrade_available` /
`bootslot` / `bootcount`），state.json 是流程真值（phase / release_id）。
恢复时先读 env 再读 state，两者矛盾时以 env 为准（env 是掉电安全的提交点）。

### 4.3 三个跨重启恢复算法（每次启动 `Recover()` 先于一切周期）

```
Recover():
  env_up = BootControl.UpgradeAvailable()
  st = StateStore.Load()            # 缺失 = v1 初始
  if not env_up and st.phase == "pending_reboot":
      # 状态说待重启、env 说没有升级 → 提交没做成或被脚本回退 → 重做提交（幂等）
      ReCommit(st.target_slot); return
  if env_up and st.phase in {"downloaded","installing","pending_reboot"}:
      st.phase = "trial_boot"; Save(); return          # 已在新系统试启动
  if not env_up and st.phase == "trial_boot":
      # 新系统启动了但试启动标志被清 → 看 running release 是否等于 st.release_id
      if RunningRelease() != st.release_id:
          st.phase = "rolled_back"; Blacklist.Add(st.release_id); Save();
          Reporter.Emit(rolled_back); Cleanup(st.release_id); return
      st.phase = "confirmed"      # 试启动中确认后断电的边界；重新确认无害
  if env_up and st.phase == "trial_boot":
      HealthGate()   # 正常试启动路径：健康检查 + 喂狗/确认期限
```

- 提交幂等：`ReCommit` 重复写同一组 env 值，结果一致。
- 回滚检测依赖「运行中 release」：`/data/ota/state.json` 的 `release_id` 与
  当前系统标记（`/etc/ota-release`，安装时写入目标槽）比对，不一致 = 被回滚。
- 拉黑之后回到正常轮询；拉黑项在适用性第 7 项生效；运维远程清除（M5）。

## 5. 安装后端细节

### 5.1 分区与槽位识别

- 分区用 **`/dev/disk/by-partlabel/`**（`boot_a/boot_b/rootfs_a/rootfs_b`）；
  GPT 分区 UUID 建表时固定，禁止设备名（`mmcblk*` 编号可互换）。
- 活动槽判定：`/proc/cmdline` 的 `root=PARTUUID=` → 反查 partlabel；
  目标槽 = 非活动槽。**断言：任何写入路径不得包含活动槽**（`Execute` 前置检查，
  违反直接抛错——这是安装器最重要的一条断言）。

### 5.2 写入与校验

- rootfs：整分区写（O_DIRECT 不做，用普通写 + `fsync` + `sync`），回读校验
  只比镜像长度那么多字节（分区尾部空洞不比）。
- boot 槽：`mkfs.vfat`（或按配置 ext4）→ 拷贝 `Image`、`*.dtb` → 逐文件
  SHA-256 → `sync` → 卸载。
- 容量前置检查：目标分区容量 > 组件 `size`。
- 写入后回读校验失败 → 抛 `InstallError`，清目标槽状态、`phase=error`、上报；
  当前系统不受影响。

### 5.3 recovery 复用方式（M4 位）

- 同一 `agent` 源码，两种链接形态：主系统动态（或大静态）+ recovery 全静态
  （busybox 环境）。CMake 提供 `ota-agent-recovery` 目标（静态、精简入口）。
- recovery 入口重做 ③④（不能信任主系统检查过）——直接复用
  `ManifestPipeline` + `DownloadGateway` + `InstallBackend`，体现「库化接口」
  的价值：recovery 侧不新写逻辑。

## 6. 常驻进程运行手册

- 锁：`flock(/data/ota/agent.lock, LOCK_EX|LOCK_NB)`，失败 → exit 0。
- systemd unit 草案（随模块交付 `agent/packaging/ota-agent.service`）：

```ini
[Unit]
Description=OTA agent (poll & install)
After=network-online.target time-sync.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/ota-agent run --config /etc/ota/agent.json
Restart=on-failure
RestartSec=30
# 试启动期间由健康检查门控喂狗；unit 不配 WatchdogSec，避免双重狗。

[Install]
WantedBy=multi-user.target
```

- `--once` 形态保留给 cron/systemd timer（`ota-agent check --once`）。
- 日志：结构化单行（ts, phase, event, release_id, detail）到 stdout，
  journald 接管；无文件日志（rootfs 只读）。

## 7. 错误分类与重试矩阵

| 错误源 | 分类 | 重试 | 上报 |
|---|---|---|---|
| 清单 404 | 发布错误 | 不重试，下周期 | 不报（常态） |
| 验签失败 / 坏 JSON / schema 不支持 | 安全/发布错误 | 不重试；本周期终止 | `failed` + release_id |
| 适用性拒绝（7 类） | 预期路径 | 不重试 | `checking` 消息带原因 |
| fsd 网络/TLS/HTTP5xx | 传输错误 | 退避续传（表见 4.5） | 进度/失败 |
| fsd 校验和不符 | 坏包 | 临时文件已删；≤2 次 | 连续不符 `failed` |
| 磁盘满 | 本地资源 | 清理旧暂存后重试 1 次 | `failed` |
| 安装写入/回读失败 | 安装错误 | 不自动重试（等下周期重来） | `failed` + 步骤号 |
| 提交（fw_setenv）失败 | 严重 | 重试 1 次；再失败 `error` | `failed` |
| 健康检查不通过 | 试启动路径 | 喂狗停 → 复位；超限自动回滚 | `rolled_back` |
| 时间未同步 | 前置 | 等待；超阈值上报 | `checking` 提示 |

OTA 层新增错误码（沿用 fsd `ErrorCode` 风格）：`kInstallFailed`、
`kCommitFailed`、`kSlotAssertion`、`kHealthCheckTimeout`。

## 8. 数据契约演进

- `config.json` M3 新增（全部可选、带缺省）：`install_window`、`reboot_delay_seconds`、
  `health_timeout_seconds`、`watchdog_timeout_seconds`、`confirm_deadline_minutes`、
  `poll_interval_seconds`（覆盖通道缺省）。
- `state.json` → schema 2（读 v1 兼容；写总是 v2）。
- 新文件：`blacklist.json`（data 分区，原子写同 state）、`agent.lock`、
  `/etc/ota-release`（安装时写进目标槽 rootfs 的标记文件，供回滚比对）。
- U-Boot env 变量沿用 `docs/OTA.md` 4.9 表，不新增变量名。

## 9. 测试计划

| 层 | 内容 | 位置 |
|---|---|---|
| 单元 | Discovery/InstallBackend/BootControl/HealthChecker/Reporter 的 fake + 状态机恢复表驱动用例（4.3 每个 if 分支至少一例） | `tests/unit/test_agent_*.cc` |
| 单元 | StateStore v2 往返 + v1 升级读；Blacklist 原子写 | `tests/unit/test_agent_state.cc` 扩展 |
| 集成（x86/板） | `ManifestPipeline` 假服务器全链路（复用 `serve.py`）：验签失败/适用性拒绝/哈希不符 | `tests/integration/` |
| 板端冒烟（非破坏） | `agent/scripts/board-verify.sh`：self-test → 真实下载到 data 分区 → **loop 设备模拟安装**（rootfs 镜像写入 loop + 回读校验，不碰真分区） | 板载 |
| 板端破坏性（人工开关） | T8 坏固件回滚、T10 掉电矩阵——需要 `--allow-destructive` 显式开关 + 确认提示 | 板载，单独执行 |

## 10. 里程碑（每步：先 PR 评审 → ctest 全绿 → 板端冒烟，再进下一步）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M3-a | `BootControl` + `StateStore` v2 + `Recover()`（4.3 全分支）+ 单实例锁 | 恢复用例单测全绿；板上 env 读写正确 |
| M3-b | `AbInstallBackend`（loop 模拟先行）+ 提交 + 试启动确认 + 回滚拉黑 | T8/T9 通过（板上真升级一轮 + 人为坏固件） |
| M3-c | 常驻 `Scheduler` + 时间窗 + `HealthChecker`/喂狗 + `LogReporter` | T10 掉电矩阵逐行演练；T11 断电试启动 |
| M4 | recovery 方案（另立计划文档） | T12 |
| M5 | check-in / MQTT Discovery、HttpReporter（只换实现类） | 服务端可见设备状态 |

## 11. 明确不做（越界防线）

1. 不在 agent 内实现任何验签/适用性逻辑（一律调 `verify`）。
2. 不在 agent 内实现任何传输/续传/摘要逻辑（一律调 `fsd`）。
3. 不读写发布端（`pack`）代码或头文件；只消费发布清单 JSON 契约。
4. 不修改 `boot.scr`、分区表、U-Boot 本体；env 变量名不新增。
5. M5 的 check-in/MQTT 只新增 `Discovery`/`Reporter` 实现类，不改其余组件接口。
