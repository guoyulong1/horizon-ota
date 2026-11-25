# M1 计划：打包 + 签名（发布端契约）

> 对应 `docs/OTA.md` 第 6 章 M1。目标：把一次构建的产物整理成可发布目录，并在设备端走通「取清单 → 验签 → 下载」。

## 目标与范围

**做**：

- 打包工具：收集组件、算哈希、生成发布清单。
- 签名工具：离线 Ed25519 签名，产出 `manifest.json.sig`。
- 设备端 M1 脚本：取清单 + 验签 + 用 `fsd` 下载组件。
- 静态发布目录约定（`releases/`、`channels/`）。
- 集成测试覆盖「打包 → 签名 → 篡改 → 下载」。

**不做（留给 M2+）**：

- 设备端 JSON 解析 + 验签的 C++ 库（M2 的 `verify/`）。
- OTA 代理状态机、安装、确认、回滚（M2–M4）。

## 工具设计（建议 Python 3 + openssl）

用 **Python 3**（`json`/`hashlib` 标准库）生成清单，避免 bash 拼 JSON 的转义坑；Ed25519 签名/验签调用 `openssl` CLI（离线机、板子、CI 都有）。全部参数走**环境变量**，不写死地址/密钥。

### `pack/pack.py`

输入（环境变量或参数）：

| 变量 | 含义 |
|---|---|
| `IMAGES_DIR` | 构建产物目录（含 Image、<dtb>、rootfs.squashfs） |
| `MODEL` / `VERSION` / `DTB` | 型号 / 版本 / dtb 文件名 |
| `SECURITY_VERSION` / `MIN_VERSION_CODE` / `HARDWARE_COMPAT` | 清单字段（见 docs/OTA.md 2.1） |
| `OUTPUT_DIR` | 发布根目录 |

输出：

```
$OUTPUT_DIR/releases/<model>/<version>/<release_id>/
├── manifest.json
├── manifest.json.sig      ← 由 sign 步骤产生
└── files/{Image,<dtb>,rootfs.squashfs}
```

要点：`release_id` = 组件哈希列表的哈希前 12 位；清单字段严格按 `docs/OTA.md` 2.3；校验输入（版本号、兼容 ID、必填项），非法输入拒绝且不留半成品。

### `pack/sign.sh` / `pack/verify.sh`

- `sign.sh`：`openssl pkeyutl -sign -rawin -inkey "$OTA_PRIVATE_KEY" -in manifest.json -out manifest.json.sig`
- `verify.sh`：`openssl pkeyutl -verify -pubin -inkey "$OTA_PUBLIC_KEY" -rawin -in manifest.json -sigfile manifest.json.sig`
- 私钥路径用 `OTA_PRIVATE_KEY` 环境变量；**私钥绝不入库**（`.gitignore`）。

### 设备端 M1：`verify/m1-fetch.sh`（临时，M2 再升级为 C++）

1. 用 `curl --cacert "$OTA_CA"` 取 `manifest.json` + `manifest.json.sig`（小文件；信任来自签名，不是 TLS 哈希）。
2. 用 `openssl pkeyutl -verify` 验签（对象是清单原始字节）。
3. 用 `python3`（或 `jq`）解析清单，校验 `model`/`hardware_compat`/`security_version` 等适用性。
4. 对每个组件，调 `fsd` CLI：`--url "$BASE/$path" --sha256 "$sha256" --expected-size "$size" --ca-file "$OTA_CA"`。

## 公开仓库安全约定

- 板子 IP、服务器域名、CA、密钥一律**不写进仓库**；用环境变量（`BOARD_HOST`、`OTA_BASE_URL`、`OTA_PRIVATE_KEY`、`OTA_PUBLIC_KEY`、`OTA_CA`）。
- 文档里用占位（`ota.example.com`、`<board-ip>`、`<release_id>`）。
- `.gitignore` 增加：`/out/`、`/releases/`、`*.pem`、`*.sig`（签名产物按需，不入库密钥）。
- 签名只在离线机执行；CI 只做「用公钥验签自检」。

## 测试（扩展 `tests/integration/`）

| 用例 | 操作 | 期望 |
|---|---|---|
| P1 | 生成 fake 产物 → `pack.py` | 清单合法、哈希/大小与文件一致、`release_id` 与目录一致 |
| P2 | 同一输入重复打包 | 结果一致 |
| P3 | 篡改清单一个字节 → `verify.sh` | 验签失败 |
| P4 | 签名正确 | 验签成功 |
| P5 | 起本地 HTTPS → `m1-fetch.sh` 全流程 | 验证 + 下载成功、内容一致 |
| P6 | 篡改组件 → `m1-fetch.sh` | `fsd` 返回校验不符（9） |

复用 `tests/integration/serve.py`（HTTPS + Range）作为静态发布服务器。

## 交付物

- `pack/pack.py`、`pack/sign.sh`、`pack/verify.sh`、`pack/README.md`（替换骨架）
- `verify/m1-fetch.sh`、`verify/README.md`（更新）
- `tests/integration/pack-sign.sh`（或并入 run.sh）
- 更新根 `README.md` 的模块状态

## 验收标准

- 一次完整链路在 X3 上跑通：**构建产物 → pack → 离线 sign → 静态托管 → 设备取清单 → 验签 → 各组件下载 → 哈希一致**。
- 对应 `docs/OTA.md` 5.2 的 T2（清单篡改）、T3（组件篡改）、T4（续传）通过。

## 待确认

1. pack 工具用 **Python 3** 还是纯 bash？（建议 Python，JSON 更安全）
2. 设备端 M1 取清单用 `curl` 还是先用 `fsd` 的 manifest 模式？（建议 curl 取小文件 + 签名验真）
3. `release_id` 取「组件哈希列表的哈希前 12 位」还是直接哈希？按文档保持一致即可。
