# horizon-ota

地平线 X3 设备 OTA 全流程的单上下文术语表。流程、契约与判据见 `docs/OTA.md`；
模块边界决策见 `docs/adr/`。

## Language

### 设备端模块

**OTA 代理（OTA agent）**:
设备端的状态机与流程编排者：发现、验签调用、下载编排、安装、确认、回滚、上报。
_Avoid_: 把 verify 模块称为「代理」

**验签（manifest verification）**:
用内置公钥对发布清单的**原始字节**做 Ed25519 验证；通过之后才允许解析 JSON。
_Avoid_: 签名校验（泛称）、checksum 校验（那是组件哈希）

**适用性判断（applicability）**:
按 `docs/OTA.md` 3.5 的固定顺序判断一份**已验签**清单是否适用于本设备。
_Avoid_: 版本比较（它只是其中一项）

**安全下载模块（fsd）**:
只负责安全传输与完整性校验的设备端库；不验签、不做防回滚、不做编排。
_Avoid_: 下载器 / OTA 模块混称

### 发布契约

**发布清单（release manifest）**:
发布端生成并签名的 JSON，描述一个版本的全部信息；设备端只信这份文件。
_Avoid_: 包描述文件、metadata

**下载清单（package manifest）**:
下载器读取的简单配置（下载哪个文件、期望哈希）；不是信任来源。
_Avoid_: 与发布清单混用

**组件（component）**:
发布清单里的一个可下载文件：内核、dtb、rootfs。

**通道（channel）**:
`dev` / `beta` / `stable` 等发布通道，设备订阅其中一个。

### 升级安全

**防回滚下限（security_version floor）**:
设备记录的 `security_version` 下限；低于它的已签名清单一律拒绝（防回滚攻击）。

**试启动（trial boot）**:
新固件已启动、但未通过健康检查的阶段；失败超限自动回滚。

**提交点（commit point）**:
安装流程中唯一不可逆的写入动作（U-Boot 环境变量三值一次写入）；之前掉电停在旧系统，之后掉电进入可识别的试启动。
