// 设备端验签工具：Ed25519 公钥验签（OpenSSL EVP），只依赖 OpenSSL。
#ifndef OTA_VERIFY_CRYPTO_H_
#define OTA_VERIFY_CRYPTO_H_

#include <cstddef>
#include <string>

namespace ota {

// 把 64 个十六进制字符解析成 32 字节原始 Ed25519 公钥。
// 失败返回 false。
bool ParseHexPublicKey(const std::string& hex,
                       std::string* raw_bytes_out);

// 用 32 字节原始 Ed25519 公钥验证 data 的签名（64 字节）。
bool VerifyEd25519(const std::string& raw_public_key,
                   const std::string& data,
                   const std::string& signature);

}  // namespace ota

#endif  // OTA_VERIFY_CRYPTO_H_
