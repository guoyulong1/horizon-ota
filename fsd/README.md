# 固件包安全下载示例

作者：GuoYuLong  
版本：1.0.0

这是一个用 C++20 编写的固件包下载演示。传输使用 libcurl，完整性摘要使用 OpenSSL 的 SHA-256。程序只接受调用方事先提供的摘要，不会从下载地址再取一份“官方校验文件”。

## 安全约束

以下行为写死在下载流程里，没有关闭开关：

- 只允许 `https://`。明文 HTTP、URL 中的用户信息会被拒绝。
- 校验服务器证书和主机名，最低 TLS 1.2。即使经过代理，也不关闭证书校验。
- 重定向只跟随 HTTPS，默认最多 5 次。
- 先把数据写入目标路径旁的 `.part` 临时文件，`fflush` 并 `fsync` 后再计算 SHA-256。
- 摘要与清单一致后，才把临时文件原子改名为正式文件，并去掉可执行权限。
- 摘要不一致时删除临时文件，不留下可被误装的包。

SHA-256 或 MD5 只能说明字节和清单一致。清单本身必须来自带外可信渠道。摘要不能证明清单可信，也不能阻止攻击者重放一份旧的、摘要已知的固件包。本示例不验证发布者签名，也没有防回滚计数。生产环境应在此之后校验签名，并要求版本号单调递增。

## 目录

| 路径 | 说明 |
| --- | --- |
| `include/fsd/` | 对外头文件：状态码、清单、HTTPS 传输、下载编排、SHA-256 |
| `src/` | 对应实现和命令行入口 `main.cc` |
| `examples/demo_package.manifest` | 示例清单，用 example.com 的页面充当占位负载 |
| `3rdparty/` | curl 与 OpenSSL 的源码。由 `3rdparty/fetch.sh` 按固定标签获取 |
| `CMakeLists.txt` | C++20 工程，静态链接 `3rdparty` 里的 curl 和 OpenSSL |
| `.clang-format` | 以 Google C++ Style Guide 为基准，语言标准锁定为 C++20 |

## 依赖与版本

程序不再链接本机的 `libcurl` 或 `libssl`。传输和摘要库都放在 `3rdparty`，由 CMake 编成静态库后再链进可执行文件。日志直接打印，不再使用额外的格式化库。下面是本次验证使用的上游版本。编译器、CMake 和 clang-format 仍用本机工具。

| 组件 | 版本 | 来源 |
| --- | --- | --- |
| g++ | 11.2.0 | 本机编译器，`-std=c++20` |
| CMake | 3.22.1 | 本机构建工具。工程要求不低于 3.16 |
| libcurl | 8.23.0 | 标签 `curl-8_23_0`。OpenSSL 后端，只允许 HTTPS |
| OpenSSL | 3.5.9 | 标签 `openssl-3.5.9`，3.5 LTS。EVP SHA-256 |
| clang-format | 14.0.0 | 只按 `.clang-format` 格式化源码，不参与链接 |

这套 curl 没有编入 nghttp2，HTTPS 走 HTTP/1.1。库支持 HTTP/2 时才会协商 HTTP/2。

## 构建

先取出第三方源码，再编译。`fetch.sh` 会核对 OpenSSL 与 curl 归档的 SHA-256，不匹配则不解压。首次编译会构建 OpenSSL，时间比较长。

```bash
./3rdparty/fetch.sh
cmake -S . -B build
cmake --build build
```

未指定构建类型时默认为 Release。可执行文件是 `build/firmware_secure_download`。

## 运行

不访问网络的自检（SHA-256、MD5 空串向量，以及清单解析）：

```bash
./build/firmware_secure_download --self-test
```

按清单下载。请在项目根目录执行，清单里的 `output` 是相对路径：

```bash
./build/firmware_secure_download --manifest examples/demo_package.manifest
```

直接指定地址、摘要和保存路径：

```bash
./build/firmware_secure_download \
  --url https://example.com/ \
  --sha256 25ddf2c883e0d1958ea971d279a7e4f0fd446724ee3db7db19dadabd4a62e484 \
  --output out/example.html \
  --expected-size 577
```

也可以只校验 MD5：

```bash
./build/firmware_secure_download \
  --url https://example.com/ \
  --md5 fd99df0d4f6a7f085a21af760f70facb \
  --output out/example.html \
  --expected-size 577
```

目标文件已存在时需要加 `--force`。`--resume` 会保留并续传 `.part`；服务器不支持区间请求时改为完整下载。SHA-256 和 MD5 都在临时文件发布前核对，格式不合法会在下载前拒绝，摘要不一致时删除临时文件。两者都给出时都要通过。

### 示例清单

`examples/demo_package.manifest` 的占位负载是 `https://example.com/`，SHA-256 采集于 2026-11-20：

```text
sha256=25ddf2c883e0d1958ea971d279a7e4f0fd446724ee3db7db19dadabd4a62e484
size=577
```

页面内容一旦变化，校验会失败并删除临时文件。这是预期的安全行为，把清单里的摘要换成可信渠道给出的新值即可。

清单为按行的 `key=value`。必填字段是 `url`、`output`，以及 `sha256` 或 `md5` 至少一个。可选字段是 `name`、`size`、`max_bytes`。未知字段会直接报错。摘要接受大小写，读入后规范为小写。MD5 比 SHA-256 弱，只作为可选完整性校验。

### 常用选项

| 选项 | 说明 |
| --- | --- |
| `--sha256 <十六进制>` | SHA-256。与 `--md5` 至少提供一个 |
| `--md5 <十六进制>` | MD5，32 位十六进制。比 SHA-256 弱 |
| `--expected-size <字节>` | 清单声明的精确大小 |
| `--max-bytes <字节>` | 体积上限，默认 256 MiB |
| `--connect-timeout <秒>` | 连接超时，默认 15 |
| `--timeout <秒>` | 整个传输超时，默认 300 |
| `--max-redirects <次数>` | HTTPS 重定向上限，默认 5，最大 20 |
| `--ca-file <路径>` | 指定 CA 证书包 |
| `--pinned-pubkey <pin>` | 公钥锁定，格式 `sha256//BASE64` |
| `--resume` | 续传 `.part` |
| `--force` | 覆盖已存在的目标文件 |
| `--self-test` | 运行本地自检 |
| `-h`, `--help` | 显示帮助 |

### 退出码

进程退出码与内部错误码一致。

| 退出码 | 名称 | 含义 |
| --- | --- | --- |
| 0 | ok | 成功 |
| 2 | invalid-argument | 参数或清单不合法，或体积与清单不符 |
| 3 | io | 读写或目标文件已存在 |
| 4 | network | 连接、超时或其他传输失败 |
| 5 | tls | 证书、主机名或公钥锁定失败 |
| 6 | http | HTTP 状态不可接受 |
| 7 | protocol | 不是 HTTPS，或重定向越界 |
| 8 | size-limit | 超过体积上限 |
| 9 | checksum-mismatch | SHA-256 或 MD5 与给出的摘要不一致 |
| 10 | range-not-supported | 服务器不支持断点续传 |
