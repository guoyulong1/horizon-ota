// 发布端通用加密工具：SHA-256 与 Ed25519 签名。
// 只依赖 OpenSSL，不包含任何 fsd 头文件。
#ifndef OTA_PACK_CRYPTO_H_
#define OTA_PACK_CRYPTO_H_

#include <string>

namespace ota {

// 对内存数据做 SHA-256，返回小写十六进制。
std::string Sha256Hex(const std::string& data);

// 流式计算文件 SHA-256，返回小写十六进制；IO 失败抛 std::runtime_error。
std::string Sha256FileHex(const std::string& path);

// 用 PEM 编码的私钥对 data 做 Ed25519 签名，返回 64 字节签名。
std::string SignEd25519(const std::string& private_key_pem,
                        const std::string& data);

// 从 PEM 私钥文件读取私钥并签名。
std::string SignEd25519File(const std::string& private_key_path,
                            const std::string& data);

}  // namespace ota

#endif  // OTA_PACK_CRYPTO_H_
