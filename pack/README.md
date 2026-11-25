# pack

发布端：把一次构建的产物整理成**待签名的发布目录**，并用离线私钥签名。

> **只依赖 OpenSSL（EVP SHA-256 / Ed25519）与 nlohmann/json，不依赖 fsd。**
> pack 最终部署在服务器 / CI / 离线签名机，fsd 在设备本机，二者独立。

## 构建

```bash
cmake -S pack -B build-pack
cmake --build build-pack
```

依赖：系统 OpenSSL（开发包）+ vendored `3rdparty/nlohmann/json.hpp`（默认路径，可用 `-DOTA_NLOHMANN_DIR=...` 覆盖）。

## ota-pack

```bash
ota-pack --images output/images \
         --model demo-board --version 1.2.3 --dtb demo-board.dtb \
         --security-version 5 --min-version-code 10000 \
         --hardware-compat "example,demo-board" \
         --hardware-compat "example,demo-board-revb" \
         --output releases-root
```

产出：

```
<output>/releases/<model>/<version>/<release_id>/
├── manifest.json
└── files/{Image, <dtb>, rootfs.squashfs}
```

字段与目录布局见 `docs/OTA.md` 2.2 / 2.3。

## ota-sign（离线机）

```bash
ota-sign --manifest <release>/manifest.json --key private.pem \
         --output <release>/manifest.json.sig
# 私钥也可用 OTA_PRIVATE_KEY 环境变量提供
```

签名覆盖清单**原始字节**（64 字节 Ed25519 签名）；清单一旦签名不能再改，包括改一个空格。

## 安全

- 私钥只在离线机，**绝不入库**（`.gitignore` 已忽略 `*.pem`、`/releases/`、`*.sig`）。
- `manifest.json.sig` 由发布流程摆渡，不随源码入库。
