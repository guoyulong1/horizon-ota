// 设备端验签实现：只使用 OpenSSL EVP。
#include "crypto.h"

#include <openssl/evp.h>

#include <memory>

namespace ota {
namespace {

bool HexNibble(char ch, unsigned char* out) {
  if (ch >= '0' && ch <= '9') {
    *out = static_cast<unsigned char>(ch - '0');
    return true;
  }
  if (ch >= 'a' && ch <= 'f') {
    *out = static_cast<unsigned char>(ch - 'a' + 10);
    return true;
  }
  if (ch >= 'A' && ch <= 'F') {
    *out = static_cast<unsigned char>(ch - 'A' + 10);
    return true;
  }
  return false;
}

using EvpPkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpMdCtx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

}  // namespace

bool ParseHexPublicKey(const std::string& hex,
                       std::string* raw_bytes_out) {
  if (hex.size() != 64) {
    return false;
  }
  std::string raw(32, '\0');
  for (std::size_t i = 0; i < 32; ++i) {
    unsigned char high = 0;
    unsigned char low = 0;
    if (!HexNibble(hex[2 * i], &high) || !HexNibble(hex[2 * i + 1], &low)) {
      return false;
    }
    raw[i] = static_cast<char>((high << 4) | low);
  }
  *raw_bytes_out = std::move(raw);
  return true;
}

bool VerifyEd25519(const std::string& raw_public_key,
                   const std::string& data,
                   const std::string& signature) {
  if (raw_public_key.size() != 32 || signature.size() != 64) {
    return false;
  }
  EvpPkey key(
      EVP_PKEY_new_raw_public_key(
          EVP_PKEY_ED25519, nullptr,
          reinterpret_cast<const unsigned char*>(raw_public_key.data()),
          raw_public_key.size()),
      EVP_PKEY_free);
  if (!key) {
    return false;
  }
  EvpMdCtx ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx) {
    return false;
  }
  // Ed25519：摘要参数必须为 nullptr，且只能一次喂完。
  if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) !=
      1) {
    return false;
  }
  const int result = EVP_DigestVerify(
      ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()),
      signature.size(),
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
  return result == 1;
}

}  // namespace ota
