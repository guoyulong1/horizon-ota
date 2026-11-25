# pack

发布端：把一次构建的产物整理成**待签名的发布目录**，并用离线私钥签名。

> **只依赖 OpenSSL（EVP SHA-256 / Ed25519）与 nlohmann/json，不依赖 fsd。**
> pack 最终部署在服务器 / CI / 离线签名机，fsd 在设备本机，二者独立。
> 依赖（`3rdparty/nlohmann`）已随模块自带，可整体拷到服务器构建。

## 构建

```bash
cmake -S pack -B build-pack
cmake --build build-pack
```

依赖：系统 OpenSSL（开发包）+ 自带 `3rdparty/nlohmann/json.hpp`（可用 `-DOTA_NLOHMANN_DIR=...` 覆盖）。

## ota-pack

```bash
ota-pack --images output/images \
         --model demo-board --version 1.2.3 --dtb demo-board.dtb \
         --security-version 5 --min-version-code 10000 \
         --hardware-compat "example,demo-board" \
         --hardware-compat "example,demo-board-revb" \
         --output releases-root
```

- `--hardware-compat` 可重复，每个参数是一个兼容 ID。
- `version` 必须是严格的 `主.次.修订`，且次/修订在 0..99（与 `version_code` 公式一致）。
- 设置 `SOURCE_DATE_EPOCH` 可让重复打包产出逐字节相同的清单（可复现）。

产出：

```
<output>/releases/<model>/<version>/<release_id>/
├── manifest.json
└── files/{Image, <dtb>, rootfs.squashfs}
```

## ota-sign（离线机）

```bash
ota-sign --manifest <release>/manifest.json --key private.pem \
         --output <release>/manifest.json.sig
# 私钥也可用 OTA_PRIVATE_KEY 环境变量提供
```

签名覆盖清单**原始字节**（64 字节 Ed25519 签名）；清单一旦签名不能再改，包括改一个空格。

## 样例 manifest.json

`ota-pack` 生成（`created_at` 随构建时间，或由 `SOURCE_DATE_EPOCH` 固定）：

```json
{
  "schema_version": 1,
  "release_id": "4123a8b951f0",
  "version": "1.2.3",
  "version_code": 10203,
  "security_version": 5,
  "model": "demo-board",
  "hardware_compat": [
    "example,demo-board",
    "example,demo-board-revb"
  ],
  "min_version_code": 10000,
  "created_at": "2026-11-25T15:37:07Z",
  "install": {
    "mode": "reboot-required",
    "window": null,
    "reboot_delay_seconds": 60,
    "layouts": [
      "ab",
      "recovery"
    ]
  },
  "components": [
    {
      "name": "kernel",
      "path": "releases/demo-board/1.2.3/4123a8b951f0/files/Image",
      "sha256": "54432409b2df787ddba248dbf3a50e78e3dd34bf473db8694b262212bc57c144",
      "size": 1024,
      "target": "boot"
    },
    {
      "name": "dtb",
      "path": "releases/demo-board/1.2.3/4123a8b951f0/files/demo.dtb",
      "sha256": "8e7b0dfcc958e160e47925178a77098e975b545bf0afd9439ed156b8ae476bb1",
      "size": 512,
      "target": "boot"
    },
    {
      "name": "rootfs",
      "path": "releases/demo-board/1.2.3/4123a8b951f0/files/rootfs.squashfs",
      "sha256": "9d1f997b1a5e4a9ec2b1d8176905c7289a3d5259ff75a72797ad6b5eb424dcfd",
      "size": 2048,
      "target": "rootfs"
    }
  ]
}
```

## 安全

- 私钥只在离线机，**绝不入库**（`.gitignore` 已忽略 `*.pem`、`/releases/`、`*.sig`）。
- `manifest.json.sig` 由发布流程摆渡，不随源码入库。
